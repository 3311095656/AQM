/*
 * 断网数据环形缓存（offline ring buffer on W25Q64 SPI Flash）
 *
 * 硬件：STM32F407-RT-SPARK 板载 W25Q64（8MB，SPI2 总线，CS=PB12），
 *       由 BSP_USING_SPI_FLASH 启用后 rt_sfud_flash_probe("W25Q64") 挂载。
 *
 * 设计：
 *   - 缓存区位于 Flash 末尾 OFFLINE_BUF_SECTORS 个 4KB 扇区；
 *   - 每条记录占用独立扇区：[扇区头(32B) + payload]，断电安全：
 *     先擦扇区 -> 写记录 -> 回写扇区头 MAGIC_DONE 标记提交；
 *   - 记录带 32 位自增序号 seq 与 CRC16，读取侧可识别半写入/损坏记录；
 *   - 环形覆盖：写满后回到起点擦除重写（擦写均衡按扇区轮转）；
 *   - 补传：MQTT 上线后按 seq 从小到大重发，发送成功逐条提交删除标记。
 *
 * 扇区头布局（32 字节）：
 *   [0..3]  magic      'AQLG'（写入中）或 'AQOK'（提交完成）
 *   [4..7]  seq        记录序号（小端）
 *   [8..11] timestamp  时间戳（秒，0 表示未知）
 *   [12..13] len       payload 长度
 *   [14..15] crc       payload CRC16
 *   [16..31] 保留
 */

#include <rtthread.h>
#include <string.h>
#include <stdlib.h>
#include "offline_cache.h"
#include "spi_flash_sfud.h"

/* sfud_cfg.h 内部定义了 DBG_TAG "SFUD"，恢复为本模块日志标签 */
#undef DBG_TAG
#undef DBG_LVL
#define DBG_TAG "ocache"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ---- 布局参数 ---- */
#define OFF_SECTOR_SIZE     4096UL
#define OFF_BUF_SECTORS     512UL                     /* 2MB / 4KB */
#define OFF_BUF_ADDR_BASE   (0UL)                     /* 相对 Flash 起始的偏移由模块统一换算 */
#define OFF_HDR_SIZE        32UL
#define OFF_MAX_PAYLOAD     (OFF_SECTOR_SIZE - OFF_HDR_SIZE)

/* Flash 总容量 8MB，缓存区放在末尾 */
#define OFF_FLASH_TOTAL     (8UL * 1024 * 1024)
#define OFF_BUF_START       (OFF_FLASH_TOTAL - OFF_BUF_SECTORS * OFF_SECTOR_SIZE)

/* 魔数：'AQLG'=写入中未提交；'AQOK'=已提交有效 */
#define MAGIC_WRITING       0x474C5141UL  /* "AQLG" little-endian */
#define MAGIC_DONE          0x4B4F5141UL  /* "AQOK" little-endian */

/* 扇区头结构（与上面字节布局一一对应，pack 避免 padding） */
#pragma pack(push, 1)
struct off_hdr
{
    rt_uint32_t magic;
    rt_uint32_t seq;
    rt_uint32_t timestamp;
    rt_uint16_t len;
    rt_uint16_t crc;
    rt_uint8_t  rsv[16];
};
#pragma pack(pop)

/* 编译期断言：扇区头必须 32 字节 */
typedef char off_hdr_size_check[(sizeof(struct off_hdr) == OFF_HDR_SIZE) ? 1 : -1];

/* ---- CRC16 (CCITT 0x1021) ---- */
static rt_uint16_t crc16(const rt_uint8_t *data, rt_uint32_t len)
{
    rt_uint16_t crc = 0xFFFF;
    while (len--)
    {
        crc ^= (rt_uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

/* ---- 模块状态 ---- */
static sfud_flash_t s_flash;                /* SFUD 设备句柄 */
static rt_uint32_t s_wsec;                  /* 下一个写入扇区号（0..OFF_BUF_SECTORS-1） */
static rt_uint32_t s_next_seq;              /* 下一条记录序号 */
static rt_uint32_t s_pending;               /* 待补传条数 */
static rt_uint32_t s_read_sec;              /* 补传读取游标（扇区号） */
static rt_uint32_t s_read_seq;              /* 补传游标当前扇区记录 seq */
static rt_uint32_t s_last_pop_sec;          /* 最近一次 pop 成功的扇区号 */
static rt_uint32_t s_last_pop_seq;          /* 最近一次 pop 成功的记录 seq */
static int s_inited;

/* 扇区号 -> Flash 绝对地址 */
static inline rt_uint32_t sec_addr(rt_uint32_t sec)
{
    return OFF_BUF_START + sec * OFF_SECTOR_SIZE;
}

/*
 * 读取扇区头并校验
 * 返回 0 有效（*hdr 填充），-1 无效/空
 */
static int hdr_load(rt_uint32_t sec, struct off_hdr *hdr)
{
    if (sfud_read(s_flash, sec_addr(sec), sizeof(*hdr), (rt_uint8_t *)hdr) != SFUD_SUCCESS)
        return -1;
    if (hdr->magic != MAGIC_DONE)
        return -1;
    if (hdr->len == 0 || hdr->len > OFF_MAX_PAYLOAD)
        return -1;
    return 0;
}

/* 找到"下一个写入位置"：顺序扫描提交记录，定位最大 seq 的下一条 */
static void scan_init(void)
{
    struct off_hdr hdr;
    rt_uint32_t max_seq = 0, max_sec = OFF_BUF_SECTORS;
    rt_uint32_t i, valid = 0;

    s_next_seq = 1;
    for (i = 0; i < OFF_BUF_SECTORS; i++)
    {
        if (hdr_load(i, &hdr) == 0)
        {
            valid++;
            if ((rt_int32_t)(hdr.seq - max_seq) > 0)
            {
                max_seq = hdr.seq;
                max_sec = i;
            }
        }
    }

    if (valid == 0)
    {
        /* 空缓存 */
        s_wsec = 0;
        s_next_seq = 1;
        s_pending = 0;
    }
    else
    {
        s_wsec = (max_sec + 1) % OFF_BUF_SECTORS;
        s_next_seq = max_seq + 1;
        s_pending = valid;
    }
    LOG_I("scan: valid=%u next_seq=%u wsec=%u", valid, s_next_seq, s_wsec);
}

int offline_cache_init(void)
{
    if (s_inited)
        return RT_EOK;

    s_flash = rt_sfud_flash_find_by_dev_name("W25Q64");
    if (!s_flash)
    {
        LOG_E("W25Q64 not found (check BSP_USING_SPI_FLASH)");
        return -RT_ERROR;
    }

    scan_init();
    s_read_sec = s_wsec;
    s_read_seq = 0;
    s_inited = 1;
    LOG_I("offline cache ready: %uKB @0x%06X", OFF_BUF_SECTORS * 4, OFF_BUF_START);
    return RT_EOK;
}

/*
 * 写入一条 JSON 记录（断电安全：擦-写-提交三步）
 * 成功返回 0；未初始化/超长/Flash 错误返回 -1
 */
int offline_cache_push(const char *json, rt_uint32_t timestamp)
{
    struct off_hdr hdr;
    rt_uint32_t addr;
    rt_uint16_t crc;
    size_t len;

    if (!s_inited || !json)
        return -1;

    len = strlen(json);
    if (len == 0 || len > OFF_MAX_PAYLOAD)
    {
        LOG_W("push: bad len %u (max %u)", len, OFF_MAX_PAYLOAD);
        return -1;
    }

    crc = crc16((const rt_uint8_t *)json, len);
    addr = sec_addr(s_wsec);

    /* 1. 擦除目标扇区 */
    if (sfud_erase(s_flash, addr, OFF_SECTOR_SIZE) != SFUD_SUCCESS)
    {
        LOG_E("push: erase sec %u failed", s_wsec);
        return -1;
    }

    /* 2. 写记录头（magic=AQLG 未提交状态）+ payload */
    rt_memset(&hdr, 0, sizeof(hdr));
    hdr.magic = MAGIC_WRITING;
    hdr.seq = s_next_seq;
    hdr.timestamp = timestamp;
    hdr.len = (rt_uint16_t)len;
    hdr.crc = crc;
    if (sfud_write(s_flash, addr, sizeof(hdr), (const rt_uint8_t *)&hdr) != SFUD_SUCCESS ||
        sfud_write(s_flash, addr + OFF_HDR_SIZE, len, (const rt_uint8_t *)json) != SFUD_SUCCESS)
    {
        LOG_E("push: write sec %u failed", s_wsec);
        return -1;
    }

    /* 3. 回写提交标记（1->0 位翻转，无需再擦） */
    hdr.magic = MAGIC_DONE;
    if (sfud_write(s_flash, addr, 4, (const rt_uint8_t *)&hdr.magic) != SFUD_SUCCESS)
    {
        LOG_E("push: commit sec %u failed", s_wsec);
        return -1;
    }

    LOG_D("push: sec=%u seq=%u len=%u", s_wsec, s_next_seq, len);
    s_next_seq++;
    s_wsec = (s_wsec + 1) % OFF_BUF_SECTORS;
    s_pending++;

    return 0;
}

/*
 * 逐条读取待补传记录（从最老记录开始按 seq 升序）
 * 返回 0 读到一条（payload 写入 buf，*seq/*ts 返回元数据）
 * 返回 -1 没有更多记录
 */
int offline_cache_pop(char *buf, size_t bufsize,
                      rt_uint32_t *seq, rt_uint32_t *ts)
{
    struct off_hdr hdr;
    rt_uint32_t i, scanned;

    if (!s_inited || !buf)
        return -1;

    /* 从读取游标向后扫描最多一圈，找有效记录 */
    for (scanned = 0; scanned < OFF_BUF_SECTORS; scanned++)
    {
        i = (s_read_sec + scanned) % OFF_BUF_SECTORS;
        if (hdr_load(i, &hdr) == 0)
        {
            if (hdr.len >= bufsize)
            {
                LOG_W("pop: buf too small (%u < %u), drop sec=%u", bufsize, hdr.len, i);
                /* buf 不够：作废该记录防止死循环 */
                static const rt_uint32_t zero = 0;
                sfud_write(s_flash, sec_addr(i), 4, (const rt_uint8_t *)&zero);
                continue;
            }
            if (sfud_read(s_flash, sec_addr(i) + OFF_HDR_SIZE, hdr.len, (rt_uint8_t *)buf) != SFUD_SUCCESS)
                return -1;
            buf[hdr.len] = '\0';

            /* CRC 校验，坏记录作废（写零魔法，1->0 翻转）防止死循环 */
            if (crc16((const rt_uint8_t *)buf, hdr.len) != hdr.crc)
            {
                static const rt_uint32_t zero = 0;
                LOG_W("pop: crc bad sec=%u, drop", i);
                sfud_write(s_flash, sec_addr(i), 4, (const rt_uint8_t *)&zero);
                continue;
            }

            s_read_sec = (i + 1) % OFF_BUF_SECTORS;
            s_last_pop_sec = i;
            s_last_pop_seq = hdr.seq;
            if (seq) *seq = hdr.seq;
            if (ts)  *ts  = hdr.timestamp;
            return 0;
        }
    }
    return -1;
}

/*
 * 标记一条记录已补传成功 -> 作废该扇区
 * （把 magic 写成全 0 即可：1->0 翻转无需擦除）
 */
void offline_cache_commit(rt_uint32_t seq)
{
    if (!s_inited)
        return;

    /* 快速路径：刚 pop 的记录直接提交 */
    if (s_last_pop_seq == seq && s_last_pop_sec < OFF_BUF_SECTORS)
    {
        static const rt_uint32_t zero = 0;
        sfud_write(s_flash, sec_addr(s_last_pop_sec), 4, (const rt_uint8_t *)&zero);
        if (s_pending) s_pending--;
        LOG_D("commit: seq=%u sec=%u left=%u", seq, s_last_pop_sec, s_pending);
        return;
    }

    /* 慢速路径：跨会话/乱序提交，全表查找 */
    struct off_hdr hdr;
    for (rt_uint32_t i = 0; i < OFF_BUF_SECTORS; i++)
    {
        if (hdr_load(i, &hdr) == 0 && hdr.seq == seq)
        {
            static const rt_uint32_t zero = 0;
            sfud_write(s_flash, sec_addr(i), 4, (const rt_uint8_t *)&zero);
            if (s_pending) s_pending--;
            LOG_D("commit: seq=%u sec=%u left=%u", seq, i, s_pending);
            return;
        }
    }
}

/* 查询待补传条数 */
rt_uint32_t offline_cache_pending(void)
{
    return s_pending;
}

/* 清空整个缓存区（测试用，全片擦除缓存区段） */
void offline_cache_clear(void)
{
    if (!s_inited)
        return;
    sfud_erase(s_flash, OFF_BUF_START, OFF_BUF_SECTORS * OFF_SECTOR_SIZE);
    s_wsec = 0;
    s_next_seq = 1;
    s_pending = 0;
    s_read_sec = 0;
    LOG_I("cache cleared");
}

/* ---- msh 调试命令 ---- */
#ifdef FINSH_USING_MSH
#include <finsh.h>

static void msh_ocache_status(int argc, char **argv)
{
    rt_kprintf("flash: %s\n", s_flash ? "W25Q64 OK" : "not found");
    rt_kprintf("inited: %d, pending: %u\n", s_inited, s_pending);
    rt_kprintf("wsec: %u, next_seq: %u\n", s_wsec, s_next_seq);
}
MSH_CMD_EXPORT(msh_ocache_status, offline cache status);

static void msh_ocache_test(int argc, char **argv)
{
    char buf[256];
    rt_uint32_t seq, ts;
    int n = 0;

    if (offline_cache_init() != RT_EOK)
        return;

    /* 写入 3 条模拟记录 */
    offline_cache_push("{\"id\":\"t1\",\"v\":1}", 1000);
    offline_cache_push("{\"id\":\"t2\",\"v\":2}", 2000);
    offline_cache_push("{\"id\":\"t3\",\"v\":3}", 3000);
    rt_kprintf("pushed 3, pending=%u\n", offline_cache_pending());

    /* 逐条读出 */
    while (offline_cache_pop(buf, sizeof(buf), &seq, &ts) == 0)
    {
        rt_kprintf("pop[%u] ts=%u: %s\n", seq, ts, buf);
        offline_cache_commit(seq);
        n++;
    }
    rt_kprintf("popped %d, pending=%u\n", n, offline_cache_pending());
}
MSH_CMD_EXPORT(msh_ocache_test, offline cache write/read test);

static void msh_ocache_clear(int argc, char **argv)
{
    offline_cache_clear();
    rt_kprintf("cache cleared\n");
}
MSH_CMD_EXPORT(msh_ocache_clear, erase offline cache region);

#endif /* FINSH_USING_MSH */
