#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "driver/gpio.h"

#include "dht.h"   // composant esp-idf-lib/dht

#define WIFI_SSID         CONFIG_TP_WIFI_SSID
#define WIFI_PASS         CONFIG_TP_WIFI_PASS
#define TS_WRITE_API_KEY  CONFIG_TP_TS_API_KEY
#define DHT_GPIO          CONFIG_TP_DHT_GPIO
#define POST_PERIOD_S     CONFIG_TP_POST_PERIOD_S

static const char *TAG = "TP";

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

/* ===== Wi-Fi Station ===== */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi deconnecte, reconnexion...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connexion Wi-Fi a %s...", WIFI_SSID);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
}

/* ===== Lecture DHT11 via esp-idf-lib/dht ===== */
static bool dht11_read(float *t_c, float *h)
{
    dht_sensor_type_t type = DHT_TYPE_DHT11;   /* DHT22 : DHT_TYPE_AM2301 */
    float temperature = 0, humidity = 0;

    esp_err_t res = dht_read_float_data(type, DHT_GPIO, &humidity, &temperature);
    if (res == ESP_OK) {
        if (t_c) *t_c = temperature;
        if (h)   *h   = humidity;
        return true;
    } else {
        ESP_LOGW(TAG, "DHT lecture echouee: %s", esp_err_to_name(res));
        return false;
    }
}

/* ===== HTTP POST vers ThingSpeak ===== */
typedef struct {
    char buf[16];
    int  len;
} http_resp_t;

/* Recupere le corps de la reponse pendant esp_http_client_perform() */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
        http_resp_t *r = (http_resp_t *)evt->user_data;
        int room = (int)sizeof(r->buf) - 1 - r->len;
        int n = evt->data_len < room ? evt->data_len : room;
        if (n > 0) {
            memcpy(r->buf + r->len, evt->data, n);
            r->len += n;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

static bool thingspeak_post(float t_c, float h)
{
    char post_data[160];
    snprintf(post_data, sizeof(post_data),
             "api_key=%s&field1=%.1f&field2=%.1f", TS_WRITE_API_KEY, t_c, h);

    http_resp_t resp = { .len = 0 };
    resp.buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = "http://api.thingspeak.com/update",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
        .event_handler = http_event_handler,
        .user_data = &resp,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "esp_http_client_init a echoue");
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP %d, body='%s'", status, resp.buf);
        esp_http_client_cleanup(client);
        /* ThingSpeak renvoie l'ID de l'entree (> 0) si OK, 0 sinon */
        return (status == 200) && (atoi(resp.buf) > 0);
    } else {
        ESP_LOGE(TAG, "HTTP erreur: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }
}

void app_main(void)
{
    /* NVS requis pour le Wi-Fi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    wifi_init_sta();   

    while (1) {
        float t = 0.0f, h = 0.0f;
        if (dht11_read(&t, &h)) {
            ESP_LOGI(TAG, "Temperature: %.1f C, Humidite: %.1f %%", t, h);
            bool ok = thingspeak_post(t, h);
            if (ok) ESP_LOGI(TAG, "Publication ThingSpeak OK");
            else    ESP_LOGW(TAG, "Publication ThingSpeak KO");
        }
        vTaskDelay(pdMS_TO_TICKS(POST_PERIOD_S * 1000));
    }
}