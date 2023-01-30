#include <esp_log.h>
#include <esp_console.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include "include/wifi.h"
// #include "include/mqtt.h"

static xTimerHandle timerBT;
static xTimerHandle timerMQTT;

void app_main(void)
{
    // Initialize Non-Volatile Memory
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGI("NVS", "Initializing NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    initializeBluetoothI2C();
    wifiInitSTA();

    timerBT = xTimerCreate("timerBT", pdMS_TO_TICKS(10000), pdTRUE, (void *)0, getAndNotifyValues);
    xTimerStart(timerBT, 1);

    // timerMQTT = xTimerCreate("timerMQTT", pdMS_TO_TICKS(30000), pdTRUE, (void *)0, mqtt_app_start);
    // xTimerStart(timerMQTT, 1);
}