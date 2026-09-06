/*
 * ntp_app.c - NTP 时间同步模块
 * 联网后通过 NTP 协议获取网络时间，更新系统时钟，
 * 供 LCD 显示日期和时间。
 *
 *  Date            Author          Notes
 *  2026-6-07      Trae AI         add NTP time sync
 */

#include <rtthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#define DBG_TAG "ntp"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* NTP 服务器地址（北京时间服务器） */
#define NTP_SERVER      "ntp.aliyun.com"
#define NTP_PORT        123
#define NTP_EPOCH_DIFF  2208988800UL   /* 1900-01-01 到 1970-01-01 的秒数 */
#define NTP_RECV_TIMEOUT 5             /* 接收超时（秒） */

/*
 * NTP 时间同步函数
 * 1. 创建 UDP socket 并连接 NTP 服务器
 * 2. 发送 NTP 请求（版本3，客户端模式）
 * 3. 接收响应，提取发送时间戳
 * 4. 转换为 Unix 时间戳，更新系统时钟
 *
 * 返回 0 表示成功，-1 表示失败。
 */
int ntp_app_sync(void)
{
    struct hostent *host;
    struct sockaddr_in server_addr;
    int sock;
    unsigned char ntp_buf[48];
    time_t server_time;
    struct timeval tv;
    struct timeval recv_timeout;

    LOG_I("NTP sync start: %s", NTP_SERVER);

    /* DNS 解析 NTP 服务器地址 */
    host = gethostbyname(NTP_SERVER);
    if (!host)
    {
        LOG_E("DNS resolve failed");
        return -1;
    }

    /* 创建 UDP socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        LOG_E("Socket create failed");
        return -1;
    }

    /* 设置接收超时（避免永久阻塞） */
    recv_timeout.tv_sec = NTP_RECV_TIMEOUT;
    recv_timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               &recv_timeout, sizeof(recv_timeout));

    /* 配置服务器地址 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(NTP_PORT);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    /* 构造 NTP 请求包：版本3 + 客户端模式 = 0x1B */
    memset(ntp_buf, 0, sizeof(ntp_buf));
    ntp_buf[0] = 0x1B;

    /* 发送请求 */
    if (sendto(sock, ntp_buf, sizeof(ntp_buf), 0,
               (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        LOG_E("NTP send failed");
        closesocket(sock);
        return -1;
    }

    /* 接收响应 */
    socklen_t addr_len = sizeof(server_addr);
    int recv_len = recvfrom(sock, ntp_buf, sizeof(ntp_buf), 0,
                            (struct sockaddr *)&server_addr, &addr_len);
    closesocket(sock);

    if (recv_len < 48)
    {
        LOG_E("NTP recv failed (%d bytes)", recv_len);
        return -1;
    }

    /* 提取 NTP 发送时间戳（字节 40-43 为整数秒，44-47 为小数秒） */
    server_time = (time_t)((ntohl(*(uint32_t *)&ntp_buf[40]) - NTP_EPOCH_DIFF));

    /* 更新系统时钟 */
    tv.tv_sec = server_time;
    tv.tv_usec = 0;
    settimeofday(&tv, RT_NULL);

    /* 打印同步结果 */
    {
        struct tm *tm_now = localtime(&server_time);
        LOG_I("NTP sync OK: %04d-%02d-%02d %02d:%02d:%02d",
              tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
              tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
    }

    return 0;
}
