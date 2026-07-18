#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "mqtt_app.h"
#include "mq5_app.h"
#include "servo_app.h"
#include "fan_app.h"
#include "ac_app.h"
#include "humidifier_app.h"
#include "schedule_app.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "paho_mqtt.h"
#include "cJSON.h"

#define DBG_TAG "mqtt"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* ==================== OneNET 平台连接参数 ==================== */
#define ONENET_MQTT_URI     "tcp://mqtts.heclouds.com:1883"   /* OneNET MQTT 服务器地址 */
#define ONENET_PRODUCT_ID   "8Fph2Xgid0"                      /* 产品 ID */
#define ONENET_DEVICE_ID    "AQMV2"                            /* 设备名称 */
#define ONENET_USERNAME     ONENET_PRODUCT_ID                  /* 用户名即产品 ID */
#define ONENET_CLIENT_ID    ONENET_DEVICE_ID                   /* 客户端 ID 即设备名称 */
#define ONENET_PASSWORD     "version=2018-10-31&res=products%2F8Fph2Xgid0%2Fdevices%2FAQMV2&et=1812214000&method=md5&sign=qS7KXVoPlPwMgEn7Ijh%2F6A%3D%3D"
                                                                /* Token 鉴权密码 */

/* ==================== MQTT 主题定义 ==================== */
#define ONENET_PUB_TOPIC    "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/post"
                                                                /* 发布主题：属性上报 */
#define ONENET_SUB_TOPIC    "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/post/reply"
                                                                /* 订阅主题：属性上报回复 */
#define ONENET_SET_TOPIC    "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/set"
                                                                /* 订阅主题：属性下发（远程控制） */
#define ONENET_SET_REPLY_TOPIC "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/set_reply"
                                                                /* 发布主题：属性下发回复 */

/* MQTT 客户端实例 */
static MQTTClient client;

/* MQTT 是否已启动的标志位 */
static int is_started = 0;

/* 远程手动控制标志（云端下发后置1，屏蔽本地自动逻辑） */
static int fan_manual = 1;
static int window_manual = 1;

static void mq_publish(const char *topic, const char *send_str);

/*
 * 从 cJSON 节点中提取布尔值
 * 兼容两种格式：裸值 "key":true  和  对象 "key":{"value":true}
 */
static int get_bool_val(cJSON *item, int def)
{
    cJSON *v = cJSON_GetObjectItem(item, "value");
    if (v) item = v;
    if (item->type == cJSON_True)  return 1;
    if (item->type == cJSON_False) return 0;
    return item->valueint ? 1 : def;
}

/*
 * 默认订阅消息回调函数
 * 当收到 OneNET 平台回复消息时（如属性上报的回复），会调用此函数。
 * 将收到的消息内容以日志形式打印到串口。
 */
static void mqtt_sub_default_callback(MQTTClient *c, MessageData *msg_data)
{
    size_t len = msg_data->message->payloadlen;
    if (len > 511) len = 511;  /* 安全截断 */
    char buf[512];
    memcpy(buf, msg_data->message->payload, len);
    buf[len] = '\0';
    LOG_D("sub callback: %.*s %s",
          msg_data->topicName->lenstring.len,
          msg_data->topicName->lenstring.data,
          buf);
}

/*
 * 属性下发回调函数
 * OneNET 平台通过 $sys/.../thing/property/set 主题下发属性设置命令。
 * 解析 JSON 中的 fan_state / window_state，执行对应操作，并回复结果。
 */
static void mqtt_sub_set_callback(MQTTClient *c, MessageData *msg_data)
{
    size_t len = msg_data->message->payloadlen;
    char payload_buf[768];  /* 足够容纳四段定时计划 JSON */
    if (len > sizeof(payload_buf) - 1)
    {
        LOG_E("set cmd: payload too large (%u > %u)", len, sizeof(payload_buf) - 1);
        mq_publish(ONENET_SET_REPLY_TOPIC,
            "{\"id\":\"0\",\"code\":413,\"msg\":\"payload too large\"}");
        return;
    }
    memcpy(payload_buf, msg_data->message->payload, len);
    payload_buf[len] = '\0';
    const char *payload = payload_buf;
    LOG_I("set cmd: %s", payload);

    cJSON *root = cJSON_Parse(payload);
    if (!root)
    {
        LOG_E("set cmd: JSON parse failed");
        mq_publish(ONENET_SET_REPLY_TOPIC,
            "{\"id\":\"0\",\"code\":400,\"msg\":\"invalid JSON\"}");
        return;
    }

    char reply[256];
    const char *msg_id = "0";
    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    if (id_item && id_item->valuestring)
        msg_id = id_item->valuestring;

    /* 收到云端命令，蜂鸣器短响一声提示（50ms，一次性硬件延时，可接受） */
    rt_pin_write(GET_PIN(B, 0), PIN_HIGH);
    rt_hw_us_delay(50000);
    rt_pin_write(GET_PIN(B, 0), PIN_LOW);

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (!params || !cJSON_IsObject(params))
    {
        LOG_W("set cmd: missing or invalid params");
        rt_snprintf(reply, sizeof(reply),
            "{\"id\":\"%s\",\"code\":400,\"msg\":\"missing or invalid params\"}", msg_id);
        mq_publish(ONENET_SET_REPLY_TOPIC, reply);
        cJSON_Delete(root);
        return;
    }

    /* ===== 安全仲裁：燃气报警时拒绝关闭安全设备 ===== */
    int gas_alarm = mq5_app_is_alarm();

    cJSON *fan = cJSON_GetObjectItem(params, "fan_state");
    if (fan)
        {
            int on = get_bool_val(fan, 0);
            if (!on && gas_alarm)
            {
                LOG_W("Remote: Fan OFF rejected (gas alarm active)");
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":403,\"msg\":\"safety: cannot close fan during gas alarm\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            if (on) { fan_on();  LOG_I("Remote: Fan ON"); }
            else    { fan_off(); LOG_I("Remote: Fan OFF"); }
            fan_manual = 1;
        }

        cJSON *window = cJSON_GetObjectItem(params, "window_state");
        if (window)
        {
            int open = get_bool_val(window, 0);
            if (!open && gas_alarm)
            {
                LOG_W("Remote: Window CLOSE rejected (gas alarm active)");
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":403,\"msg\":\"safety: cannot close window during gas alarm\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            if (open) { servo_window_open();  LOG_I("Remote: Window OPEN"); }
            else      { servo_window_close(); LOG_I("Remote: Window CLOSE"); }
            window_manual = 1;
        }

        cJSON *buzzer = cJSON_GetObjectItem(params, "buzzer_state");
        if (buzzer)
        {
            int on = get_bool_val(buzzer, 0);
            if (!on && gas_alarm)
            {
                LOG_W("Remote: Buzzer OFF rejected (gas alarm active)");
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":403,\"msg\":\"safety: cannot silence buzzer during gas alarm\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            if (on) { buzzer_on();  LOG_I("Remote: Buzzer ON"); }
            else    { buzzer_off(); LOG_I("Remote: Buzzer OFF"); }
        }

        cJSON *auto_mode = cJSON_GetObjectItem(params, "auto_mode");
        if (auto_mode)
        {
            int restore = get_bool_val(auto_mode, 0);
            if (restore && gas_alarm)
            {
                LOG_W("Remote: Auto mode rejected (gas alarm active)");
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":403,\"msg\":\"safety: cannot enable auto during gas alarm\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            if (restore)
            {
                fan_manual = 0;
                window_manual = 0;
                buzzer_set_manual(0);
                ac_set_manual(0);
                ac_set_mode(AC_AUTO);   /* 切自动时 AC 设为自动模式，LED 亮绿灯 */
                humi_set_manual(0);
                LOG_I("Remote: Auto mode restored");
            }
            else
            {
                fan_manual = 1;
                window_manual = 1;
                buzzer_set_manual(1);
                ac_set_manual(1);
                humi_set_manual(1);
                LOG_I("Remote: Manual mode, auto control disabled");
            }
        }

        /* 空调远程控制：ac_mode = 0/1/2/3（关/制冷/制热/自动） */
        cJSON *ac_mode = cJSON_GetObjectItem(params, "ac_mode");
        if (ac_mode)
        {
            if (!cJSON_IsNumber(ac_mode))
            {
                LOG_W("Remote: AC mode not a number");
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":400,\"msg\":\"ac_mode must be a number\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            int mode = ac_mode->valueint;
            if (mode < 0 || mode > 3)
            {
                LOG_W("Remote: AC invalid mode=%d", mode);
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":400,\"msg\":\"invalid ac_mode: %d\"}", msg_id, mode);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            if (mode != 0 && gas_alarm)
            {
                LOG_W("Remote: AC mode=%d rejected (gas alarm active)", mode);
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":403,\"msg\":\"safety: cannot enable AC during gas alarm\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            ac_set_mode((ac_mode_t)mode);
            if (mode == 3)  /* AC_AUTO: AC 独立自动，不影响操作模式 */
                ac_set_manual(0);
            else
                ac_set_manual(1);
            const char *mode_str[] = {"OFF", "COOL", "HEAT", "AUTO"};
            LOG_I("Remote: AC mode=%s", mode_str[mode]);
        }

        /* 加湿/除湿远程控制：humi_mode = 0/1/2/3（关/加湿/除湿/自动） */
        cJSON *humi_mode = cJSON_GetObjectItem(params, "humi_mode");
        if (humi_mode)
        {
            if (!cJSON_IsNumber(humi_mode))
            {
                LOG_W("Remote: Humi mode not a number");
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":400,\"msg\":\"humi_mode must be a number\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            int mode = humi_mode->valueint;
            if (mode < 0 || mode > 3)
            {
                LOG_W("Remote: Humi invalid mode=%d", mode);
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":400,\"msg\":\"invalid humi_mode: %d\"}", msg_id, mode);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            if (mode != 0 && gas_alarm)
            {
                LOG_W("Remote: Humi mode=%d rejected (gas alarm active)", mode);
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":403,\"msg\":\"safety: cannot enable humidifier during gas alarm\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            humi_set_mode((humi_mode_t)mode);
            if (mode == HUMI_AUTO)
                humi_set_manual(0);
            else
                humi_set_manual(1);
            const char *mode_str[] = {"OFF", "HUMIDIFY", "DEHUMIDIFY", "AUTO"};
            LOG_I("Remote: Humi mode=%s", mode_str[mode]);
        }

        /* 目标温度设置：target_temp = float（16.0~40.0，步长 0.5） */
        cJSON *tt = cJSON_GetObjectItem(params, "target_temp");
        if (tt)
        {
            if (!cJSON_IsNumber(tt))
            {
                LOG_W("Remote: target_temp not a number");
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":400,\"msg\":\"target_temp must be a number\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            float temp = (float)tt->valuedouble;
            float set = ac_set_target_temp(temp);
            LOG_I("Remote: Target temp=%d.%d C", (int)set, 
                  (int)((set - (int)set) * 10 + 0.5f) % 10);
        }

        /* 目标湿度设置：target_humi = float（20~90，步长 5） */
        cJSON *th = cJSON_GetObjectItem(params, "target_humi");
        if (th)
        {
            if (!cJSON_IsNumber(th))
            {
                LOG_W("Remote: target_humi not a number");
                rt_snprintf(reply, sizeof(reply),
                    "{\"id\":\"%s\",\"code\":400,\"msg\":\"target_humi must be a number\"}", msg_id);
                mq_publish(ONENET_SET_REPLY_TOPIC, reply);
                cJSON_Delete(root);
                return;
            }
            float humi = (float)th->valuedouble;
            float set = humi_set_target_humi(humi);
            LOG_I("Remote: Target humi=%d%%", (int)set);
        }

        /* 定时计划设置（兼容 JSON对象 和 JSON字符串 两种格式） */
        cJSON *schedule = cJSON_GetObjectItem(params, "schedule");
        if (schedule)
        {
            cJSON *schedule_obj = schedule;
            int need_free = 0;

            /* 如果是 string 类型，先解析为 JSON 对象 */
            if (cJSON_IsString(schedule) && schedule->valuestring)
            {
                schedule_obj = cJSON_Parse(schedule->valuestring);
                need_free = 1;
            }

            if (schedule_obj && cJSON_IsObject(schedule_obj))
            {
                cJSON *enable  = cJSON_GetObjectItem(schedule_obj, "enable");
                cJSON *periods = cJSON_GetObjectItem(schedule_obj, "periods");
                schedule_set_config(enable ? enable->valueint : 0, periods);
                LOG_I("Remote: Schedule updated");
            }
            else
            {
                LOG_W("Schedule parse failed");
            }

            if (need_free && schedule_obj)
                cJSON_Delete(schedule_obj);
        }

    rt_snprintf(reply, sizeof(reply),
        "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}", msg_id);
    mq_publish(ONENET_SET_REPLY_TOPIC, reply);

    cJSON_Delete(root);
}

/*
 * MQTT 连接事件回调函数
 * 当 MQTT 客户端开始尝试连接服务器时触发。
 */
static void mqtt_connect_callback(MQTTClient *c)
{
    LOG_I("Start to connect mqtt server");
}

/*
 * MQTT 上线回调函数
 * 当 MQTT 客户端成功连接到 OneNET 服务器后触发。
 */
static void mqtt_online_callback(MQTTClient *c)
{
    LOG_I("MQTT connected, sensor data will be published");
}

/*
 * MQTT 离线回调函数
 * 当 MQTT 与 OneNET 服务器断开连接时触发，由 paho_mqtt 库自动重连。
 */
static void mqtt_offline_callback(MQTTClient *c)
{
    LOG_I("Disconnect from mqtt server, will auto reconnect...");
}

/*
 * MQTT 发布消息内部函数
 * 将指定字符串作为 MQTT 消息发布到指定主题。
 *   topic    - 目标主题
 *   send_str - 要发布的字符串内容
 */
static void mq_publish(const char *topic, const char *send_str)
{
    MQTTMessage message;
    message.qos = QOS1;                /* QOS1：至少一次，有 ACK 确认 */
    message.retained = 0;              /* 不保留消息 */
    message.payload = (void *)send_str;
    message.payloadlen = strlen(send_str);

    /* 发送失败自动重试最多 3 次，间隔 200ms，提升网络抖动时的数据可靠性 */
    int retry;
    for (retry = 0; retry < 3; retry++)
    {
        if (MQTTPublish(&client, topic, &message) == 0)
            return;
        if (retry < 2)
        {
            LOG_W("publish failed, retry %d/3", retry + 1);
            rt_thread_mdelay(200);
        }
    }
    LOG_E("publish failed after 3 retries, topic=%s", topic);
}

/*
 * MQTT 模块初始化函数
 * 配置 OneNET 平台的连接参数（客户端ID、用户名、密码等），
 * 设置回调函数，注册订阅主题，配置自动重连间隔（5秒），
 * 然后启动 MQTT 客户端线程。
 * 成功返回 RT_EOK，已初始化过返回 -RT_ERROR，内存不足返回 -RT_ENOMEM。
 */
int mqtt_app_init(void)
{
    MQTTPacket_connectData condata = MQTTPacket_connectData_initializer;

    /* 防止重复初始化 */
    if (is_started)
    {
        return -RT_ERROR;
    }

    client.isconnected = 0;
    client.uri = ONENET_MQTT_URI;

    /* 填充 MQTT 连接参数 */
    memcpy(&client.condata, &condata, sizeof(condata));
    client.condata.clientID.cstring = ONENET_CLIENT_ID;     /* 客户端 ID */
    client.condata.keepAliveInterval = 120;                  /* 心跳间隔 120 秒 */
    client.condata.cleansession = 1;                         /* 清除会话 */
    client.condata.username.cstring = ONENET_USERNAME;       /* 用户名 */
    client.condata.password.cstring = ONENET_PASSWORD;       /* Token 密码 */

    client.condata.willFlag = 0;                             /* 不使用遗嘱消息 */

    /* 分配发送和接收缓冲区（各 2KB） */
    client.buf_size = client.readbuf_size = 1024 * 2;
    client.buf = malloc(client.buf_size);
    client.readbuf = malloc(client.readbuf_size);
    if (!(client.buf && client.readbuf))
    {
        LOG_E("no memory for MQTT client buffer!");
        return -RT_ENOMEM;
    }

    /* 注册连接/上线/离线事件回调 */
    client.connect_callback = mqtt_connect_callback;
    client.online_callback = mqtt_online_callback;
    client.offline_callback = mqtt_offline_callback;

    /* 注册 subscribe 主题（属性上报回复） */
    client.messageHandlers[0].topicFilter = ONENET_SUB_TOPIC;
    client.messageHandlers[0].callback = mqtt_sub_default_callback;
    client.messageHandlers[0].qos = QOS1;

    /* 注册 set 主题（属性下发远程控制） */
    client.messageHandlers[1].topicFilter = ONENET_SET_TOPIC;
    client.messageHandlers[1].callback = mqtt_sub_set_callback;
    client.messageHandlers[1].qos = QOS1;

    /* 设置默认消息处理回调 */
    client.defaultMessageHandler = mqtt_sub_default_callback;

    /* 设置自动重连间隔为 5 秒 */
    int reconnect_interval = 5000;
    paho_mqtt_control(&client, MQTT_CTRL_SET_RECONN_INTERVAL, &reconnect_interval);

    LOG_I("Connect OneNET: %s", ONENET_MQTT_URI);
    LOG_I("Client ID: %s", ONENET_CLIENT_ID);

    /* 启动 MQTT 客户端（内部会创建独立线程处理连接和收发） */
    paho_mqtt_start(&client);
    is_started = 1;

    return RT_EOK;
}

/*
 * 将传感器数据上报到 OneNET 平台
 * 上报字段：室外温湿度、光照强度、室内温湿度、燃气报警、窗户状态、人体存在。
 * 未连接时直接返回 -RT_ERROR。
 */
int mqtt_app_publish_sensor(const sensor_data_t *data)
{
    if (!client.isconnected)
    {
        return -RT_ERROR;
    }
    if (!data->valid)
    {
        LOG_D("MQTT: skip publish (sensor data invalid)");
        return -RT_ERROR;  /* 不上报无效数据 */
    }

    static char json_buf[1024];

    int out_t_int = (int)data->out_temperature;
    int out_t_dec = abs((int)(data->out_temperature * 10) % 10);
    int out_h_int = (int)data->out_humidity;
    int out_h_dec = abs((int)(data->out_humidity * 10) % 10);
    int bri_int   = (int)data->brightness;
    int bri_dec   = abs((int)(data->brightness * 10) % 10);
    int in_t_int  = (int)data->in_temperature;
    int in_t_dec  = abs((int)(data->in_temperature * 10) % 10);
    int in_h_int  = (int)data->in_humidity;
    int in_h_dec  = abs((int)(data->in_humidity * 10) % 10);
    int gas_alarm  = mq5_app_is_alarm();
    int window_st  = servo_window_is_open();
    int window_angle_val = servo_window_get_angle();
    int presence   = data->presence;
    int is_fan_on  = fan_is_on();
    int is_buzz_on = buzzer_is_on();
    int is_auto    = !(fan_manual || window_manual || buzzer_manual_mode() || ac_manual_mode() || humi_manual_mode());
    int ac_mode_val = (int)ac_get_mode();
    int humi_mode_val = (int)humi_get_mode();

    /* target_temp 拆成整数+小数（rt_snprintf 不支持 %f） */
    float tt = ac_get_target_temp();
    int tt_int = (int)tt;
    int tt_dec = (int)((tt - tt_int) * 10 + 0.5f);
    if (tt_dec >= 10) { tt_int++; tt_dec = 0; }

    /* target_humi 拆成整数+小数 */
    float th = humi_get_target_humi();
    int th_int = (int)th;
    int th_dec = (int)((th - th_int) * 10 + 0.5f);
    if (th_dec >= 10) { th_int++; th_dec = 0; }

    rt_snprintf(json_buf, sizeof(json_buf),
        "{\"id\":\"1\",\"version\":\"1.0\",\"params\":{"
        "\"out_temperature\":{\"value\":%d.%d},"
        "\"out_humidity\":{\"value\":%d.%d},"
        "\"brightness\":{\"value\":%d.%d},"
        "\"in_temperature\":{\"value\":%d.%d},"
        "\"in_humidity\":{\"value\":%d.%d},"
        "\"gas_alarm\":{\"value\":%s},"
        "\"window_state\":{\"value\":%s},"
        "\"window_angle\":{\"value\":%d},"
        "\"fan_state\":{\"value\":%s},"
        "\"buzzer_state\":{\"value\":%s},"
        "\"presence\":{\"value\":%s},"
        "\"auto_mode\":{\"value\":%s},"
        "\"ac_mode\":{\"value\":%d},"
        "\"humi_mode\":{\"value\":%d},"
        "\"target_temp\":{\"value\":%d.%d},"
        "\"target_humi\":{\"value\":%d.%d}"
        "}}",
        out_t_int, out_t_dec,
        out_h_int, out_h_dec,
        bri_int,   bri_dec,
        in_t_int,  in_t_dec,
        in_h_int,  in_h_dec,
        gas_alarm  ? "true" : "false",
        window_st  ? "true" : "false",
        window_angle_val,
        is_fan_on  ? "true" : "false",
        is_buzz_on ? "true" : "false",
        presence   ? "true" : "false",
        is_auto    ? "true" : "false",
        ac_mode_val,
        humi_mode_val,
        tt_int, tt_dec,
        th_int, th_dec);

    mq_publish(ONENET_PUB_TOPIC, json_buf);
    LOG_D("publish: %s", json_buf);

    return RT_EOK;
}

/*
 * 查询 MQTT 连接状态
 * 返回非零值表示已连接到 OneNET 平台。
 */
int mqtt_app_is_connected(void)
{
    return client.isconnected;
}

/*
 * 查询风扇是否处于远程手动模式
 */
int mqtt_app_fan_manual(void)
{
    return fan_manual;
}

/*
 * 查询窗户是否处于远程手动模式
 */
int mqtt_app_window_manual(void)
{
    return window_manual;
}

/*
 * KEY2 切换手动/自动模式
 * 翻转所有设备的手动标志。
 * 返回：1=手动模式，0=自动模式
 */
int mqtt_app_toggle_auto_mode(void)
{
    if (fan_manual || window_manual || buzzer_manual_mode() || ac_manual_mode() || humi_manual_mode())
    {
        fan_manual = 0;
        window_manual = 0;
        buzzer_set_manual(0);
        ac_set_manual(0);
        ac_set_mode(AC_AUTO);
        humi_set_manual(0);
        LOG_I("KEY2: Auto mode restored");
        return 0;
    }
    else
    {
        fan_manual = 1;
        window_manual = 1;
        buzzer_set_manual(1);
        ac_set_manual(1);
        humi_set_manual(1);
        LOG_I("KEY2: Manual mode, auto control disabled");
        return 1;
    }
}

/*
 * 查询当前是否为自动模式（供定时计划和 main.c 使用）
 * 返回 1=自动模式，0=手动模式
 */
int mqtt_app_auto_mode(void)
{
    return !(fan_manual || window_manual || buzzer_manual_mode() || ac_manual_mode() || humi_manual_mode());
}

/*
 * 定时计划远程切换自动/手动模式
 * restore: 1=切自动，0=切手动
 */
void mqtt_set_auto_mode_remote(int restore)
{
    if (restore)
    {
        fan_manual = 0;
        window_manual = 0;
        buzzer_set_manual(0);
        ac_set_manual(0);
        ac_set_mode(AC_AUTO);
        humi_set_manual(0);
    }
    else
    {
        fan_manual = 1;
        window_manual = 1;
        buzzer_set_manual(1);
        ac_set_manual(1);
        humi_set_manual(1);
    }
}

/*
 * MSH 命令：手动向 OneNET 发布 MQTT 消息
 * 用法：msh_mq_publish {"id":"1","version":"1.0","params":{"temperature":{"value":25.5}}}
 * 用于调试时手动发送数据到 OneNET 平台。
 */
static void msh_mq_publish(int argc, char *argv[])
{
    char send_buff[256] = {'\0'};
    for (int i = 1; i < argc; i++)
    {
        if (i > 1)
        {
            strcat(send_buff, " ");
        }
        strcat(send_buff, argv[i]);
    }
    mq_publish(ONENET_PUB_TOPIC, send_buff);
}
MSH_CMD_EXPORT(msh_mq_publish, publish message to OneNET by msh);
