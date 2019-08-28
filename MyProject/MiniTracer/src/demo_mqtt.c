/*
 * @Description: In User Settings Edit
 * @Author: your name
 * @Date: 2019-08-03 19:57:03
 * @LastEditTime: 2019-08-27 18:08:30
 * @LastEditors: Please set LastEditors
 */
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"

#include "api_os.h"
#include "api_debug.h"
#include "api_event.h"
#include "api_mqtt.h"
#include "api_network.h"
#include "api_socket.h"
#include "api_info.h"
#include "demo_mqtt.h"

#include "buffer.h"
#include "gps_parse.h"
#include "math.h"
#include "gps.h"
#include "cJSON.h"

#define PDP_CONTEXT_APN "cmnet"
#define PDP_CONTEXT_USERNAME ""
#define PDP_CONTEXT_PASSWD ""

#define MAIN_TASK_STACK_SIZE (2048 * 2)
#define MAIN_TASK_PRIORITY 0
#define MAIN_TASK_NAME "Main Task"

#define SECOND_TASK_STACK_SIZE (2048 * 2)
#define SECOND_TASK_PRIORITY 1
#define SECOND_TASK_NAME "MQTT Task"

#define GPS_TASK_STACK_SIZE (2048 * 2)
#define GPS_TASK_PRIORITY 0
#define GPS_TASK_NAME "GPS Task"

bool flagIsGPSFinish = false;
uint8_t imei[16], subTopic[50], pubTopic[50];
char getLatitude[15], getLongitude[15];
uint8_t postDate[150];

static HANDLE mainTaskHandle = NULL;
static HANDLE mqttTaskHandle = NULL;
static HANDLE grsTaskHandle = NULL;

static HANDLE semMqttStart = NULL;
MQTT_Connect_Info_t ci;

static MQTT_Client_t *mqttClient;

typedef enum
{
    MQTT_EVENT_CONNECTED = 0,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_MAX
} MQTT_Event_ID_t;

typedef struct
{
    MQTT_Event_ID_t id;
    MQTT_Client_t *client;
} MQTT_Event_t;

typedef enum
{
    MQTT_STATUS_DISCONNECTED = 0,
    MQTT_STATUS_CONNECTED,
    MQTT_STATUS_MAX
} MQTT_Status_t;

MQTT_Status_t mqttStatus = MQTT_STATUS_DISCONNECTED;

static uint32_t reconnectInterval = 3000;
void StartTimerPublish(uint32_t interval, MQTT_Client_t *client);
void StartTimerConnect(uint32_t interval, MQTT_Client_t *client);
void OnPublish(void *arg, MQTT_Error_t err);

bool AttachActivate()
{
    uint8_t status;
    bool ret = Network_GetAttachStatus(&status);
    if (!ret)
    {
        Trace(2, "get attach staus fail");
        return false;
    }
    Trace(2, "attach status:%d", status);
    if (!status)
    {
        ret = Network_StartAttach();
        if (!ret)
        {
            Trace(2, "network attach fail");
            return false;
        }
    }
    else
    {
        ret = Network_GetActiveStatus(&status);
        if (!ret)
        {
            Trace(2, "get activate staus fail");
            return false;
        }
        Trace(2, "activate status:%d", status);
        if (!status)
        {
            Network_PDP_Context_t context = {
                .apn = PDP_CONTEXT_APN,
                .userName = PDP_CONTEXT_USERNAME,
                .userPasswd = PDP_CONTEXT_PASSWD};
            Network_StartActive(context);
        }
    }
    return true;
}

static void NetworkEventDispatch(API_Event_t *pEvent)
{
    switch (pEvent->id)
    {
    case API_EVENT_ID_NETWORK_REGISTER_DENIED:
        Trace(2, "network register denied");
        break;

    case API_EVENT_ID_NETWORK_REGISTER_NO:
        Trace(2, "network register no");
        break;

    case API_EVENT_ID_NETWORK_REGISTERED_HOME:
    case API_EVENT_ID_NETWORK_REGISTERED_ROAMING:
        Trace(2, "network register success");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_DETACHED:
        Trace(2, "network detached");
        AttachActivate();
        break;
    case API_EVENT_ID_NETWORK_ATTACH_FAILED:
        Trace(2, "network attach failed");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_ATTACHED:
        Trace(2, "network attach success");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_DEACTIVED:
        Trace(2, "network deactived");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_ACTIVATE_FAILED:
        Trace(2, "network activate failed");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_ACTIVATED:
        Trace(2, "network activate success..");
        if (semMqttStart)
            OS_ReleaseSemaphore(semMqttStart);
        break;

    case API_EVENT_ID_SIGNAL_QUALITY:
        Trace(2, "CSQ:%d", pEvent->param1);
        break;

    default:
        break;
    }
}

static void EventDispatch(API_Event_t *pEvent)
{
    switch (pEvent->id)
    {

    case API_EVENT_ID_NETWORK_REGISTERED_HOME:
    case API_EVENT_ID_NETWORK_REGISTERED_ROAMING:
        Trace(1, "gprs register complete");
        flagIsGPSFinish = true;
        NetworkEventDispatch(pEvent);
        break;

    case API_EVENT_ID_GPS_UART_RECEIVED:
        // Trace(1,"received GPS data,length:%d, data:%s,flag:%d",pEvent->param1,pEvent->pParam1,flag);
        GPS_Update(pEvent->pParam1, pEvent->param1);
        break;
    case API_EVENT_ID_NO_SIMCARD:
        Trace(2, "!!NO SIM CARD%d!!!!", pEvent->param1);
        break;
    case API_EVENT_ID_SIMCARD_DROP:
        Trace(2, "!!SIM CARD%d DROP!!!!", pEvent->param1);
        break;
    case API_EVENT_ID_SYSTEM_READY:
        Trace(2, "system initialize complete");
        break;
    case API_EVENT_ID_NETWORK_REGISTER_DENIED:
    case API_EVENT_ID_NETWORK_REGISTER_NO:
    case API_EVENT_ID_NETWORK_DETACHED:
    case API_EVENT_ID_NETWORK_ATTACH_FAILED:
    case API_EVENT_ID_NETWORK_ATTACHED:
    case API_EVENT_ID_NETWORK_DEACTIVED:
    case API_EVENT_ID_NETWORK_ACTIVATE_FAILED:
    case API_EVENT_ID_NETWORK_ACTIVATED:
    case API_EVENT_ID_SIGNAL_QUALITY:
        NetworkEventDispatch(pEvent);
        break;

    default:
        break;
    }
}

void OnMqttReceived(void *arg, const char *topic, uint32_t payloadLen)
{

    Trace(1, "MQTT received data , topic:[%s], payload length:%d", topic, payloadLen);
}

void OnMqttReceiedData(void *arg, const uint8_t *data, uint16_t len, MQTT_Flags_t flags)
{

    char jsonData[255];
    memset(jsonData, '\0', sizeof(jsonData));

    strncpy(jsonData, data, len);
    Trace(1, "MQTT recieved data , length:%d,data:%s", len, jsonData);
    Trace(1, "cJSON_Version Version: %s ", cJSON_Version());

    ////首先整体判断是否为一个json格式的数据
    cJSON *pJsonRoot = cJSON_Parse(data);

    //如果是否json格式数据
    if (NULL == pJsonRoot)
    {
        Trace(1, "MQTT data pJsonRoot == NULL");
        goto __cJSON_Delete;
    }

    cJSON *pCMD = cJSON_GetObjectItemCaseSensitive(pJsonRoot, "cmd");

    if (cJSON_IsString(pCMD) && (pCMD->valuestring != NULL))
    {
        printf("Checking monitor \"%s\"\n", pCMD->valuestring);
    }

    if (NULL != pCMD)
    {
        Trace(1, "MQTT data pChange get cmd : %s", pCMD->valuestring);
        if (0 == strcmp(pCMD->valuestring, "refresh"))
        {
            Trace(1, "MQTT data cmd:refresh");
            //不能在这里直接发布消息，使用定时器发布不会断开mqtt连接！！
            //也不能把下面的延迟改为0，会直接死机！！哈哈！这个是我测试出来的！
            StartTimerPublish(100, mqttClient);
        }
    }
    else
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        Trace(1, "MQTT data pChange get fail: %s", error_ptr);
    }

__cJSON_Delete:
    cJSON_Delete(pJsonRoot);
}

void OnMqttSubscribed(void *arg, MQTT_Error_t err)
{
    if (err != MQTT_ERROR_NONE)
        Trace(1, "MQTT subscribe fail,error code:%d", err);
    else
        Trace(1, "MQTT subscribe success  ,  topic:%s", (const char *)arg);
}

void OnMqttConnection(MQTT_Client_t *client, void *arg, MQTT_Connection_Status_t status)
{
    Trace(1, "MQTT connection status:%d", status);

    MQTT_Event_t *event = (MQTT_Event_t *)OS_Malloc(sizeof(MQTT_Event_t));
    if (!event)
    {
        Trace(1, "MQTT no memory");
        return;
    }
    if (status == MQTT_CONNECTION_ACCEPTED)
    {
        Trace(1, "MQTT succeed connect to broker");
        //!!! DO NOT suscribe here(interrupt function), do MQTT suscribe in task, or it will not excute
        event->id = MQTT_EVENT_CONNECTED;
        event->client = client;
        //mqttClient = (MQTT_Client_t *)OS_Malloc(sizeof(MQTT_Client_t));
        mqttClient = client;
        OS_SendEvent(mqttTaskHandle, event, OS_TIME_OUT_WAIT_FOREVER, OS_EVENT_PRI_NORMAL);
    }
    else
    {
        event->id = MQTT_EVENT_DISCONNECTED;
        event->client = client;
        mqttClient = client;
        OS_SendEvent(mqttTaskHandle, event, OS_TIME_OUT_WAIT_FOREVER, OS_EVENT_PRI_NORMAL);
        Trace(1, "MQTT connect to broker fail,error code:%d", status);
    }
    Trace(1, "MQTT OnMqttConnection() end");
}

void OnPublish(void *arg, MQTT_Error_t err)
{
    if (err == MQTT_ERROR_NONE)
        Trace(1, "MQTT publish success");
    else
        Trace(1, "MQTT publish error, error code:%d", err);
}

void OnTimerPublish(void *param)
{
    MQTT_Error_t err;
    MQTT_Client_t *client = (MQTT_Client_t *)param;
    uint8_t status = MQTT_IsConnected(client);
    Trace(1, "mqtt status:%d", status);
    if (mqttStatus != MQTT_STATUS_CONNECTED)
    {
        Trace(1, "MQTT not connected to broker! can not publish");
        return;
    }
    Trace(1, "MQTT OnTimerPublish");

    //向MQTT服务器(broker)发布消息
    //client：MQTT客户端对象
    // topic：主题
    // port：服务器端口
    // payload：消息体
    // payloadLen：消息体长度
    // dup：标示发送重复数
    // qos：服务质量
    // retain:需要服务器持久保存消息
    // callback：发布请求回调函数
    // arg：需要传递给回调函数的参数
    err = MQTT_Publish(client, pubTopic, postDate, strlen(postDate), 1, 2, 0, OnPublish, NULL);

    if (err != MQTT_ERROR_NONE)
        Trace(1, "MQTT publish error, error code:%d", err);
}

void StartTimerPublish(uint32_t interval, MQTT_Client_t *client)
{
    OS_StartCallbackTimer(mainTaskHandle, interval, OnTimerPublish, (void *)client);
}

void OnTimerStartConnect(void *param)
{
    MQTT_Error_t err;
    MQTT_Client_t *client = (MQTT_Client_t *)param;
    uint8_t status = MQTT_IsConnected(client);
    Trace(1, "mqtt status:%d", status);
    if (mqttStatus == MQTT_STATUS_CONNECTED)
    {
        Trace(1, "already connected!");
        return;
    }
    err = MQTT_Connect(client, BROKER_IP, BROKER_PORT, OnMqttConnection, NULL, &ci);
    if (err != MQTT_ERROR_NONE)
    {
        Trace(1, "MQTT connect fail,error code:%d", err);
        reconnectInterval += 1000;
        if (reconnectInterval >= 60000)
            reconnectInterval = 60000;
        StartTimerConnect(reconnectInterval, client);
    }
}

void StartTimerConnect(uint32_t interval, MQTT_Client_t *client)
{
    OS_StartCallbackTimer(mainTaskHandle, interval, OnTimerStartConnect, (void *)client);
}

void SecondTaskEventDispatch(MQTT_Event_t *pEvent)
{
    switch (pEvent->id)
    {
    case MQTT_EVENT_CONNECTED:
        reconnectInterval = 3000;
        mqttStatus = MQTT_STATUS_CONNECTED;
        Trace(1, "MQTT connected, now subscribe topic:%s", subTopic);
        MQTT_Error_t err;
        MQTT_SetInPubCallback(pEvent->client, OnMqttReceived, OnMqttReceiedData, (pEvent->client));
        //向MQTT服务器(broker)发起订阅主题请求
        err = MQTT_Subscribe(pEvent->client, subTopic, 2, OnMqttSubscribed, (void *)subTopic);
        if (err != MQTT_ERROR_NONE)
            Trace(1, "MQTT subscribe error, error code:%d", err);
        StartTimerPublish(PUBLISH_INTERVAL, pEvent->client);
        break;
    case MQTT_EVENT_DISCONNECTED:
        mqttStatus = MQTT_STATUS_DISCONNECTED;
        StartTimerConnect(reconnectInterval, pEvent->client);
        break;
    default:
        break;
    }
}

void TaskMqtt(void *pData)
{
    MQTT_Event_t *event = NULL;

    semMqttStart = OS_CreateSemaphore(0);
    OS_WaitForSemaphore(semMqttStart, OS_WAIT_FOREVER);
    OS_DeleteSemaphore(semMqttStart);
    semMqttStart = NULL;

    Trace(1, "start mqtt task");

    INFO_GetIMEI(imei);

    sprintf(subTopic, "/A9g/%s/get", imei);
    sprintf(pubTopic, "/A9g/%s/update", imei);

    Trace(1, "start mqtt test");
    Trace(1, "subTopic: %s", subTopic);
    Trace(1, "pubTopic: %s", pubTopic);

    MQTT_Client_t *client = MQTT_ClientNew();
    MQTT_Error_t err;
    memset(&ci, 0, sizeof(MQTT_Connect_Info_t));
    ci.client_id = imei;
    ci.client_user = CLIENT_USER;
    ci.client_pass = CLIENT_PASS;
    ci.keep_alive = 30;
    ci.clean_session = 1;
    ci.use_ssl = false;

    err = MQTT_Connect(client, BROKER_IP, BROKER_PORT, OnMqttConnection, NULL, &ci);
    if (err != MQTT_ERROR_NONE)
        Trace(1, "MQTT connect fail,error code:%d", err);

    while (1)
    {
        if (OS_WaitEvent(mqttTaskHandle, (void **)&event, OS_TIME_OUT_WAIT_FOREVER))
        {
            SecondTaskEventDispatch(event);
            OS_Free(event);
        }
    }
}

void TaskGPS(void *pData)
{
    GPS_Info_t *gpsInfo = Gps_GetInfo();
    uint8_t buffer[150];
    //wait for gprs register complete
    //The process of GPRS registration network may cause the power supply voltage of GPS to drop,
    //which resulting in GPS restart.
    while (!flagIsGPSFinish)
    {
        Trace(1, "wait for gprs regiter complete please .");
        OS_Sleep(2000);
    }

    //open GPS hardware(UART2 open either)
    GPS_Init();
    GPS_Open(NULL);

    //wait for gps start up, or gps will not response command
    while (gpsInfo->rmc.latitude.value == 0)
    {
        Trace(1, "while (gpsInfo->rmc.latitude.value == 0)");
        OS_Sleep(1000);
    }

    // set gps nmea output interval
    for (uint8_t i = 0; i < 5; ++i)
    {
        bool ret = GPS_SetOutputInterval(10000);
        Trace(1, "set gps ret:%d", ret);
        if (ret)
            break;
        OS_Sleep(1000);
    }

    if (!GPS_GetVersion(buffer, 150))
        Trace(1, "get gps firmware version fail");
    else
        Trace(1, "gps firmware version:%s", buffer);

    if (!GPS_SetOutputInterval(1000))
        Trace(1, "set nmea output interval fail");

    Trace(1, "init ok");

    while (1)
    {
        //show fix info
        uint8_t isFixed = gpsInfo->gsa[0].fix_type > gpsInfo->gsa[1].fix_type ? gpsInfo->gsa[0].fix_type : gpsInfo->gsa[1].fix_type;
        char *isFixedStr = "no fix";
        if (isFixed == 2)
            isFixedStr = "2D fix";
        else if (isFixed == 3)
        {
            if (gpsInfo->gga.fix_quality == 1)
                isFixedStr = "3D fix";
            else if (gpsInfo->gga.fix_quality == 2)
                isFixedStr = "3D/DGPS fix";
        }

        //convert unit ddmm.mmmm to degree(°)
        int temp = (int)(gpsInfo->rmc.latitude.value / gpsInfo->rmc.latitude.scale / 100);
        double latitude = temp + (double)(gpsInfo->rmc.latitude.value - temp * gpsInfo->rmc.latitude.scale * 100) / gpsInfo->rmc.latitude.scale / 60.0;
        temp = (int)(gpsInfo->rmc.longitude.value / gpsInfo->rmc.longitude.scale / 100);
        double longitude = temp + (double)(gpsInfo->rmc.longitude.value - temp * gpsInfo->rmc.longitude.scale * 100) / gpsInfo->rmc.longitude.scale / 60.0;

        // gcvt(latitude, 6, getLatitude);
        // gcvt(longitude, 6, getLongitude);

        //you can copy ` getLatitude,buff2 `(latitude,longitude) to http://www.gpsspg.com/maps.htm check location on map

        //snprintf(buffer, sizeof(buffer), "{\"IsFix\":\"%s\",\"Lat\":%s,\"Lon\":%s}", isFixedStr, getLatitude, getLongitude);
        sprintf(postDate, "{\"IsFix\":\"%s\",\"Lat\":%lf,\"Lon\":%lf}", isFixedStr, latitude, longitude);

        // snprintf(buffer, sizeof(buffer), "{\"GPSfixMode\":%d, \"BDSFixMode\":%d,\"FixQuality\":%d,\"IsFixed\":\"%s\",\"coordinate\":\"WGS84\", \"Latitude\":%s,\"Longitude\":%s,\"unit\":\"degree\"}", gpsInfo->gsa[0].fix_type, gpsInfo->gsa[1].fix_type,
        //          gpsInfo->gga.fix_quality, isFixedStr, getLatitude, getLongitude);

        //show in tracer
        //Trace(1, buffer);
        Trace(1, postDate);
        //sayHello();

        OS_Sleep(5000); //10秒后获取GPS定位信息
    }
}

void MainTask(void *pData)
{
    API_Event_t *event = NULL;

    mqttTaskHandle = OS_CreateTask(TaskMqtt, NULL, NULL, SECOND_TASK_STACK_SIZE, SECOND_TASK_PRIORITY, 0, 0, SECOND_TASK_NAME);

    grsTaskHandle = OS_CreateTask(TaskGPS, NULL, NULL, GPS_TASK_STACK_SIZE, GPS_TASK_PRIORITY, 0, 0, GPS_TASK_NAME);

    while (1)
    {
        if (OS_WaitEvent(mainTaskHandle, (void **)&event, OS_TIME_OUT_WAIT_FOREVER))
        {
            EventDispatch(event);
            OS_Free(event->pParam1);
            OS_Free(event->pParam2);
            OS_Free(event);
        }
    }
}

void mqtt_Main(void)
{
    mainTaskHandle = OS_CreateTask(MainTask,
                                   NULL, NULL, MAIN_TASK_STACK_SIZE, MAIN_TASK_PRIORITY, 0, 0, MAIN_TASK_NAME);
    OS_SetUserMainHandle(&mainTaskHandle);
}
