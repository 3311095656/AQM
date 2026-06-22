#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "ws2812b_app.h"

#define DBG_TAG "ws2812b"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* ==================== WS2812B RGB LED 驱动 ==================== */

/*
 * 硬件连接：WS2812B 数据线接 PG6（40Pin 排针上空闲 GPIO）
 * 供电：3.3V~5V，GND 共地
 *
 * WS2812B 通信协议（单线，800kHz）：
 *   Bit 1：HIGH 700ns ±150ns，LOW 600ns ±150ns
 *   Bit 0：HIGH 350ns ±150ns，LOW 800ns ±150ns
 *   Reset：LOW > 50µs
 *   数据顺序：GRB（先发绿色，再发红色，再发蓝色），MSB first
 *
 * STM32F407 主频 168MHz，直接操作 GPIO BSRR 寄存器
 * 比 rt_pin_write() 快 10 倍以上，时序更精确
 */

#define WS2812B_PIN         GET_PIN(G, 6)   /* PG6 数据引脚 */

/* PG6 直接寄存器操作宏 */
#define WS_HIGH()   (GPIOG->BSRR = GPIO_PIN_6)
#define WS_LOW()    (GPIOG->BSRR = (GPIO_PIN_6 << 16))

/* 颜色缓冲区，GRB 顺序 */
static rt_uint8_t ws_buf[WS2812B_LED_NUM][3];   /* [led][G/R/B] */

/*
 * 精确延时宏（内联汇编，避免编译器优化）
 * STM32F407 @168MHz：1 NOP ≈ 5.95ns
 */
#define NOP1()  __asm__ __volatile__("nop")
#define NOP10() do { NOP1();NOP1();NOP1();NOP1();NOP1();NOP1();NOP1();NOP1();NOP1();NOP1(); } while(0)
#define NOP20() do { NOP10();NOP10(); } while(0)
#define NOP50() do { NOP20();NOP20();NOP10(); } while(0)

/*
 * 发送 1 bit 数据（直接寄存器操作，关中断保证时序）
 */
static void ws2812b_send_bit(rt_uint8_t bit)
{
    if (bit)
    {
        /* Bit 1：HIGH ~700ns (≈118 NOP)，LOW ~600ns (≈100 NOP) */
        WS_HIGH();
        NOP50(); NOP50(); NOP10(); NOP1(); NOP1(); NOP1(); NOP1(); NOP1(); NOP1(); NOP1(); NOP1();  /* 118 NOP */
        WS_LOW();
        NOP50(); NOP50();  /* 100 NOP */
    }
    else
    {
        /* Bit 0：HIGH ~350ns (≈59 NOP)，LOW ~800ns (≈134 NOP) */
        WS_HIGH();
        NOP50(); NOP1(); NOP1(); NOP1(); NOP1(); NOP1(); NOP1(); NOP1(); NOP1(); NOP1();  /* 59 NOP */
        WS_LOW();
        NOP50(); NOP50(); NOP20(); NOP10();  /* 130 NOP */
    }
}

/*
 * 发送 1 个字节（MSB first）
 */
static void ws2812b_send_byte(rt_uint8_t data)
{
    int i;
    for (i = 7; i >= 0; i--)
    {
        ws2812b_send_bit((data >> i) & 0x01);
    }
}

/*
 * 将缓冲区数据发送到 WS2812B（关中断保证时序精确）
 */
static void ws2812b_refresh(void)
{
    rt_base_t level;
    int i;

    level = rt_hw_interrupt_disable();   /* 关中断 */

    for (i = 0; i < WS2812B_LED_NUM; i++)
    {
        ws2812b_send_byte(ws_buf[i][0]); /* G */
        ws2812b_send_byte(ws_buf[i][1]); /* R */
        ws2812b_send_byte(ws_buf[i][2]); /* B */
    }

    rt_hw_interrupt_enable(level);       /* 开中断 */

    /* Reset：拉低 > 50µs */
    WS_LOW();
    rt_hw_us_delay(60);
}

/*
 * 初始化 WS2812B
 * 配置 PG6 为推挽输出，全部 LED 熄灭
 */
int ws2812b_app_init(void)
{
    rt_pin_mode(WS2812B_PIN, PIN_MODE_OUTPUT);
    WS_LOW();

    /* 清零缓冲区 */
    rt_memset(ws_buf, 0, sizeof(ws_buf));
    ws2812b_refresh();

    LOG_I("WS2812B init OK, %d LEDs on PG6", WS2812B_LED_NUM);
    return RT_EOK;
}

/*
 * 设置某颗 LED 的颜色
 * led: LED 编号
 * r, g, b: 红绿蓝分量（0~255）
 */
void ws2812b_set_color(rt_uint8_t led, rt_uint8_t r, rt_uint8_t g, rt_uint8_t b)
{
    if (led >= WS2812B_LED_NUM)
        return;

    ws_buf[led][0] = g;  /* WS2812B 数据顺序：GRB */
    ws_buf[led][1] = r;
    ws_buf[led][2] = b;

    ws2812b_refresh();
}

/*
 * 熄灭某颗 LED
 */
void ws2812b_set_off(rt_uint8_t led)
{
    ws2812b_set_color(led, COLOR_OFF);
}

/*
 * 全部 LED 熄灭
 */
void ws2812b_all_off(void)
{
    rt_memset(ws_buf, 0, sizeof(ws_buf));
    ws2812b_refresh();
}
