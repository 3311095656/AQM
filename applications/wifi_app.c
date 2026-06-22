#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <board.h>
#include <msh.h>

#include <wlan_mgnt.h>
#include <wlan_prot.h>
#include <wlan_cfg.h>
#include <stdio.h>
#include <stdlib.h>

/* ==================== WiFi 热点配置 ==================== */
#define WLAN_SSID       "test_ssid"     /* 要连接的 WiFi 热点名称 */
#define WLAN_PASSWORD   "12345678"      /* WiFi 密码 */

/* ==================== 超时和重试参数 ==================== */
#define WIFI_SCAN_TIMEOUT   (rt_tick_from_millisecond(5 * 1000))     /* 扫描超时：5 秒 */
#define WIFI_CONNECT_TIMEOUT (rt_tick_from_millisecond(10 * 1000))   /* 连接超时：10 秒 */
#define WIFI_CONNECT_RETRY  3                                        /* 最大重试次数：3 次 */

#define DBG_TAG "wifi"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* 网络就绪信号量，WiFi 连接成功并获取 IP 后释放 */
static struct rt_semaphore net_ready;

/* 扫描完成信号量，WiFi 热点扫描完成后释放 */
static struct rt_semaphore scan_done;

/*
 * 打印 WiFi 热点信息
 * 以表格形式输出热点的 SSID、MAC 地址、加密方式、信号强度、信道和速率。
 */
static void print_wlan_information(struct rt_wlan_info *info, int index)
{
    char *security;

    /* 第一条记录时打印表头 */
    if (index == 0)
    {
        rt_kprintf("             SSID                      MAC            security    rssi chn Mbps\n");
        rt_kprintf("------------------------------- -----------------  -------------- ---- --- ----\n");
    }

    /* 打印 SSID */
    rt_kprintf("%-32.32s", &(info->ssid.val[0]));
    /* 打印 MAC 地址 */
    rt_kprintf("%02x:%02x:%02x:%02x:%02x:%02x  ",
            info->bssid[0],
            info->bssid[1],
            info->bssid[2],
            info->bssid[3],
            info->bssid[4],
            info->bssid[5]);

    /* 根据加密类型枚举值转换为可读字符串 */
    switch (info->security)
    {
    case SECURITY_OPEN:           security = "OPEN";           break;
    case SECURITY_WEP_PSK:        security = "WEP_PSK";        break;
    case SECURITY_WEP_SHARED:     security = "WEP_SHARED";     break;
    case SECURITY_WPA_TKIP_PSK:   security = "WPA_TKIP_PSK";   break;
    case SECURITY_WPA_AES_PSK:    security = "WPA_AES_PSK";    break;
    case SECURITY_WPA2_AES_PSK:   security = "WPA2_AES_PSK";   break;
    case SECURITY_WPA2_TKIP_PSK:  security = "WPA2_TKIP_PSK";  break;
    case SECURITY_WPA2_MIXED_PSK: security = "WPA2_MIXED_PSK"; break;
    case SECURITY_WPS_OPEN:       security = "WPS_OPEN";       break;
    case SECURITY_WPS_SECURE:     security = "WPS_SECURE";     break;
    default:                      security = "UNKNOWN";        break;
    }

    /* 打印加密方式、信号强度、信道和速率 */
    rt_kprintf("%-14.14s ", security);
    rt_kprintf("%-4d ", info->rssi);
    rt_kprintf("%3d ", info->channel);
    rt_kprintf("%4d\n", info->datarate / 1000000);
}

/*
 * 扫描报告事件回调
 * 每扫描到一个热点就调用一次，打印该热点的信息。
 */
static void wlan_scan_report_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    struct rt_wlan_info *info = RT_NULL;
    int index = 0;
    RT_ASSERT(event == RT_WLAN_EVT_SCAN_REPORT);
    RT_ASSERT(buff != RT_NULL);
    RT_ASSERT(parameter != RT_NULL);

    info = (struct rt_wlan_info *)buff->data;
    index = *((int *)(parameter));
    print_wlan_information(info, index);
    ++*((int *)(parameter));
}

/*
 * 扫描完成事件回调
 * 当所有热点扫描完毕后触发，释放扫描完成信号量。
 */
static void wlan_scan_done_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    RT_ASSERT(event == RT_WLAN_EVT_SCAN_DONE);
    rt_sem_release(&scan_done);
}

/*
 * 网络就绪事件回调
 * 当 WiFi 连接成功且获取到 IP 地址后触发，释放网络就绪信号量。
 */
static void wlan_ready_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_sem_release(&net_ready);
}

/*
 * WiFi 断开连接事件回调
 * 当与热点断开连接时打印提示信息。
 */
static void wlan_station_disconnect_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    LOG_I("disconnect from the network!");
}

/*
 * WiFi 连接成功事件回调（STA 模式）
 * 打印已连接热点的 SSID。
 */
static void wlan_connect_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_kprintf("%s\n", __FUNCTION__);
    if ((buff != RT_NULL) && (buff->len == sizeof(struct rt_wlan_info)))
    {
        rt_kprintf("ssid : %s \n", ((struct rt_wlan_info *)buff->data)->ssid.val);
    }
}

/*
 * WiFi 连接失败事件回调
 * 打印连接失败热点的 SSID，方便排查问题。
 */
static void wlan_connect_fail_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    rt_kprintf("%s\n", __FUNCTION__);
    if ((buff != RT_NULL) && (buff->len == sizeof(struct rt_wlan_info)))
    {
        rt_kprintf("ssid : %s \n", ((struct rt_wlan_info *)buff->data)->ssid.val);
    }
}

/*
 * 配置 WiFi 自动重连
 * 将 WiFi 设置为 Station（客户端）模式，并开启断线自动重连功能，
 * 同时注册连接成功和连接失败的事件回调。
 */
static int wifi_autoconnect(void)
{
    rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);   /* 设置为 STA 模式 */
    rt_wlan_config_autoreconnect(RT_TRUE);                         /* 开启自动重连 */
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED, wlan_connect_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED_FAIL, wlan_connect_fail_handler, RT_NULL);
    return 0;
}

/*
 * WiFi 模块初始化函数
 * 整体流程：
 *   1. 扫描周围 WiFi 热点并打印列表
 *   2. 使用配置的 SSID 和密码连接热点（失败最多重试 3 次）
 *   3. 等待网络就绪（获取 IP 地址）
 *   4. 配置自动重连功能
 * 成功返回 RT_EOK，失败返回错误码。
 */
int wifi_app_init(void)
{
    static int i = 0;
    int result = RT_EOK;
    struct rt_wlan_info info;

    /* 等待 500ms，让 WiFi 驱动就绪 */
    rt_thread_mdelay(500);

    /* ==================== 第一步：扫描 WiFi 热点 ==================== */
    LOG_D("start to scan ap ...");
    rt_sem_init(&scan_done, "scan_done", 0, RT_IPC_FLAG_FIFO);
    rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_REPORT, wlan_scan_report_handler, &i);
    rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_DONE, wlan_scan_done_handler, RT_NULL);

    if (rt_wlan_scan() == RT_EOK)
    {
        LOG_D("the scan is started... ");
        /* 等待扫描完成，超时则跳过 */
        result = rt_sem_take(&scan_done, WIFI_SCAN_TIMEOUT);
        if (result != RT_EOK)
        {
            LOG_W("scan timeout, skip wifi");
            rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_REPORT);
            rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_DONE);
            rt_sem_detach(&scan_done);
            wifi_autoconnect();
            return -RT_ETIMEOUT;
        }
    }
    else
    {
        LOG_W("scan failed, skip wifi");
        rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_REPORT);
        rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_DONE);
        rt_sem_detach(&scan_done);
        wifi_autoconnect();
        return -RT_ERROR;
    }

    /* 注销扫描相关事件回调，销毁扫描信号量 */
    rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_REPORT);
    rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_DONE);
    rt_sem_detach(&scan_done);

    /* ==================== 第二步：连接到指定热点 ==================== */
    LOG_D("start to connect ap ...");
    rt_sem_init(&net_ready, "net_ready", 0, RT_IPC_FLAG_FIFO);

    /* 注册网络就绪和断开连接的事件回调 */
    rt_wlan_register_event_handler(RT_WLAN_EVT_READY, wlan_ready_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED, wlan_station_disconnect_handler, RT_NULL);

    /* 尝试连接热点，失败后最多重试 WIFI_CONNECT_RETRY 次 */
    for (int retry = 0; retry < WIFI_CONNECT_RETRY; retry++)
    {
        if (retry > 0)
        {
            LOG_D("retry connect (%d/%d)...", retry + 1, WIFI_CONNECT_RETRY);
            rt_thread_mdelay(2000);  /* 重试前等待 2 秒 */
        }
        result = rt_wlan_connect(WLAN_SSID, WLAN_PASSWORD);
        if (result == RT_EOK)
        {
            break;
        }
        LOG_W("connect attempt %d failed", retry + 1);
    }

    if (result == RT_EOK)
    {
        /* 获取并打印连接的热点信息 */
        rt_memset(&info, 0, sizeof(struct rt_wlan_info));
        rt_wlan_get_info(&info);
        LOG_D("station information:");
        print_wlan_information(&info, 0);

        /* ==================== 第三步：等待网络就绪 ==================== */
        result = rt_sem_take(&net_ready, WIFI_CONNECT_TIMEOUT);
        if (result == RT_EOK)
        {
            LOG_I("networking ready!");
            /* 执行 ifconfig 命令，打印网络接口信息 */
            msh_exec("ifconfig", rt_strlen("ifconfig"));

            /* 清理就绪信号量相关资源 */
            rt_wlan_unregister_event_handler(RT_WLAN_EVT_READY);
            rt_sem_detach(&net_ready);

            /* 配置自动重连 */
            wifi_autoconnect();
            return RT_EOK;
        }
        else
        {
            LOG_W("connect timeout, skip wifi");
        }
    }
    else
    {
        LOG_W("connect to %s failed, skip wifi", WLAN_SSID);
    }

    /* 连接失败时清理资源 */
    rt_wlan_unregister_event_handler(RT_WLAN_EVT_READY);
    rt_sem_detach(&net_ready);
    rt_wlan_disconnect();

    /* 即使连接失败也配置自动重连，后续可能恢复 */
    wifi_autoconnect();

    return -RT_ERROR;
}
