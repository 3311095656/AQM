#include <rtthread.h>
#include <drv_lcd.h>
#include <time.h>
#include "lcd_app.h"

/* ==================== LCD 显示布局参数 ==================== */
#define LCD_DATA_X      85      /* 数据值的 X 起始坐标 */
#define LCD_TEMP_Y      45      /* 温度行的 Y 坐标 */
#define LCD_HUMI_Y      80      /* 湿度行的 Y 坐标 */
#define LCD_LIGHT_Y     135     /* 光照行的 Y 坐标 */
#define LCD_FONT_SIZE   32      /* 传感器数据字体大小（像素） */
#define LCD_DATA_X_END  290     /* 数据区域右边界 X 坐标 */

#define LCD_STATUS_FONT 24      /* 状态栏字体大小（像素） */
#define LCD_WIFI_Y      182     /* WiFi 状态行 Y 坐标 */
#define LCD_MQTT_Y      210     /* MQTT 状态行 Y 坐标 */
#define LCD_STATUS_X    65      /* 状态文本 X 起始坐标 */

#define LCD_TITLE_Y     10      /* 标题行 Y 坐标 */
#define LCD_MODE_X      75      /* Mode 文本 X 坐标 */
#define LCD_MODE_FONT   24      /* Mode 字体大小 */

/*
 * 绘制分隔线
 * 在指定的 Y 坐标范围内绘制一条横向分隔线（白色背景上的分隔区域）。
 *   y_start - 分隔线起始 Y 坐标
 *   y_end   - 分隔线结束 Y 坐标
 */
static void lcd_draw_separator(rt_uint16_t y_start, rt_uint16_t y_end)
{
    for (rt_uint16_t y = y_start; y <= y_end; y++)
    {
        lcd_draw_line(0, y, 240, y);
    }
}

/*
 * LCD 显示界面初始化
 * 清屏为白色，设置前景色为黑色，绘制分隔线，
 * 然后显示标题"室外"以及"温度"、"湿度"、"光强"三个标签。
 */
void lcd_app_init(void)
{
    lcd_clear(WHITE);
    lcd_set_color(WHITE, BLACK);

    /* 在温度区和光照区之间绘制分隔线 */
    lcd_draw_separator(114, 128);

    /* 显示标题和模式 */
    lcd_show_string(5, LCD_TITLE_Y, LCD_FONT_SIZE, "室外");
    lcd_show_string(LCD_MODE_X, LCD_TITLE_Y + (LCD_FONT_SIZE - LCD_MODE_FONT) / 2,
                    LCD_MODE_FONT, "Mode:Auto  ");

    lcd_show_string(5, LCD_TEMP_Y, LCD_FONT_SIZE, "温度:");
    lcd_show_string(5, LCD_HUMI_Y, LCD_FONT_SIZE, "湿度:");
    lcd_show_string(5, LCD_LIGHT_Y, LCD_FONT_SIZE, "光强:");

    /* 恢复前景色为黑色 */
    lcd_set_color(WHITE, BLACK);

    /* 显示状态标签（24×24 字体） */
    lcd_show_string(5, LCD_WIFI_Y, LCD_STATUS_FONT, "WiFi:");
    lcd_show_string(5, LCD_MQTT_Y, LCD_STATUS_FONT, "MQTT:");

    /* 初始化状态显示 */
    lcd_app_update_status(0, 0);
}

/*
 * 更新 LCD 屏幕上的传感器数据显示
 * 先用白色填充旧数据区域（避免残留），再显示新的数值。
 * 温度显示格式：XX.XC
 * 湿度显示格式：XX.X%
 * 光照显示格式：XX.Xlux
 */
void lcd_app_update(const sensor_data_t *data)
{
    if (data == RT_NULL)
        return;

    /* 更新室外温度显示（AHT10） */
    lcd_fill(LCD_DATA_X, LCD_TEMP_Y, LCD_DATA_X_END, LCD_TEMP_Y + LCD_FONT_SIZE - 1, WHITE);
    if (data->valid)
        lcd_show_string(LCD_DATA_X, LCD_TEMP_Y, LCD_FONT_SIZE, "%d.%d\u2103",
                (int)data->out_temperature, (int)(data->out_temperature * 10) % 10);
    else
        lcd_show_string(LCD_DATA_X, LCD_TEMP_Y, LCD_FONT_SIZE, "--\u2103");

    /* 更新室外湿度显示（AHT10） */
    lcd_fill(LCD_DATA_X, LCD_HUMI_Y, LCD_DATA_X_END, LCD_HUMI_Y + LCD_FONT_SIZE - 1, WHITE);
    if (data->valid)
        lcd_show_string(LCD_DATA_X, LCD_HUMI_Y, LCD_FONT_SIZE, "%d.%d%%",
                (int)data->out_humidity, (int)(data->out_humidity * 10) % 10);
    else
        lcd_show_string(LCD_DATA_X, LCD_HUMI_Y, LCD_FONT_SIZE, "--%%");

    /* 更新光照强度显示 */
    lcd_fill(LCD_DATA_X, LCD_LIGHT_Y, LCD_DATA_X_END, LCD_LIGHT_Y + LCD_FONT_SIZE - 1, WHITE);
    if (data->valid)
        lcd_show_string(LCD_DATA_X, LCD_LIGHT_Y, LCD_FONT_SIZE, "%d.%dlux",
                (int)data->brightness, ((int)(10 * data->brightness) % 10));
    else
        lcd_show_string(LCD_DATA_X, LCD_LIGHT_Y, LCD_FONT_SIZE, "--lux");
}

/*
 * 更新 WiFi 和 MQTT 连接状态显示
 * 颜色方块（18×18） + ASCII 文字：
 *   绿色 + "OK" = 已连接
 *   红色 + "OFF" = 断开
 */
void lcd_app_update_status(int wifi_ok, int mqtt_ok)
{
    rt_uint16_t dot_x = LCD_STATUS_X;
    rt_uint16_t txt_x = LCD_STATUS_X + 30;

    /* 清除旧状态区域 */
    lcd_fill(dot_x, LCD_WIFI_Y, LCD_STATUS_X + 120, LCD_WIFI_Y + LCD_STATUS_FONT - 1, WHITE);
    lcd_fill(dot_x, LCD_MQTT_Y, LCD_STATUS_X + 120, LCD_MQTT_Y + LCD_STATUS_FONT - 1, WHITE);

    /* WiFi 状态 */
    lcd_fill(dot_x, LCD_WIFI_Y + 3, dot_x + 18, LCD_WIFI_Y + 20, wifi_ok ? GREEN : RED);
    lcd_set_color(WHITE, wifi_ok ? GREEN : RED);
    lcd_show_string(txt_x, LCD_WIFI_Y, LCD_STATUS_FONT, wifi_ok ? "ON " : "OFF");

    /* MQTT 状态 */
    lcd_fill(dot_x, LCD_MQTT_Y + 3, dot_x + 18, LCD_MQTT_Y + 20, mqtt_ok ? GREEN : RED);
    lcd_set_color(WHITE, mqtt_ok ? GREEN : RED);
    lcd_show_string(txt_x, LCD_MQTT_Y, LCD_STATUS_FONT, mqtt_ok ? "ON " : "OFF");

    /* 恢复前景色为黑色 */
    lcd_set_color(WHITE, BLACK);
}

/*
 * 更新 自动/手动 模式显示
 * 清除旧模式文本（Mode:Auto 或 Mode:Manual），显示新的。
 */
void lcd_app_update_mode(int is_auto)
{
    int y = LCD_TITLE_Y + (LCD_FONT_SIZE - LCD_MODE_FONT) / 2;
    lcd_fill(LCD_MODE_X, y, LCD_MODE_X + 150, y + LCD_MODE_FONT - 1, WHITE);
    lcd_show_string(LCD_MODE_X, y, LCD_MODE_FONT,
                    is_auto ? "Mode:Auto  " : "Mode:Manual");
}

/* ==================== 日期时间显示 ==================== */

#define LCD_DATE_X      140     /* 日期 X 起始坐标（WiFi 行右侧） */
#define LCD_TIME_X      140     /* 时间 X 起始坐标（MQTT 行右侧） */

/*
 * 清除日期时间显示区域
 */
static void lcd_app_clear_datetime(void)
{
    lcd_fill(LCD_DATE_X, LCD_WIFI_Y, LCD_DATE_X + 85, LCD_WIFI_Y + LCD_STATUS_FONT - 1, WHITE);
    lcd_set_color(WHITE, GRAY);
    lcd_show_string(LCD_DATE_X, LCD_WIFI_Y, LCD_STATUS_FONT, "--/--/--");

    lcd_fill(LCD_TIME_X, LCD_MQTT_Y, LCD_TIME_X + 65, LCD_MQTT_Y + LCD_STATUS_FONT - 1, WHITE);
    lcd_show_string(LCD_TIME_X, LCD_MQTT_Y, LCD_STATUS_FONT, "--:--");
    lcd_set_color(WHITE, BLACK);
}

/*
 * 更新 LCD 屏幕上的日期时间显示
 * wifi_ok=1 时在 WiFi 行显示日期（YY/MM/DD），MQTT 行显示时间（HH:MM）。
 * wifi_ok=0 时清除日期时间，显示灰色占位符。
 * 系统时间需先通过 NTP 同步，否则显示 00/00/00 00:00。
 */
void lcd_app_update_datetime(int wifi_ok)
{
    if (!wifi_ok)
    {
        lcd_app_clear_datetime();
        return;
    }

    time_t now;
    struct tm *tm_now;

    time(&now);
    tm_now = localtime(&now);

    if (tm_now == RT_NULL)
        return;

    /* 在 WiFi 行右侧显示日期 YY/MM/DD */
    lcd_set_color(WHITE, BLACK);
    lcd_fill(LCD_DATE_X, LCD_WIFI_Y, LCD_DATE_X + 85, LCD_WIFI_Y + LCD_STATUS_FONT - 1, WHITE);
    lcd_show_string(LCD_DATE_X, LCD_WIFI_Y, LCD_STATUS_FONT, "%02d/%02d/%02d",
                    tm_now->tm_year % 100, tm_now->tm_mon + 1, tm_now->tm_mday);

    /* 在 MQTT 行右侧显示时间 HH:MM */
    lcd_fill(LCD_TIME_X, LCD_MQTT_Y, LCD_TIME_X + 65, LCD_MQTT_Y + LCD_STATUS_FONT - 1, WHITE);
    lcd_show_string(LCD_TIME_X, LCD_MQTT_Y, LCD_STATUS_FONT, "%02d:%02d",
                    tm_now->tm_hour, tm_now->tm_min);
}
