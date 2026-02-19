/*
 * Home Assistant REST API client
 *
 * Controls lights and covers via direct HTTP calls to the HA REST API.
 * - Commands: POST /api/services/light/turn_on|turn_off
 *             POST /api/services/cover/open_cover|close_cover|set_cover_position
 * - State sync: GET /api/states/<entity_id> polled every 10s
 */

#include "mqtt_client_app.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "ha_api";

#define HA_BASE_URL  CONFIG_HA_BASE_URL
#define HA_TOKEN     CONFIG_HA_TOKEN

#define POLL_INTERVAL_MS 10000
#define HTTP_BUF_SIZE    1024

static char s_response_buf[HTTP_BUF_SIZE];
static int  s_response_len;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA &&
        !esp_http_client_is_chunked_response(evt->client)) {
        int copy = evt->data_len;
        if (s_response_len + copy >= HTTP_BUF_SIZE - 1)
            copy = HTTP_BUF_SIZE - 1 - s_response_len;
        memcpy(s_response_buf + s_response_len, evt->data, copy);
        s_response_len += copy;
    }
    return ESP_OK;
}

static esp_err_t ha_post(const char *path, const char *body)
{
    char url[128];
    snprintf(url, sizeof(url), "%s%s", HA_BASE_URL, path);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    s_response_len = 0;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    return (err == ESP_OK && status == 200) ? ESP_OK : ESP_FAIL;
}

// ---- Light commands ----

void mqtt_publish_command(const char *entity_id, const char *payload)
{
    const char *service = (strcmp(payload, "ON") == 0) ? "turn_on" : "turn_off";
    char path[64], body[128];
    snprintf(path, sizeof(path), "/api/services/light/%s", service);
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity_id);

    if (ha_post(path, body) == ESP_OK)
        ESP_LOGI(TAG, "%s -> %s OK", entity_id, service);
    else
        ESP_LOGW(TAG, "%s -> %s failed", entity_id, service);
}

void ha_set_light_with_params(const char *entity_id, bool on, int brightness, int color_temp_kelvin)
{
    if (!on) {
        mqtt_publish_command(entity_id, "OFF");
        return;
    }

    char body[192];
    int len = snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"", entity_id);
    if (brightness >= 0)
        len += snprintf(body + len, sizeof(body) - len, ",\"brightness\":%d", brightness);
    if (color_temp_kelvin > 0)
        len += snprintf(body + len, sizeof(body) - len, ",\"color_temp_kelvin\":%d", color_temp_kelvin);
    snprintf(body + len, sizeof(body) - len, "}");

    if (ha_post("/api/services/light/turn_on", body) == ESP_OK)
        ESP_LOGI(TAG, "%s brightness=%d ct=%d OK", entity_id, brightness, color_temp_kelvin);
    else
        ESP_LOGW(TAG, "%s params failed", entity_id);
}

// ---- Cover commands ----

void ha_cover_open(const char *entity_id)
{
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity_id);
    if (ha_post("/api/services/cover/open_cover", body) == ESP_OK)
        ESP_LOGI(TAG, "%s -> open OK", entity_id);
    else
        ESP_LOGW(TAG, "%s -> open failed", entity_id);
}

void ha_cover_close(const char *entity_id)
{
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity_id);
    if (ha_post("/api/services/cover/close_cover", body) == ESP_OK)
        ESP_LOGI(TAG, "%s -> close OK", entity_id);
    else
        ESP_LOGW(TAG, "%s -> close failed", entity_id);
}

void ha_cover_set_position(const char *entity_id, int position)
{
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"position\":%d}", entity_id, position);
    if (ha_post("/api/services/cover/set_cover_position", body) == ESP_OK)
        ESP_LOGI(TAG, "%s -> position=%d OK", entity_id, position);
    else
        ESP_LOGW(TAG, "%s -> set_position failed", entity_id);
}

// ---- Polling ----

static int parse_json_int(const char *buf, const char *key)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(buf, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ' ') p++;
    if (strncmp(p, "null", 4) == 0) return -1;
    return atoi(p);
}

static void poll_light(const char *entity_id)
{
    char url[128];
    snprintf(url, sizeof(url), "%s/api/states/%s", HA_BASE_URL, entity_id);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);

    s_response_len = 0;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) return;
    s_response_buf[s_response_len] = '\0';

    const char *state_key = strstr(s_response_buf, "\"state\":");
    if (!state_key) return;
    const char *val = strchr(state_key + 8, '"');
    if (!val) return;
    val++;
    bool is_on = (strncmp(val, "on", 2) == 0 && val[2] == '"');

    int brightness        = parse_json_int(s_response_buf, "brightness");
    int color_temp_kelvin = parse_json_int(s_response_buf, "color_temp_kelvin");

    if (lvgl_port_lock(100)) {
        ui_update_light_state(entity_id, is_on);
        ui_update_light_params(entity_id, brightness, color_temp_kelvin);
        lvgl_port_unlock();
    }
}

static void poll_cover(const char *entity_id)
{
    char url[128];
    snprintf(url, sizeof(url), "%s/api/states/%s", HA_BASE_URL, entity_id);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Authorization", "Bearer " HA_TOKEN);

    s_response_len = 0;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) return;
    s_response_buf[s_response_len] = '\0';

    int position = parse_json_int(s_response_buf, "current_position");

    if (lvgl_port_lock(100)) {
        ui_update_cover_state(entity_id, position);
        lvgl_port_unlock();
    }
}

static void ha_poll_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    while (1) {
        poll_light("light.guldlampan");
        vTaskDelay(pdMS_TO_TICKS(200));
        poll_light("light.videolampor");
        vTaskDelay(pdMS_TO_TICKS(200));
        poll_light("light.iris_golvlampa");
        vTaskDelay(pdMS_TO_TICKS(200));
        poll_cover("cover.persienn_arbetsrum");
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void mqtt_app_init(void)
{
    ESP_LOGI(TAG, "Starting HA REST API -> %s", HA_BASE_URL);
    xTaskCreate(ha_poll_task, "ha_poll", 4096, NULL, 5, NULL);
}
