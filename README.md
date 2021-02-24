## 描述

该项目可以直接修改，连接网络上任何MQTT协议的服务器📃

**①**📁修改WIFI连接部分

```C
#define EXAMPLE_WIFI_SSID        "HUAWEI-3AHFVD"//需要连接到的wifi热点名称SSID
#define EXAMPLE_WIFI_PASS        "13550695909"	//需要连接到的wifi热点名称密码
```

**②**🔨修改MQTT连接地址及其password

```C
esp_mqtt_client_config_t mqtt_cfg = {
    .host = "183.240.93.18",            //MQTT服务器IP
    .username = "xmh6fy0/stm32f4",		//MQTT服务器username
    .password = "bJYDuKbxcOMikzLd",		//MQTT服务器password
    .client_id = "esp32",				//客户端名字（任意）
    .event_handle = mqtt_event_handler, //MQTT事件
    .port=1883,                         //端口（默认）
    // .user_context = (void *)your_context
};
```

**③**🔧修改你需要订阅的服务器主题

```C
msg_id = esp_mqtt_client_subscribe(client, "test", 1);//test为你所订阅的主题，1为服务质量为QoS1
```

