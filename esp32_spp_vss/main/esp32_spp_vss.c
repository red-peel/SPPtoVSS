#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "driver/gpio.h"
#include "esp_timer.h"



static const char *TAG = "SPP_VSS";

/* ========= User-tunable bits ========= */
#define BT_DEVICE_NAME   "VSS-ESP32"
#define SPP_SERVER_NAME  "VSS_SPP"

/* ========= Runtime state ========= */
static uint32_t s_spp_handle = 0;
static float g_mph = 0.0f;

/* ========= VSS pulse output =========
Open-drain sink output to emulate hall/open-collector VSS line.
*/
#define VSS_GPIO               GPIO_NUM_25   // change if you want

/* Calibration: pulses per mile (tune this to match your speedo) */

#define VSS_PULSES_PER_MILE (int)(4000 * 1.035)        // ≈ 4140 for 2007 Subaru Impreza 2.5i
#define MPH_TIMEOUT_MS         1500           // stop pulses if stale

static volatile int64_t g_last_mph_us = 0;
static esp_timer_handle_t g_vss_timer = NULL;
static volatile bool g_vss_level_low = false; // current output phase
static volatile float g_target_hz = 0.0f;



/* Line-oriented RX buffer */
static char   s_line_buf[256];
static size_t s_line_len = 0;

static inline void mph_set(float mph)
{
    if (mph < 0.0f) mph = 0.0f;
    if (mph > 200.0f) mph = 200.0f;

    g_mph = mph;
    g_last_mph_us = esp_timer_get_time();

    // Hz = MPH * pulses_per_mile / 3600
    g_target_hz = (g_mph * VSS_PULSES_PER_MILE) / 3600.0f;
}


/*
 * Accepts incoming lines like:
 *   "12.34"
 *   "MPH:12.34"
 *   "mph=12.34"
 * Any line containing a parseable float will work.
 */
static void parse_line_update_mph(const char *line)
{
    // trim leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // find first digit or sign
    const char *p = line;
    while (*p && !((*p >= '0' && *p <= '9') || *p == '-' || *p == '+')) p++;
    if (!*p) return;

    char *endptr = NULL;
    float mph = strtof(p, &endptr);
    if (endptr == p) return; // no parse

    mph_set(mph);
    ESP_LOGI(TAG, "MPH=%.2f (from '%s')", g_mph, line);
        // If timer exists, restart it so it uses the latest Hz
    if (g_vss_timer) {
        esp_timer_stop(g_vss_timer);
        esp_timer_start_once(g_vss_timer, 1000); // 1ms bootstrap
    }

}

static void rx_feed_bytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];

        // normalize CR to LF
        if (c == '\r') c = '\n';

        if (c == '\n') {
            if (s_line_len > 0) {
                s_line_buf[s_line_len] = '\0';
                parse_line_update_mph(s_line_buf);
                s_line_len = 0;
            }
            continue;
        }

        if (s_line_len < sizeof(s_line_buf) - 1) {
            s_line_buf[s_line_len++] = c;
        } else {
            // overflow -> drop line
            s_line_len = 0;
        }
    }
}

/* ========= Bluetooth callbacks ========= */

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Auth OK: %s", param->auth_cmpl.device_name);
            } else {
                ESP_LOGW(TAG, "Auth failed: status=%d", param->auth_cmpl.stat);
            }
            break;

        default:
            break;
    }
}

static void spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
        case ESP_SPP_INIT_EVT:
            ESP_LOGI(TAG, "SPP init");
            // Start SPP server (no security for now; we can tighten later)
            esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, SPP_SERVER_NAME);
            break;

        case ESP_SPP_START_EVT:
            ESP_LOGI(TAG, "SPP server started: %s", SPP_SERVER_NAME);
            break;

        case ESP_SPP_SRV_OPEN_EVT:
            ESP_LOGI(TAG, "Client connected");
            s_spp_handle = param->srv_open.handle;
            break;

        case ESP_SPP_CLOSE_EVT:
            ESP_LOGI(TAG, "Client disconnected");
            s_spp_handle = 0;
            s_line_len = 0;
            break;

        case ESP_SPP_DATA_IND_EVT:
            // bytes received from phone
            rx_feed_bytes(param->data_ind.data, param->data_ind.len);
            break;

        default:
            break;
    }
}

/* ========= Init ========= */

static void vss_gpio_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << VSS_GPIO),
        .mode = GPIO_MODE_OUTPUT,        // PUSH-PULL output (driving NPN base)
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    // idle state: base LOW -> transistor OFF -> VSS line released
    gpio_set_level(VSS_GPIO, 0);
}

static void vss_timer_cb(void *arg)
{
    (void)arg;

    const int64_t now = esp_timer_get_time();
    const int64_t age_ms = (now - g_last_mph_us) / 1000;

     // stale data or zero speed -> stop pulsing, turn transistor OFF, release line
    if (age_ms > MPH_TIMEOUT_MS || g_target_hz <= 0.1f) {
        g_vss_level_low = false;
        gpio_set_level(VSS_GPIO, 0); // base LOW -> transistor OFF -> release
        esp_timer_stop(g_vss_timer);
        return;
    }

    // Toggle output level each tick (50% duty)
    // g_vss_level_low == "sink ON" phase
    g_vss_level_low = !g_vss_level_low;

    // NPN inverts: base HIGH -> collector LOW (sink), base LOW -> release
    gpio_set_level(VSS_GPIO, g_vss_level_low ? 1 : 0);

    // Compute half-period (us) for current target Hz
    float hz = g_target_hz;
    if (hz < 0.1f) hz = 0.1f;

    int64_t half_period_us = (int64_t)(1000000.0f / (2.0f * hz));

    // clamps
    if (half_period_us < 200) half_period_us = 200;
    if (half_period_us > 500000) half_period_us = 500000;

    // Re-arm one-shot timer with new interval
    esp_timer_start_once(g_vss_timer, half_period_us);
}

static void vss_timer_init(void)
{
    const esp_timer_create_args_t args = {
        .callback = &vss_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vss_pulse"
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &g_vss_timer));
}

static void bt_init(void)
{
    // NVS needed for BT stack
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // We only want Classic BT for SPP (no BLE)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_spp_register_callback(spp_cb));
    ESP_ERROR_CHECK(esp_spp_init(ESP_SPP_MODE_CB));

    esp_bt_dev_set_device_name(BT_DEVICE_NAME);

    // discoverable + connectable
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));

    ESP_LOGI(TAG, "BT ready. Name='%s'", BT_DEVICE_NAME);
}

void app_main(void)
{
    bt_init();
    vss_gpio_init();
    vss_timer_init();

    // Basic heartbeat until we bolt on pulse output
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "Heartbeat | connected=%s | MPH=%.2f",
                s_spp_handle ? "yes" : "no",
                g_mph);
    }
}
