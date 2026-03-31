/**
 * @file wifi_manager.c
 * @brief WiFi manager with automatic STA/AP provisioning flow
 */
#include "wifi_manager.h"

#include <stdbool.h>
#include <string.h>

#include "config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_CONFIG_BIT     BIT2

static EventGroupHandle_t s_evt_grp = NULL;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static esp_timer_handle_t s_connect_timer = NULL;
static wifi_state_t s_state = WIFI_STATE_DISCONNECTED;
static int s_retry = 0;
static bool s_wifi_started = false;
static bool s_suppress_disconnect = false;
static bool s_have_credentials = false;
static char s_ip[20] = "0.0.0.0";
static char s_ap_client_ip[20] = "0.0.0.0";
static char s_saved_ssid[WIFI_SSID_MAX_LEN] = "";
static char s_saved_pass[WIFI_PASS_MAX_LEN] = "";
static char s_ap_ssid[33] = "";
static uint8_t s_ap_client_mac[6] = {0};

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
static void wifi_connect_timeout_cb(void *arg);
static esp_err_t start_station(void);
static esp_err_t start_provisioning_ap(void);
static esp_err_t configure_provisioning_ap_netif(void);
static esp_err_t load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len, bool *from_nvs);
static esp_err_t save_credentials(const char *ssid, const char *pass);
static esp_err_t clear_credentials(void);

static void set_state(wifi_state_t state)
{
    s_state = state;

    if (!s_evt_grp) {
        return;
    }

    switch (state) {
    case WIFI_STATE_CONNECTED:
        xEventGroupClearBits(s_evt_grp, WIFI_FAIL_BIT | WIFI_CONFIG_BIT);
        xEventGroupSetBits(s_evt_grp, WIFI_CONNECTED_BIT);
        break;
    case WIFI_STATE_CONFIGURING:
        xEventGroupClearBits(s_evt_grp, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        xEventGroupSetBits(s_evt_grp, WIFI_CONFIG_BIT);
        break;
    case WIFI_STATE_FAILED:
        xEventGroupClearBits(s_evt_grp, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_evt_grp, WIFI_FAIL_BIT);
        break;
    default:
        xEventGroupClearBits(s_evt_grp, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_CONFIG_BIT);
        break;
    }
}

static bool has_text(const char *value)
{
    return value && value[0] != '\0';
}

static void update_ap_ssid(void)
{
    uint8_t mac[6] = {0};

    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), WIFI_PROV_AP_SSID_PREFIX "-%02X%02X",
             mac[4], mac[5]);
}

static void update_saved_credentials(const char *ssid, const char *pass)
{
    strlcpy(s_saved_ssid, ssid ? ssid : "", sizeof(s_saved_ssid));
    strlcpy(s_saved_pass, pass ? pass : "", sizeof(s_saved_pass));
    s_have_credentials = has_text(s_saved_ssid);
}

static void clear_ap_client_info(void)
{
    memset(s_ap_client_mac, 0, sizeof(s_ap_client_mac));
    strlcpy(s_ap_client_ip, "0.0.0.0", sizeof(s_ap_client_ip));
}

static esp_err_t stop_wifi_if_started(void)
{
    if (!s_wifi_started) {
        return ESP_OK;
    }

    s_suppress_disconnect = true;
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        s_suppress_disconnect = false;
        return ret;
    }

    s_wifi_started = false;
    strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
    clear_ap_client_info();
    return ESP_OK;
}

static esp_err_t load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len, bool *from_nvs)
{
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    nvs_handle_t handle = 0;
    bool nvs_loaded = false;

    if (ssid && ssid_len > 0) {
        strlcpy(ssid, "", ssid_len);
    }
    if (pass && pass_len > 0) {
        strlcpy(pass, "", pass_len);
    }
    if (from_nvs) {
        *from_nvs = false;
    }

    ret = nvs_open(WIFI_NVS_NS, NVS_READONLY, &handle);
    if (ret == ESP_OK) {
        size_t len = ssid_len;
        ret = nvs_get_str(handle, "ssid", ssid, &len);
        if (ret == ESP_OK && has_text(ssid)) {
            len = pass_len;
            ret = nvs_get_str(handle, "pass", pass, &len);
            if (ret == ESP_ERR_NVS_NOT_FOUND) {
                if (pass && pass_len > 0) {
                    strlcpy(pass, "", pass_len);
                }
                ret = ESP_OK;
            }
            if (ret == ESP_OK) {
                nvs_loaded = true;
            }
        }
        nvs_close(handle);
    }

    if (nvs_loaded) {
        if (from_nvs) {
            *from_nvs = true;
        }
        return ESP_OK;
    }

    if (ssid && ssid_len > 0) {
        strlcpy(ssid, CONFIG_WIFI_SSID_DEFAULT, ssid_len);
    }
    if (pass && pass_len > 0) {
        strlcpy(pass, CONFIG_WIFI_PASS_DEFAULT, pass_len);
    }
    return has_text(ssid) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(WIFI_NVS_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(handle, "ssid", ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "pass", pass ? pass : "");
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t clear_credentials(void)
{
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(WIFI_NVS_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_erase_key(handle, "ssid");
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        esp_err_t pass_ret = nvs_erase_key(handle, "pass");
        if (pass_ret != ESP_OK && pass_ret != ESP_ERR_NVS_NOT_FOUND) {
            ret = pass_ret;
        }
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

static esp_err_t configure_wifi_base(void)
{
    if (!s_evt_grp) {
        s_evt_grp = xEventGroupCreate();
        if (!s_evt_grp) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL));

    update_ap_ssid();

    if (!s_connect_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = wifi_connect_timeout_cb,
            .name = "wifi_connect_timeout",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_connect_timer));
    }

    return ESP_OK;
}

static esp_err_t start_station(void)
{
    wifi_config_t wifi_cfg = {0};
    esp_err_t ret = stop_wifi_if_started();

    if (ret != ESP_OK) {
        return ret;
    }
    if (!has_text(s_saved_ssid)) {
        return start_provisioning_ap();
    }

    strlcpy((char *)wifi_cfg.sta.ssid, s_saved_ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, s_saved_pass, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    s_retry = 0;
    strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
    set_state(WIFI_STATE_CONNECTING);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;
    s_suppress_disconnect = false;

    ESP_LOGI(TAG_WIFI, "Starting STA connect to SSID: %s", s_saved_ssid);
    return ESP_OK;
}

static esp_err_t configure_provisioning_ap_netif(void)
{
    esp_netif_ip_info_t ip_info = {0};
    esp_err_t ret = ESP_OK;

    if (!s_ap_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(WIFI_PROV_AP_IP, &ip_info.ip), TAG_WIFI,
                        "parse provisioning ap ip failed");
    ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4(WIFI_PROV_AP_IP, &ip_info.gw), TAG_WIFI,
                        "parse provisioning ap gateway failed");
    ESP_RETURN_ON_ERROR(esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask), TAG_WIFI,
                        "parse provisioning ap netmask failed");

    ret = esp_netif_dhcps_stop(s_ap_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return ret;
    }

    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_ap_netif, &ip_info), TAG_WIFI,
                        "set provisioning ap ip failed");

    ret = esp_netif_dhcps_start(s_ap_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        return ret;
    }

    return ESP_OK;
}

static esp_err_t start_provisioning_ap(void)
{
    wifi_config_t ap_cfg = {0};
    esp_err_t ret = stop_wifi_if_started();

    if (ret != ESP_OK) {
        return ret;
    }

    strlcpy((char *)ap_cfg.ap.ssid, s_ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(s_ap_ssid);
    ap_cfg.ap.channel = WIFI_PROV_AP_CHANNEL;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    if (s_connect_timer) {
        esp_timer_stop(s_connect_timer);
    }
    ESP_RETURN_ON_ERROR(configure_provisioning_ap_netif(), TAG_WIFI,
                        "configure provisioning ap netif failed");
    clear_ap_client_info();

    set_state(WIFI_STATE_CONFIGURING);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;
    s_suppress_disconnect = false;

    ESP_LOGW(TAG_WIFI, "Provisioning AP started: SSID=%s URL=http://" WIFI_PROV_AP_IP "/", s_ap_ssid);
    return ESP_OK;
}

static void wifi_connect_timeout_cb(void *arg)
{
    (void)arg;

    if (s_state != WIFI_STATE_CONNECTING) {
        return;
    }

    ESP_LOGW(TAG_WIFI, "WiFi connect timeout, entering provisioning mode");
    set_state(WIFI_STATE_FAILED);
    start_provisioning_ap();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        set_state(WIFI_STATE_CONNECTING);
        if (s_connect_timer) {
            esp_timer_stop(s_connect_timer);
            esp_timer_start_once(s_connect_timer, (uint64_t)WIFI_CONNECT_TIMEOUT_MS * 1000ULL);
        }
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)data;

        strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
        if (s_state == WIFI_STATE_CONFIGURING || s_suppress_disconnect) {
            return;
        }

        if (s_retry < WIFI_MAX_RETRY && has_text(s_saved_ssid)) {
            s_retry++;
            set_state(WIFI_STATE_CONNECTING);
            ESP_LOGW(TAG_WIFI, "STA disconnected, retry %d/%d reason=%d",
                     s_retry, WIFI_MAX_RETRY, evt ? evt->reason : -1);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG_WIFI, "STA connect failed, switching to provisioning");
            set_state(WIFI_STATE_FAILED);
            start_provisioning_ap();
        }
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t *)data;

        if (evt && memcmp(evt->mac, s_ap_client_mac, sizeof(s_ap_client_mac)) == 0) {
            clear_ap_client_info();
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;

        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry = 0;
        if (s_connect_timer) {
            esp_timer_stop(s_connect_timer);
        }
        set_state(WIFI_STATE_CONNECTED);
        ESP_LOGI(TAG_WIFI, "Connected, IP: %s", s_ip);
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t *evt = (ip_event_ap_staipassigned_t *)data;

        if (!evt) {
            return;
        }

        memcpy(s_ap_client_mac, evt->mac, sizeof(s_ap_client_mac));
        snprintf(s_ap_client_ip, sizeof(s_ap_client_ip), IPSTR, IP2STR(&evt->ip));
        ESP_LOGI(TAG_WIFI, "Provisioning STA " MACSTR " assigned IP %s",
                 MAC2STR(evt->mac), s_ap_client_ip);
        return;
    }
}

esp_err_t wifi_manager_init(void)
{
    char ssid[WIFI_SSID_MAX_LEN] = "";
    char pass[WIFI_PASS_MAX_LEN] = "";
    bool from_nvs = false;
    esp_err_t ret = configure_wifi_base();

    if (ret != ESP_OK) {
        return ret;
    }

    ret = load_credentials(ssid, sizeof(ssid), pass, sizeof(pass), &from_nvs);
    if (ret == ESP_OK) {
        update_saved_credentials(ssid, pass);
        ESP_LOGI(TAG_WIFI, "Loaded WiFi credentials from %s: %s",
                 from_nvs ? "NVS" : "defaults", s_saved_ssid);
        return start_station();
    }

    update_saved_credentials("", "");
    ESP_LOGW(TAG_WIFI, "No WiFi credentials found, entering provisioning mode");
    return start_provisioning_ap();
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_state;
}

bool wifi_manager_is_connected(void)
{
    return s_state == WIFI_STATE_CONNECTED;
}

bool wifi_manager_is_config_mode(void)
{
    return s_state == WIFI_STATE_CONFIGURING;
}

bool wifi_manager_has_credentials(void)
{
    return s_have_credentials;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_evt_grp,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_CONFIG_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

void wifi_manager_get_ip(char *buf, size_t len)
{
    if (buf && len > 0) {
        strlcpy(buf, s_ip, len);
    }
}

void wifi_manager_get_ap_ssid(char *buf, size_t len)
{
    if (buf && len > 0) {
        strlcpy(buf, s_ap_ssid, len);
    }
}

void wifi_manager_get_ap_client_ip(char *buf, size_t len)
{
    if (buf && len > 0) {
        strlcpy(buf, s_ap_client_ip, len);
    }
}

void wifi_manager_get_config_url(char *buf, size_t len)
{
    if (buf && len > 0) {
        strlcpy(buf, "http://" WIFI_PROV_AP_IP "/", len);
    }
}

void wifi_manager_get_saved_ssid(char *buf, size_t len)
{
    if (buf && len > 0) {
        strlcpy(buf, s_saved_ssid, len);
    }
}

int8_t wifi_manager_get_rssi(void)
{
    wifi_ap_record_t ap = {0};

    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

esp_err_t wifi_manager_connect_with_credentials(const char *ssid, const char *pass)
{
    if (!has_text(ssid)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(save_credentials(ssid, pass ? pass : ""), TAG_WIFI, "save wifi credentials failed");
    update_saved_credentials(ssid, pass ? pass : "");
    ESP_LOGI(TAG_WIFI, "Stored WiFi credentials, reconnecting to %s", s_saved_ssid);
    return start_station();
}

esp_err_t wifi_manager_reset_credentials_and_start_provisioning(void)
{
    ESP_RETURN_ON_ERROR(clear_credentials(), TAG_WIFI, "clear wifi credentials failed");
    update_saved_credentials("", "");
    ESP_LOGW(TAG_WIFI, "WiFi credentials cleared, entering provisioning mode");
    return start_provisioning_ap();
}
