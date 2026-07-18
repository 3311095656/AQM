#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include <drv_lcd.h>
#include "oled_app.h"
#include "ac_app.h"
#include "humidifier_app.h"

#define DBG_TAG "oled"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* ==================== SSD1306 OLED 参数 ==================== */
#define OLED_I2C_BUS    "i2c4"
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_PAGES      (OLED_HEIGHT / 8)

#define OLED_CMD        0x00
#define OLED_DATA       0x40

#define FONT_ASC_W      8
#define FONT_ASC_H      16
#define FONT_CN_W       16
#define FONT_CN_H       16

static struct rt_i2c_bus_device *oled_bus = RT_NULL;

/* 帧缓冲：static 避免栈溢出 */
static rt_uint8_t oled_fb[OLED_WIDTH * OLED_PAGES];

/* ---- I2C 底层操作 ---- */

static void oled_write_byte(rt_uint8_t is_cmd, rt_uint8_t byte)
{
    struct rt_i2c_msg msgs[2];
    rt_uint8_t ctrl = is_cmd ? OLED_CMD : OLED_DATA;

    msgs[0].addr  = OLED_ADDR;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf   = &ctrl;
    msgs[0].len   = 1;

    msgs[1].addr  = OLED_ADDR;
    msgs[1].flags = RT_I2C_WR | RT_I2C_NO_START;
    msgs[1].buf   = &byte;
    msgs[1].len   = 1;

    rt_i2c_transfer(oled_bus, msgs, 2);
}

static void oled_write_bytes(const rt_uint8_t *buf, rt_uint32_t len)
{
    struct rt_i2c_msg msgs[2];
    rt_uint8_t ctrl = OLED_DATA;

    msgs[0].addr  = OLED_ADDR;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf   = &ctrl;
    msgs[0].len   = 1;

    msgs[1].addr  = OLED_ADDR;
    msgs[1].flags = RT_I2C_WR | RT_I2C_NO_START;
    msgs[1].buf   = (rt_uint8_t *)buf;
    msgs[1].len   = len;

    rt_i2c_transfer(oled_bus, msgs, 2);
}

static void oled_set_pos(rt_uint8_t page, rt_uint8_t col)
{
    oled_write_byte(1, 0xB0 + page);
    oled_write_byte(1, col & 0x0F);
    oled_write_byte(1, 0x10 | (col >> 4));
}

static void oled_clear(void)
{
    rt_uint8_t zero_buf[OLED_WIDTH];
    rt_memset(zero_buf, 0, sizeof(zero_buf));
    for (rt_uint8_t page = 0; page < OLED_PAGES; page++)
    {
        oled_set_pos(page, 0);
        oled_write_bytes(zero_buf, OLED_WIDTH);
    }
}

static void oled_refresh(const rt_uint8_t *buf)
{
    for (rt_uint8_t page = 0; page < OLED_PAGES; page++)
    {
        oled_set_pos(page, 0);
        oled_write_bytes(buf + page * OLED_WIDTH, OLED_WIDTH);
    }
}

/* ---- 中文字库（引用 drv_lcd 的统一索引） ---- */

extern const rt_uint8_t cn_font_1616[];

static int oled_find_cn_char(rt_uint16_t unicode)
{
    for (int i = 0; i < (int)CN_FONT_CHAR_COUNT; i++)
    {
        if (cn_font_index[i] == unicode)
            return i;
    }
    return -1;
}

/* ---- 字体绘制 ---- */

static void oled_draw_ascii(rt_uint8_t x, rt_uint8_t y, char ch, rt_uint8_t *buf)
{
    extern const rt_uint8_t asc2_1608[];
    rt_uint16_t offset;

    if (ch >= ' ' && ch <= '~')
        offset = (ch - ' ') * 16;
    else
        offset = 0;

    const rt_uint8_t *font = &asc2_1608[offset];

    for (rt_uint8_t i = 0; i < 16; i++)
    {
        rt_uint8_t page = (y + i) / 8;
        rt_uint8_t bit  = (y + i) % 8;
        if (page >= OLED_PAGES) break;

        rt_uint8_t *p = buf + page * OLED_WIDTH + x;
        for (rt_uint8_t j = 0; j < 8; j++)
        {
            if ((font[i] << j) & 0x80)
                p[j] |= (1 << bit);
            else
                p[j] &= ~(1 << bit);
        }
    }
}

static void oled_draw_chinese(rt_uint8_t x, rt_uint8_t y, rt_uint16_t unicode, rt_uint8_t *buf)
{
    int idx = oled_find_cn_char(unicode);
    if (idx < 0) return;

    const rt_uint8_t *font = &cn_font_1616[idx * 32];

    for (rt_uint8_t i = 0; i < 16; i++)
    {
        rt_uint8_t page = (y + i) / 8;
        rt_uint8_t bit  = (y + i) % 8;
        if (page >= OLED_PAGES) break;

        rt_uint8_t *p = buf + page * OLED_WIDTH + x;
        for (rt_uint8_t j = 0; j < 16; j++)
        {
            rt_uint8_t byte_idx = i * 2 + (j / 8);
            rt_uint8_t bit_in_byte = 7 - (j % 8);

            if (font[byte_idx] & (1 << bit_in_byte))
                p[j] |= (1 << bit);
            else
                p[j] &= ~(1 << bit);
        }
    }
}

static void oled_show_utf8(rt_uint8_t x, rt_uint8_t y,
                           const char *str, rt_uint8_t *buf)
{
    rt_uint8_t cx = x;
    rt_uint8_t cy = y;

    while (*str)
    {
        rt_uint8_t ch = (rt_uint8_t)*str;

        if (ch < 0x80)
        {
            if (cx + FONT_ASC_W > OLED_WIDTH)
            {
                cx = 0;
                cy += FONT_CN_H;
                if (cy + FONT_CN_H > OLED_HEIGHT) break;
            }
            oled_draw_ascii(cx, cy, (char)ch, buf);
            cx += FONT_ASC_W;
            str++;
        }
        else if ((ch & 0xE0) == 0xE0)
        {
            rt_uint16_t unicode = ((ch & 0x0F) << 12) |
                                  (((rt_uint8_t)str[1] & 0x3F) << 6) |
                                   ((rt_uint8_t)str[2] & 0x3F);

            if (cx + FONT_CN_W > OLED_WIDTH)
            {
                cx = 0;
                cy += FONT_CN_H;
                if (cy + FONT_CN_H > OLED_HEIGHT) break;
            }

            rt_uint8_t cn_y = cy + (FONT_CN_H - 16) / 2;
            oled_draw_chinese(cx, cn_y, unicode, buf);
            cx += FONT_CN_W;
            str += 3;
        }
        else
        {
            str++;
        }
    }
}

/* ---- SSD1306 初始化 ---- */

static void oled_init_ssd1306(void)
{
    rt_thread_mdelay(100);

    oled_write_byte(1, 0xAE);
    oled_write_byte(1, 0xD5); oled_write_byte(1, 0x80);
    oled_write_byte(1, 0xA8); oled_write_byte(1, 0x3F);
    oled_write_byte(1, 0xD3); oled_write_byte(1, 0x00);
    oled_write_byte(1, 0x40);
    oled_write_byte(1, 0x8D); oled_write_byte(1, 0x14);
    oled_write_byte(1, 0x20); oled_write_byte(1, 0x00);
    oled_write_byte(1, 0xA1);
    oled_write_byte(1, 0xC8);
    oled_write_byte(1, 0xDA); oled_write_byte(1, 0x12);
    oled_write_byte(1, 0x81); oled_write_byte(1, 0xCF);
    oled_write_byte(1, 0xD9); oled_write_byte(1, 0xF1);
    oled_write_byte(1, 0xDB); oled_write_byte(1, 0x40);
    oled_write_byte(1, 0xA4);
    oled_write_byte(1, 0xA6);
    oled_write_byte(1, 0xAF);

    oled_clear();
}

int oled_app_init(void)
{
    oled_bus = rt_i2c_bus_device_find(OLED_I2C_BUS);
    if (oled_bus == RT_NULL)
    {
        LOG_E("OLED I2C bus '%s' not found!", OLED_I2C_BUS);
        return -RT_ERROR;
    }

    oled_init_ssd1306();

    LOG_I("OLED initialized on %s (0x%02X)", OLED_I2C_BUS, OLED_ADDR);
    return RT_EOK;
}

void oled_app_update(const sensor_data_t *data, int gas_ok, int presence)
{
    if (oled_bus == RT_NULL) return;

    char line[32];

    rt_memset(oled_fb, 0, sizeof(oled_fb));

    /* 第 0 行：室内 + 人:有/无 + 燃气 */
    oled_show_utf8(0, 0, "室内", oled_fb);
    rt_snprintf(line, sizeof(line), "人:%s 燃:%s",
            presence ? "有" : "无",
            gas_ok ? "OK" : "!!");
    oled_show_utf8(34, 0, line, oled_fb);

    /* 第 1 行：室内温度（DHT22） */
    if (data->valid)
        rt_snprintf(line, sizeof(line), "温度:%d.%d\u2103",
                (int)data->in_temperature, (int)(data->in_temperature * 10) % 10);
    else
        rt_snprintf(line, sizeof(line), "温度:--\u2103");
    oled_show_utf8(0, 16, line, oled_fb);

    /* 第 2 行：室内湿度（DHT22） */
    if (data->valid)
        rt_snprintf(line, sizeof(line), "湿度:%d.%d%%",
                (int)data->in_humidity, (int)(data->in_humidity * 10) % 10);
    else
        rt_snprintf(line, sizeof(line), "湿度:--%%");
    oled_show_utf8(0, 32, line, oled_fb);

    /* 第 3 行：空调 + 加湿/除湿状态（固定位置防抖动） */
    {
        const char *ac_str[]   = {"Off","C","H","A"};
        const char *humi_str[] = {"Off","U","D","A"};
        int ac_val = (int)ac_get_mode() & 0x03;
        int humi_val = (int)humi_get_mode() & 0x03;

        oled_show_utf8(0, 48,  "AC:", oled_fb);
        oled_show_utf8(30, 48, ac_str[ac_val], oled_fb);
        oled_show_utf8(60, 48, "HUM:", oled_fb);
        oled_show_utf8(90, 48, humi_str[humi_val], oled_fb);
    }

    oled_refresh(oled_fb);
}
