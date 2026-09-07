/*
 * 断网数据环形缓存（offline ring buffer on W25Q64 SPI Flash）
 *
 * 用途：MQTT 与 OneNET 平台断连期间，传感器 JSON 数据落盘缓存；
 *       重连后按序号从旧到新逐条补传。
 * 硬件：板载 W25Q64（8MB，SPI2，CS=PB12），依赖 BSP_USING_SPI_FLASH。
 * 布局：缓存区占用 Flash 末尾 2MB（512 个 4KB 扇区），一条记录一个扇区。
 * 可靠性：擦-写-提交三步写入 + CRC16 校验，断电不产生半条脏记录。
 */

#ifndef __OFFLINE_CACHE_H__
#define __OFFLINE_CACHE_H__

#include <rtthread.h>
#include <stddef.h>

/* 初始化（懒加载 W25Q64 设备，重复调用安全）。返回 0 成功。 */
int offline_cache_init(void);

/* 写入一条 JSON 记录。返回 0 成功，-1 失败。 */
int offline_cache_push(const char *json, rt_uint32_t timestamp);

/*
 * 读取最老一条待补传记录
 * buf/bufsize  : 输出缓冲（需 >= 记录长度 + 1）
 * seq/ts       : 输出记录序号与时间戳，可为 RT_NULL
 * 返回 0 读到一条，-1 没有更多记录
 */
int offline_cache_pop(char *buf, size_t bufsize,
                      rt_uint32_t *seq, rt_uint32_t *ts);

/* 标记 seq 记录已补传成功（作废扇区） */
void offline_cache_commit(rt_uint32_t seq);

/* 当前待补传条数 */
rt_uint32_t offline_cache_pending(void);

/* 清空缓存区（擦除 2MB 缓存段，仅测试用） */
void offline_cache_clear(void);

#endif /* __OFFLINE_CACHE_H__ */
