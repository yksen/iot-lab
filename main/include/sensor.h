#include <esp_nimble_hci.h>
#include <driver/i2c.h>

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/util/util.h>

#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

// Hardware configuration
#define LED_GPIO_PIN GPIO_NUM_1
#define I2C_PORT_NUMBER I2C_NUM_0
#define I2C_CLK_FREQUENCY 100000
#define I2C_SDA_PIN GPIO_NUM_4
#define I2C_SCL_PIN GPIO_NUM_5
#define I2C_TIMEOUT (100 / portTICK_RATE_MS)
#define I2C_AHT20_ADDRESS 0x38

// Sensor configuration
#define CMD_SOFTRESET 0xBA
#define CMD_TRIGGER 0xAC
#define CMD_CALIBRATE 0xE1
#define STATUS_CALIBRATED 0x08

// Bluetooth configuration (Environmental Sensing Service)
#define GATT_ESS_UUID 0x181A
#define GATT_ESS_TEMPERATURE_UUID 0x2A6E
#define GATT_ESS_HUMIDITY_UUID 0x2A6F

uint16_t humidity;
int16_t temperature;

static bool device_connected;
static uint16_t conn_handle;
static uint16_t humidity_handle;
static uint16_t temperature_handle;

static void startAdvertisement(void);

static void setLedState(bool state)
{
    gpio_set_direction(LED_GPIO_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO_PIN, state ? 1 : 0);
}

static void waitMs(unsigned delay)
{
    vTaskDelay(delay / portTICK_PERIOD_MS);
}

static int getTemperature(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc = os_mbuf_append(ctxt->om, &temperature, sizeof(temperature));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int getHumidity(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc = os_mbuf_append(ctxt->om, &humidity, sizeof(humidity));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def kBleServices[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(GATT_ESS_UUID),
     .characteristics = (struct ble_gatt_chr_def[]){
         {
             .uuid = BLE_UUID16_DECLARE(GATT_ESS_TEMPERATURE_UUID),
             .access_cb = getTemperature,
             .val_handle = &temperature_handle,
             .flags = BLE_GATT_CHR_F_NOTIFY,
         },
         {
             .uuid = BLE_UUID16_DECLARE(GATT_ESS_HUMIDITY_UUID),
             .access_cb = getHumidity,
             .val_handle = &humidity_handle,
             .flags = BLE_GATT_CHR_F_NOTIFY,
         },
         {
             0,
         },
     }},
    {
        0,
    },
};

static int onBleGapEvent(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI("BLE GAP Event", "Connected");
        setLedState(true);
        device_connected = true;
        conn_handle = event->connect.conn_handle;
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("BLE GAP Event", "Disconnected");
        setLedState(false);
        startAdvertisement();
        device_connected = false;
        break;

    default:
        ESP_LOGI("BLE GAP Event", "Type: 0x%02X", event->type);
        break;
    }

    return 0;
}

static void startAdvertisement(void)
{
    struct ble_gap_adv_params adv_parameters;
    memset(&adv_parameters, 0, sizeof(adv_parameters));

    adv_parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;

    if (ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &adv_parameters,
                          onBleGapEvent, NULL) != 0)
    {
        ESP_LOGE("BLE", "Can't start Advertisement");
        return;
    }

    ESP_LOGI("BLE", "Advertisement started...");
}

static void setDeviceName(const char *device_name)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    if (ble_gap_adv_set_fields(&fields) != 0)
    {
        ESP_LOGE("BLE", "Can't configure BLE advertisement fields");
        return;
    }

    ble_svc_gap_device_name_set(device_name);
}

static void startBleService(void *param)
{
    ESP_LOGI("BLE task", "BLE Host Task Started");

    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void initializeI2C(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_CLK_FREQUENCY,
    };

    i2c_param_config(I2C_PORT_NUMBER, &conf);
    i2c_driver_install(I2C_PORT_NUMBER, conf.mode, 0, 0, 0);
}

static void writeToTheSensor(uint8_t *data, size_t length)
{
    i2c_master_write_to_device(I2C_PORT_NUMBER, I2C_AHT20_ADDRESS, data, length, I2C_TIMEOUT);
}

static void readFromTheSensor(uint8_t *buffer, size_t length)
{
    i2c_master_read_from_device(I2C_PORT_NUMBER, I2C_AHT20_ADDRESS, buffer, length, I2C_TIMEOUT);
}

void getValuesFromSensor()
{
    uint8_t cmd_softreset = CMD_SOFTRESET;
    writeToTheSensor(&cmd_softreset, 1);
    waitMs(20);

    uint8_t cmd_calibrate = {CMD_CALIBRATE, STATUS_CALIBRATED, 0x00};
    writeToTheSensor(&cmd_calibrate, 3);
    waitMs(100);

    uint8_t cmd_trigger[3] = {CMD_TRIGGER, 0x33, 0};
    writeToTheSensor(&cmd_trigger, 3);
    waitMs(300);

    uint8_t data[6];
    readFromTheSensor(&data, 6);

    uint32_t hData = data[1];
    hData <<= 8;
    hData |= data[2];
    hData <<= 4;
    hData |= data[3] >> 4;
    humidity = ((float)hData * 100) / 0x100000 * 100;

    uint32_t tData = data[3] & 0x0F;
    tData <<= 8;
    tData |= data[4];
    tData <<= 8;
    tData |= data[5];
    temperature = (((float)tData * 200 / 0x100000) - 50) * 100;

    ESP_LOGI("Values from sensors", "Humidity: %f, Temperature: %f", (float)humidity / 100, (float)temperature / 100);
}

void getAndNotifyValues()
{
    getValuesFromSensor();

    if (device_connected)
    {
        int rc;
        struct os_mbuf *om;
        om = ble_hs_mbuf_from_flat(&humidity, sizeof(humidity));
        rc = ble_gattc_notify_custom(conn_handle, humidity_handle, om);

        om = ble_hs_mbuf_from_flat(&temperature, sizeof(temperature));
        rc = ble_gattc_notify_custom(conn_handle, temperature_handle, om);
    }
}

void initializeBluetoothI2C()
{
    // Initialize BLE peripheral
    esp_nimble_hci_and_controller_init();
    nimble_port_init();

    // Initialize BLE library (nimble)
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Configure BLE library (nimble)
    int rc = ble_gatts_count_cfg(kBleServices);
    if (rc != 0)
    {
        ESP_LOGE("BLE GATT", "Service registration failed");
    }

    rc = ble_gatts_add_svcs(kBleServices);
    if (rc != 0)
    {
        ESP_LOGE("BLE GATT", "Service registration failed");
    }

    // Run BLE
    nimble_port_freertos_init(startBleService);

    setDeviceName("AHT20 Destiny");
    startAdvertisement();

    initializeI2C();
}