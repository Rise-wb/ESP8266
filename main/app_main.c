#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "driver/gpio.h"
#include "driver/spi.h"
#include "driver/uart.h"
#include "esp8266/gpio_struct.h"
#include "esp8266/spi_struct.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_EXAMPLE";
#define ESP_WIFI_SSID      "Rise111"//////////
#define ESP_WIFI_PASS      "11111111"//////////
#define ESP_HOST      "1.117.171.99"//"mq.tongxinmao.com"//"1.117.171.99"
#define ESP_PORT      1883
//adxl345
#define ADXL345_CS     15
#define ADXL345_MISO   12
#define ADXL345_MOSI  13
#define ADXL345_SCK   14
#define G5  5
#define ADXL345_test   (1ULL<<G5)
#define CS_PIN GPIO_NUM_15
#define ADXL345_PIN  (1ULL<<ADXL345_MISO) | (1ULL<<ADXL345_CS) | (1ULL<<ADXL345_MOSI) |(1ULL<<ADXL345_SCK)
//#define ADXL345_PIN (1ULL<<G5)
#define ADXL345_CS_LOW()    gpio_set_level(CS_PIN,0)
#define ADXL345_CS_HIGH()   gpio_set_level(CS_PIN,1)
#define transBufSize 3300
//uint32_t ADXL345_REG_DEVID = 0x00u;
/** @brief ADXL345 register read flag. */
uint16_t ADXL345_REG_READ_FLAG = 0x80u;
/** @brief ADXL345 register multibyte flag. */
uint16_t ADXL345_REG_MB_FLAG = 0x40u;
/** @brief ADXL345 register: DEVID. */
uint16_t ADXL345_REG_DEVID = 0x00u;
/** @brief ADXL345 register: BW_RATE. */
uint16_t ADXL345_REG_BW_RATE = 0x2Cu;
/** @brief ADXL345 register: POWER_CTL. */
uint16_t ADXL345_REG_POWER_CTL = 0x2Du;
/** @brief ADXL345 register: DATAX0. */
uint16_t ADXL345_REG_DATAX0 = 0x32u;
/** @brief ADXL345 POWER_CTL flag: Measure. */
uint32_t ADXL345_POWER_CTL_MEASURE = 0x08u;
char acc[transBufSize];
char acc2[transBufSize];
char acc3[transBufSize];
char acc4[transBufSize];
char acc5[transBufSize];
char acc6[transBufSize];
//char accte[19200];
uint16_t acc_account=0;
uint32_t P_acc[3];
extern uint32_t esp_get_time(void);
//uart
#define BUF_SIZE (1024)
//tcp+mqtt
static esp_mqtt_client_handle_t mqtt_client=NULL;
static EventGroupHandle_t mqtt_event_group;
const static int CONNECTED_BIT = BIT0;
//spi+gpio
void gpio_initialize(){
    printf( "init gpio\n");
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ADXL345_PIN;
    io_conf.pull_down_en = 1;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
}
void spi_initialize(){
    printf( "init spi\n");
    spi_config_t spi_config;
    // Load default interface parameters
    // CS_EN:1, MISO_EN:1, MOSI_EN:1, BYTE_TX_ORDER:1, BYTE_TX_ORDER:1,
    // BIT_RX_ORDER:0, BIT_TX_ORDER:0, CPHA:0, CPOL:0
    spi_config.interface.val = SPI_DEFAULT_INTERFACE;
    spi_config.interface.cpol = 1;
    spi_config.interface.cpha = 1;
    // Load default interrupt enable
    // TRANS_DONE: true, WRITE_STATUS: false, READ_STATUS: false, i
    // WRITE_BUFFER: false, READ_BUFFER: false
    spi_config.intr_enable.val = SPI_MASTER_DEFAULT_INTR_ENABLE;
    // Set SPI to master mode
    // ESP8266 Only support half-duplex (and non-mappable interface)
    spi_config.mode = SPI_MASTER_MODE;
    // Set the SPI clock frequency division factor (divide from 80MHz)
    spi_config.clk_div = SPI_5MHz_DIV;   //100khz     val=40 = 2mhz
    spi_config.event_cb = NULL;
    spi_init(HSPI_HOST, &spi_config);
}
uint8_t spi_read_bytes ( uint16_t cmd){
    uint8_t rx;
    spi_trans_t trans;
    ADXL345_CS_LOW();
    memset(&trans, 0x0, sizeof(trans));
    trans.bits.val = 0;
    trans.cmd = &cmd;
    trans.miso = &rx;
    trans.addr = NULL;
    trans.bits.cmd = 8 * 1;
    trans.bits.miso = 8 ;
    spi_trans(HSPI_HOST, &trans);
    ADXL345_CS_HIGH();
    return rx;
}
void spi_write_bytes ( uint16_t cmd, uint8_t *wdata, int length){
    uint32_t wx[16];
    //convert to uint32_t array from passed byte array
    for(int n=0; n< length; n=n+4) wx[n/4] = *(uint32_t*) &wdata[n];
    ADXL345_CS_LOW() ;

    spi_trans_t trans;
    memset(&trans, 0x0, sizeof(trans));
    trans.bits.val = 0;
    trans.bits.cmd = 8 * 1;
    trans.bits.mosi = 8 * length;
    trans.cmd = &cmd;
    trans.addr = NULL;
    trans.mosi = wx;
    spi_trans(HSPI_HOST, &trans);
}
void spi_write_byte ( uint16_t cmd, uint32_t data){
    spi_trans_t trans;
    ADXL345_CS_LOW();
    memset(&trans, 0x0, sizeof(trans));
    trans.bits.val = 0;
    trans.bits.cmd = 8 * 1;
    trans.bits.addr = 0;          // transmit status do not use address bit
    trans.bits.mosi = 8 * 1;
    trans.cmd = &cmd;
    trans.addr = NULL;
    trans.mosi = &data;
    spi_trans(HSPI_HOST, &trans);
    ADXL345_CS_HIGH();
}
void ADXL_init()
{
    spi_write_byte(0x2c,0x0F);
    spi_write_byte(ADXL345_REG_POWER_CTL,ADXL345_POWER_CTL_MEASURE);
    spi_write_byte(0x31,0x0B);
}
void ADXL_READ()
{
    acc[0]=spi_read_bytes(0x32|0x80);
//     gpio_set_level(GPIO_NUM_15,0);
//    vTaskDelay(500);
    acc[1]=spi_read_bytes(0x34|0x80);
    acc[2]=spi_read_bytes(0x36|0x80);

}
//mqtt+uart
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id=0;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            xEventGroupSetBits(mqtt_event_group, CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            //msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}
static void mqtt_app_start(void)
{
    mqtt_event_group = xEventGroupCreate();
    esp_mqtt_client_config_t mqtt_cfg = {
            .host = ESP_HOST,//MQTT 地址
            .port = ESP_PORT,   //MQTT端口
            .username="JFGCotOYy",
            .password="voxnqzcgu7sswocz2zcdfmmpkutivr6nbukz4thcgkfbxe6yi5trbdqardk06bi7",
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
    xEventGroupClearBits(mqtt_event_group, CONNECTED_BIT);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);
    esp_mqtt_client_start(mqtt_client);
    xEventGroupWaitBits(mqtt_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}
static void taskA(void * pvParameters){
    uart_config_t uart_config = {
            .baud_rate = 74880,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, 0);

    static portTickType xLastWakeTime;
    const portTickType xFrequency = pdMS_TO_TICKS(10*1000);
    xLastWakeTime = xTaskGetTickCount();
    uint32_t time0=0,time1=0,time2=0,time3=0;
    char tes;
    for( ;; )
    {
        //等待下一个周期
        vTaskDelayUntil( &xLastWakeTime,xFrequency );

        // 需要周期性执行代码放在这里
        for (uint32_t i = 0; i < transBufSize;i=i+3) {
            time1 = esp_get_time();
            acc[i] = spi_read_bytes(0x32 | 0x80);
            //acc[i+1] = spi_read_bytes(0x33 | 0x80);
            acc[i+1] = spi_read_bytes(0x34 | 0x80);
            //acc2[i+1] = spi_read_bytes(0x35 | 0x80);
            acc[i+2] = spi_read_bytes(0x36 | 0x80);
            //acc3[i+1] = spi_read_bytes(0x37 | 0x80);
            while (esp_get_time() - time1 < 313);
        }
        for (uint32_t i = 0; i < transBufSize;i=i+3) {
            time1 = esp_get_time();
            acc2[i] = spi_read_bytes(0x32 | 0x80);
            //acc[i+1] = spi_read_bytes(0x33 | 0x80);
            acc2[i+1] = spi_read_bytes(0x34 | 0x80);
            //acc2[i+1] = spi_read_bytes(0x35 | 0x80);
            acc2[i+2] = spi_read_bytes(0x36 | 0x80);
            //acc3[i+1] = spi_read_bytes(0x37 | 0x80);
            while (esp_get_time() - time1 < 313);
        }
        for (uint32_t i = 0; i < transBufSize;i=i+3) {
            time1 = esp_get_time();
            acc3[i] = spi_read_bytes(0x32 | 0x80);
            //acc[i+1] = spi_read_bytes(0x33 | 0x80);
            acc3[i+1] = spi_read_bytes(0x34 | 0x80);
            //acc2[i+1] = spi_read_bytes(0x35 | 0x80);
            acc3[i+2] = spi_read_bytes(0x36 | 0x80);
            //acc3[i+1] = spi_read_bytes(0x37 | 0x80);
            while (esp_get_time() - time1 < 313);
        }

        esp_mqtt_client_publish(mqtt_client, "device-data",acc, transBufSize ,1, 0);
        //vTaskDelay(10000 / portTICK_PERIOD_MS);
        esp_mqtt_client_publish(mqtt_client, "device-data",acc2, transBufSize ,1, 0);
        //vTaskDelay(10000 / portTICK_PERIOD_MS);
        esp_mqtt_client_publish(mqtt_client, "device-data",acc3, transBufSize ,1, 0);
        //esp_mqtt_client_publish(mqtt_client, "device-data",acc4, transBufSize ,1, 0);
//        esp_mqtt_client_publish(mqtt_client, "device-data",acc5, transBufSize ,1, 0);
//        esp_mqtt_client_publish(mqtt_client, "device-data",acc6, transBufSize ,1, 0);
        //uart_write_bytes(UART_NUM_0, (const char *) acc, transBufSize);
        //uart_write_bytes(UART_NUM_0, (const char *) acc2, transBufSize);
        //uart_write_bytes(UART_NUM_0, (const char *) acc3, transBufSize);
        //uart_write_bytes(UART_NUM_0, (const char *) acc4, transBufSize);
//        uart_write_bytes(UART_NUM_0, (const char *) acc5, transBufSize);
//        uart_write_bytes(UART_NUM_0, (const char *) acc6, transBufSize);


    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    gpio_initialize();
    ADXL345_CS_HIGH();
    spi_initialize();
    ADXL_init();
    ESP_ERROR_CHECK(example_set_connection_info(ESP_WIFI_SSID, ESP_WIFI_PASS));
    ESP_ERROR_CHECK(example_connect());
    mqtt_app_start();
    //xTaskCreate(taskA, "taskA", 1024, NULL, 5, NULL);
    //while (1) {
    //    char tes =spi_read_bytes(0x33|0x80);
    //    tes = tes&0x1f;
    //    char test =spi_read_bytes(0x32|0x80);
    //    printf("%x %x\n",tes,test);
    //    vTaskDelay(500 / portTICK_PERIOD_MS);
    //}
char test11[5]={0x00,0x00,0x00,0,0};
 esp_mqtt_client_publish(mqtt_client, "device-data",test11, 5 ,1, 0);

}
