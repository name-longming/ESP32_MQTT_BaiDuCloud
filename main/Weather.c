#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <lwip/sockets.h>
#include "lwip/sys.h"
#include "sdkconfig.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "mqtt_client.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define EXAMPLE_WIFI_SSID        "HUAWEI-3AHFVD"//需要连接到的wifi热点的SSID和密码
#define EXAMPLE_WIFI_PASS        "13550695909"

/*
===========================
宏定义
=========================== 
*/
#define false		0
#define true		1
#define errno		(*__errno())

//http组包宏，获取天气的http接口参数
#define WEB_SERVER          "api.thinkpage.cn"              
#define WEB_PORT            "80"
#define WEB_URL             "/v3/weather/now.json?key="
#define APIKEY		        "SG_ckZ26xPfp8E2EK"       
#define city		        "chengdu"
#define language	        "en"
/*
===========================
全局变量定义
=========================== 
*/
//http请求包
static const char *REQUEST = "GET "WEB_URL""APIKEY"&location="city"&language="language" HTTP/1.1\r\n"
    "Host: "WEB_SERVER"\r\n"
    "Connection: close\r\n"
    "\r\n";

//wifi链接成功事件
static EventGroupHandle_t wifi_event_group;
//天气解析结构体
typedef struct 
{
    char cit[20];
    char weather_text[20];
    char weather_code[2];
    char temperatur[3];
}weather_info;

weather_info weathe;

static EventGroupHandle_t mqtt_event_group;
esp_mqtt_client_handle_t client;
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;//添加连接成功消息
static const char *TAG = "main";
static const char *HTTP_TAG = "http_task";
//static const int ESPTOUCH_DONE_BIT = BIT1;

void http_get_task(void);

void cjson_to_struct_info(char *text)
{
    cJSON *root,*psub;
    cJSON *arrayItem;
    //截取有效json
    char *index=strchr(text,'{');
    strcpy(text,index);

    root = cJSON_Parse(text);
    
    if(root!=NULL)
    {
        psub = cJSON_GetObjectItem(root, "results");
        arrayItem = cJSON_GetArrayItem(psub,0);

        cJSON *locat = cJSON_GetObjectItem(arrayItem, "location");
        cJSON *now = cJSON_GetObjectItem(arrayItem, "now");
        if((locat!=NULL)&&(now!=NULL))
        {
            psub=cJSON_GetObjectItem(locat,"name");
            sprintf(weathe.cit,"%s",psub->valuestring);
            ESP_LOGI(HTTP_TAG,"city:%s",weathe.cit);

            psub=cJSON_GetObjectItem(now,"text");
            sprintf(weathe.weather_text,"%s",psub->valuestring);
            ESP_LOGI(HTTP_TAG,"weather:%s",weathe.weather_text);
            
            psub=cJSON_GetObjectItem(now,"code");
            sprintf(weathe.weather_code,"%s",psub->valuestring);
            //ESP_LOGI(HTTP_TAG,"%s",weathe.weather_code);

            psub=cJSON_GetObjectItem(now,"temperature");
            sprintf(weathe.temperatur,"%s",psub->valuestring);
            ESP_LOGI(HTTP_TAG,"temperatur:%s",weathe.temperatur);

            //ESP_LOGI(HTTP_TAG,"--->city %s,weather %s,temperature %s<---\r\n",weathe.cit,weathe.weather_text,weathe.temperatur);
        }
    }
    //ESP_LOGI(HTTP_TAG,"%s 222",__func__);
    cJSON_Delete(root);
}

void http_get_task(void)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[1024];
    char mid_buf[1024];
    while(1) {
        
        //DNS域名解析
        int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);
        if(err != 0 || res == NULL) {
            ESP_LOGE(HTTP_TAG, "DNS lookup failed err=%d res=%p\r\n", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        //打印获取的IP
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        //ESP_LOGI(HTTP_TAG, "DNS lookup succeeded. IP=%s\r\n", inet_ntoa(*addr));

        //新建socket
        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(HTTP_TAG, "... Failed to allocate socket.\r\n");
            close(s);
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        //连接ip
        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(HTTP_TAG, "... socket connect failed errno=%d\r\n", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        freeaddrinfo(res);
        //发送http包
        if (write(s, REQUEST, strlen(REQUEST)) < 0) {
            ESP_LOGE(HTTP_TAG, "... socket send failed\r\n");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        //清缓存
        memset(mid_buf,0,sizeof(mid_buf));
        //获取http应答包
        do {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf)-1);
            strcat(mid_buf,recv_buf);
        } while(r > 0);
        //json解析
        cjson_to_struct_info(mid_buf);
        //关闭socket，http是短连接
        close(s);

        break;
    }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED://MQTT连上事件
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            xEventGroupSetBits(mqtt_event_group, CONNECTED_BIT);
            //发布主题
            // msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
            // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            //发送订阅
            // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
            // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            //发送订阅
            msg_id = esp_mqtt_client_subscribe(client, "temp_hum", 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            //取消订阅
            // msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
            // ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED://MQTT断开连接事件
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            //mqtt连上事件
            xEventGroupClearBits(mqtt_event_group, CONNECTED_BIT);
            break;

        case MQTT_EVENT_SUBSCRIBED://MQTT发送订阅事件
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            msg_id = esp_mqtt_client_publish(client, "temp_hum", "订阅成功", 0, 0, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED://MQTT取消订阅事件
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED://MQTT发布事件
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA://MQTT接受数据事件
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);   //主题
            printf("DATA=%.*s\r\n", event->data_len, event->data);      //内容
            break;
        case MQTT_EVENT_ERROR://MQTT错误事件
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            xEventGroupClearBits(mqtt_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static esp_err_t event_handler(void *ctx, system_event_t *event)//终端事件处理
{

    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
      ESP_LOGI(TAG, "STA start");
      esp_wifi_connect();//当事件为SYSTEM_EVENT_STA_START时，进行wifi连接

    break;

    case SYSTEM_EVENT_STA_GOT_IP:
      ESP_LOGD(TAG, "Got an IP: " IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));//获取连接成功的ip详情
      xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);//设置CONNECTED_BIT事件标志位

    break;

    case SYSTEM_EVENT_STA_DISCONNECTED:

        /* This is a workaround as ESP32 WiFi libs don't currently

           auto-reassociate. */
      esp_wifi_connect();                                                                                    //esp32进行wifi连接
      xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);//清楚事件标志位
    break;

    default:
    break;

    }

    return ESP_OK;

}



static void initialise_wifi(void)//函数初始化WIFI

{

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },//staiton配置要连接到的站点的信息
    };

    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();//创建event_group
    mqtt_event_group = xEventGroupCreate();
    esp_event_loop_init(event_handler, NULL);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));//wifi初始化
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));//设置内存ram
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));//wifi模式设置
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));//wifi作为station的一些配置
    ESP_ERROR_CHECK(esp_wifi_start());//wifi开始工作

    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);//创建事件等待位，等待wifi连接事件产生,每有一个connect连接事件就执行其后的程序一次，否则一直等待并执行其他程序
}

//mqtt初始化
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .host = "183.240.93.18",            //MQTT服务器IP
        //.uri = "183.240.93.18",            //MQTT服务器域名：123.123.net,改成自己服务的域名
        .username = "xmh6fy0/stm32f4",
        .password = "bJYDuKbxcOMikzLd",
        .client_id = "esp32",
        .event_handle = mqtt_event_handler, //MQTT事件
        .port=1883,                         //端口
        // .user_context = (void *)your_context
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    //esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    //等mqtt连上
    xEventGroupWaitBits(mqtt_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

void app_main(void)
{
    char buf[100];

    nvs_flash_init();//初始化nvs_flash
    initialise_wifi();//初始化wifi设置
    http_get_task();
    mqtt_app_start();
    //printf("%s,%s\r\n",weathe.temperatur,weathe.weather_text);
    sprintf(buf,"{\"name\": \"%s\", \"temp\": %d}",weathe.weather_text,atoi(weathe.temperatur));
    esp_mqtt_client_publish(client, "temp_hum", buf, 0, 0, 0);
    while (true) {
      vTaskDelay(5000 / portTICK_PERIOD_MS);

    }

}
