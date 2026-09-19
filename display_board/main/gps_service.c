#include "gps_service.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static gps_fix_t s_fix;
static void publish(const gps_fix_t *fix)
{ portENTER_CRITICAL(&s_lock); s_fix=*fix; portEXIT_CRITICAL(&s_lock); }
void gps_service_get(gps_fix_t *fix)
{ if (fix) { portENTER_CRITICAL(&s_lock); *fix=s_fix; portEXIT_CRITICAL(&s_lock); } }
static void receive_task(void *arg)
{
    (void)arg;
    gps_nmea_t parser={0}; gps_fix_t fix={.enabled=true}; uint8_t bytes[128];
    for (;;) {
        int count=uart_read_bytes(UART_NUM_1,bytes,sizeof(bytes),pdMS_TO_TICKS(200));
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
        for (int i=0;i<count;++i) if (gps_nmea_feed(&parser,(char)bytes[i],now,&fix)) publish(&fix);
    }
}
esp_err_t gps_service_start(void)
{
    /* GPIO4 is J1.18 on EV Board v1.4. Receive-only: module RX is not driven. */
    uart_config_t cfg={.baud_rate=CONFIG_DEVICE_GPS_BAUD,.data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,
        .source_clk=UART_SCLK_DEFAULT};
    esp_err_t err=uart_driver_install(UART_NUM_1,1024,0,0,NULL,0);
    bool installed=err==ESP_OK;
    if (err==ESP_OK) err=uart_param_config(UART_NUM_1,&cfg);
    if (err==ESP_OK) err=uart_set_pin(UART_NUM_1,UART_PIN_NO_CHANGE,CONFIG_DEVICE_GPS_RX_GPIO,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if (err==ESP_OK) err=gpio_set_pull_mode(CONFIG_DEVICE_GPS_RX_GPIO,GPIO_PULLUP_ONLY);
    if (err==ESP_OK) {
        gps_fix_t waiting={.enabled=true}; publish(&waiting);
        if (xTaskCreate(receive_task,"gps_rx",3072,NULL,2,NULL)!=pdPASS) err=ESP_ERR_NO_MEM;
    }
    if (err!=ESP_OK) {
        if (installed) uart_driver_delete(UART_NUM_1);
        gps_fix_t failed={.enabled=true,.error=true}; publish(&failed);
    } else ESP_LOGI("gps","NMEA receive-only: UART1 RX=GPIO%d baud=%d",CONFIG_DEVICE_GPS_RX_GPIO,CONFIG_DEVICE_GPS_BAUD);
    return err;
}
