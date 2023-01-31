#include <string.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#if !CONFIG_IDF_TARGET_LINUX
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#endif

#include "esp_http_client.h"

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

#define DEVICE_ID "kcQvFok0S4rahYTnfezRTIx3"
#define DEVICE_TOKEN "maker:4jrExtJr32uoVBrPrnJN7K23URhTkHStLJ8LFUMO"

extern uint16_t humidity;
extern int16_t temperature;

static const char *TAG_HTTP = "HTTP_CLIENT";

esp_err_t httpEventHandler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG_HTTP, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG_HTTP, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG_HTTP, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG_HTTP, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG_HTTP, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
            // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
            // ESP_LOG_BUFFER_HEX(TAG_HTTP, output_buffer, output_len);
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_HTTP, "HTTP_EVENT_DISCONNECTED");
        int mbedtls_err = 0;
        esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
        if (err != 0)
        {
            ESP_LOGI(TAG_HTTP, "Last esp error code: 0x%x", err);
            ESP_LOGI(TAG_HTTP, "Last mbedtls failure: 0x%x", mbedtls_err);
        }
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void httpPutValue(const char *asset, float value)
{
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

    char path_string[64];
    sprintf(path_string, "/device/" DEVICE_ID "/asset/%s/state", asset);

    esp_http_client_config_t config = {
        .host = "api.allthingstalk.io",
        .path = path_string,
        .event_handler = httpEventHandler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_method(client, HTTP_METHOD_PUT);
    esp_http_client_set_header(client, "Authorization", "Bearer " DEVICE_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    char value_string[16];
    sprintf(value_string, "%f", value);
    char post_data[64];
    sprintf(post_data, "{\"value\": %s}", value_string);
    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_HTTP, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    }
    else
    {
        int wlen = esp_http_client_write(client, post_data, strlen(post_data));
    }

    err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG_HTTP, "HTTP PUT Status = %d", esp_http_client_get_status_code(client));
    }
    else
    {
        ESP_LOGE(TAG_HTTP, "HTTP PUT request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void httpPutSensorValues()
{
    getValuesFromSensor();
    httpPutValue("temperature", temperature / 100.f);
    httpPutValue("humidity", humidity / 100.f);
}