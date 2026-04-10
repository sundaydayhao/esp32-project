/**
 * @file wifi_manager.c
 * @brief WiFi manager with automatic STA/AP provisioning flow
 */
#include "wifi_manager.h"

#include <stdbool.h>
#include <string.h>

#include "app_events.h"
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
static esp_timer_handle_t s_reconnect_timer = NULL;
static wifi_state_t s_state = WIFI_STATE_DISCONNECTED;
static int s_retry = 0;
static bool s_wifi_started = false;
static bool s_suppress_disconnect = false;
static bool s_reconnect_pending = false;
static bool s_have_credentials = false;
static wifi_err_reason_t s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
static char s_ip[20] = "0.0.0.0";
static char s_saved_ssid[WIFI_SSID_MAX_LEN] = "";
static char s_saved_pass[WIFI_PASS_MAX_LEN] = "";
static char s_ap_ssid[33] = "";

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
static void wifi_connect_timeout_cb(void *arg);
static void wifi_reconnect_timer_cb(void *arg);
static esp_err_t start_station(void);
static esp_err_t start_provisioning_ap(void);
static esp_err_t configure_provisioning_ap_netif(void);
static esp_err_t load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len,
                                  bool *from_nvs);
static esp_err_t save_credentials(const char *ssid, const char *pass);
static esp_err_t clear_credentials(void);
static void set_state(wifi_state_t state);
static void publish_wifi_state_event(void);
static bool has_text(const char *value);
static void refresh_ip_from_netif(void);

static const char *wifi_reason_to_name(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_BEACON_TIMEOUT:
        return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND:
        return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL:
        return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL:
        return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL:
        return "CONNECTION_FAIL";
    case WIFI_REASON_AP_TSF_RESET:
        return "AP_TSF_RESET";
    case WIFI_REASON_ROAMING:
        return "ROAMING";
    default:
        return "UNKNOWN";
    }
}

static bool wifi_reason_is_auth_failure(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return true;
    default:
        return false;
    }
}

static bool wifi_reason_is_runtime_drop(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_BEACON_TIMEOUT:
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_AP_TSF_RESET:
    case WIFI_REASON_ROAMING:
        return true;
    default:
        return false;
    }
}

static bool wifi_reason_should_fallback_to_provisioning(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_BEACON_TIMEOUT:
    case WIFI_REASON_CONNECTION_FAIL:
        return true;
    default:
        return false;
    }
}

static void stop_connect_timer(void)
{
    if (s_connect_timer) {
        esp_timer_stop(s_connect_timer);
    }
}

static void start_connect_timer(void)
{
    if (!s_connect_timer) {
        return;
    }

    esp_timer_stop(s_connect_timer);
    esp_timer_start_once(s_connect_timer, (uint64_t)WIFI_CONNECT_TIMEOUT_MS * 1000ULL);
}

static void stop_reconnect_timer(void)
{
    if (s_reconnect_timer) {
        esp_timer_stop(s_reconnect_timer);
    }
}

static uint32_t wifi_reconnect_delay_ms(int attempt)
{
    uint32_t delay_ms = WIFI_RECONNECT_DELAY_MS;
    int backoff_steps = attempt - 1;

    if (backoff_steps < 0) {
        backoff_steps = 0;
    }
    if (backoff_steps > 5) {
        backoff_steps = 5;
    }

    while (backoff_steps-- > 0 && delay_ms < WIFI_CONNECT_TIMEOUT_MS) {
        if (delay_ms > WIFI_CONNECT_TIMEOUT_MS / 2U) {
            delay_ms = WIFI_CONNECT_TIMEOUT_MS;
            break;
        }
        delay_ms *= 2U;
    }

    return delay_ms;
}

static void maybe_enter_provisioning_after_retries(void)
{
    if (s_retry < WIFI_MAX_RETRY ||
        !wifi_reason_should_fallback_to_provisioning((uint8_t)s_last_disconnect_reason)) {
        return;
    }

    stop_connect_timer();
    stop_reconnect_timer();
    ESP_LOGW(TAG_WIFI,
             "STA retries reached %d/%d, switching to provisioning (reason=%s/%u)",
             s_retry, WIFI_MAX_RETRY, wifi_reason_to_name(s_last_disconnect_reason),
             s_last_disconnect_reason);
    set_state(WIFI_STATE_FAILED);
    start_provisioning_ap();
}

static void schedule_sta_reconnect(uint8_t reason, const char *source)
{
    uint32_t delay_ms = 0;
    bool auth_failure = false;
    bool runtime_drop = false;

    if (!has_text(s_saved_ssid)) {
        set_state(WIFI_STATE_FAILED);
        start_provisioning_ap();
        return;
    }

    auth_failure = wifi_reason_is_auth_failure(reason);
    runtime_drop = wifi_reason_is_runtime_drop(reason);
    s_last_disconnect_reason = (wifi_err_reason_t)reason;

    if (s_reconnect_pending) {
        ESP_LOGW(TAG_WIFI, "Reconnect already pending, ignore duplicate trigger source=%s reason=%s/%u",
                 source ? source : "unknown", wifi_reason_to_name(reason), reason);
        return;
    }

    s_retry++;

    maybe_enter_provisioning_after_retries();
    if (s_state == WIFI_STATE_CONFIGURING || s_state == WIFI_STATE_FAILED) {
        return;
    }

    delay_ms = wifi_reconnect_delay_ms(s_retry);
    stop_connect_timer();
    stop_reconnect_timer();
    s_reconnect_pending = true;
    set_state(WIFI_STATE_CONNECTING);
    ESP_LOGW(TAG_WIFI,
             "STA reconnect scheduled in %lu ms, attempt %d, source=%s reason=%s/%u%s%s",
             (unsigned long)delay_ms, s_retry, source ? source : "unknown",
             wifi_reason_to_name(reason), reason,
             runtime_drop ? " runtime-drop" : "",
             auth_failure ? " auth-related" : "");
    if (s_reconnect_timer) {
        esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000ULL);
    }
}

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

    publish_wifi_state_event();
}

static void publish_wifi_state_event(void)
{
    app_wifi_event_t event = {
        .state = s_state,
        .connected = s_state == WIFI_STATE_CONNECTED,
        .configuring = s_state == WIFI_STATE_CONFIGURING,
        .has_credentials = s_have_credentials,
        .rssi = 0,
    };

    if (event.connected) {
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            event.rssi = ap.rssi;
        }
    }

    strlcpy(event.ssid, s_saved_ssid, sizeof(event.ssid));
    strlcpy(event.ap_ssid, s_ap_ssid, sizeof(event.ap_ssid));
    if (event.configuring) {
        strlcpy(event.ip, WIFI_PROV_AP_IP, sizeof(event.ip));
    } else {
        strlcpy(event.ip, s_ip, sizeof(event.ip));
    }

    esp_err_t ret = app_events_post(APP_EVENT_WIFI_STATE_CHANGED, &event, sizeof(event));
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG_WIFI, "Post wifi state event failed: %s", esp_err_to_name(ret));
    }
}

static bool has_text(const char *value)
{
    return value && value[0] != '\0';
}

static void refresh_ip_from_netif(void)
{
    esp_netif_ip_info_t ip_info = {0};

    if (!s_sta_netif) {
        return;
    }

    if (esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ip_info.ip));
    }
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

static esp_err_t stop_wifi_if_started(void)
{
    if (!s_wifi_started) {
        return ESP_OK;
    }

    stop_connect_timer();
    stop_reconnect_timer();
    s_reconnect_pending = false;
    s_suppress_disconnect = true;
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED) {
        s_suppress_disconnect = false;
        return ret;
    }

    s_wifi_started = false;
    strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
    return ESP_OK;
}

static esp_err_t load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len,
                                  bool *from_nvs)
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
        return ret;
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG_WIFI, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   &wifi_event_handler, NULL),
                        TAG_WIFI, "register wifi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   &wifi_event_handler, NULL),
                        TAG_WIFI, "register sta got ip handler failed");

    update_ap_ssid();

    if (!s_connect_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = wifi_connect_timeout_cb,
            .name = "wifi_connect_timeout",
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_connect_timer), TAG_WIFI,
                            "create connect timer failed");
    }
    if (!s_reconnect_timer) {
        const esp_timer_create_args_t timer_args = {
            .callback = wifi_reconnect_timer_cb,
            .name = "wifi_reconnect",
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_reconnect_timer), TAG_WIFI,
                            "create reconnect timer failed");
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
    s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
    s_reconnect_pending = false;
    strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
    stop_reconnect_timer();
    set_state(WIFI_STATE_CONNECTING);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG_WIFI, "set wifi mode sta failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG_WIFI,
                        "set wifi sta config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG_WIFI, "start sta wifi failed");
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

    stop_connect_timer();
    stop_reconnect_timer();
    s_reconnect_pending = false;
    ESP_RETURN_ON_ERROR(configure_provisioning_ap_netif(), TAG_WIFI,
                        "configure provisioning ap netif failed");

    set_state(WIFI_STATE_CONFIGURING);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG_WIFI, "set wifi mode ap failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), TAG_WIFI,
                        "set wifi ap config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG_WIFI, "start ap wifi failed");
    s_wifi_started = true;
    s_suppress_disconnect = false;

    ESP_LOGW(TAG_WIFI, "Provisioning AP started: SSID=%s URL=http://" WIFI_PROV_AP_IP "/",
             s_ap_ssid);
    return ESP_OK;
}

static void wifi_connect_timeout_cb(void *arg)
{
    (void)arg;

    if (s_state != WIFI_STATE_CONNECTING) {
        return;
    }

    ESP_LOGW(TAG_WIFI, "WiFi connect timeout while connecting to %s", s_saved_ssid);
    schedule_sta_reconnect((uint8_t)s_last_disconnect_reason, "connect-timeout");
}

static void wifi_reconnect_timer_cb(void *arg)
{
    esp_err_t ret = ESP_OK;

    (void)arg;

    if (s_state == WIFI_STATE_CONFIGURING || s_suppress_disconnect || !has_text(s_saved_ssid)) {
        return;
    }

    s_reconnect_pending = false;
    set_state(WIFI_STATE_CONNECTING);
    start_connect_timer();
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_WIFI, "esp_wifi_connect failed during retry: %s", esp_err_to_name(ret));
        schedule_sta_reconnect((uint8_t)s_last_disconnect_reason, "connect-call-failed");
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        set_state(WIFI_STATE_CONNECTING);
        start_connect_timer();
        stop_reconnect_timer();
        s_reconnect_pending = false;
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)data;

        strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
        if (s_state == WIFI_STATE_CONFIGURING || s_suppress_disconnect) {
            return;
        }

        schedule_sta_reconnect(evt ? evt->reason : WIFI_REASON_UNSPECIFIED, "sta-disconnected");
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;

        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry = 0;
        s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
        s_reconnect_pending = false;
        stop_connect_timer();
        stop_reconnect_timer();
        set_state(WIFI_STATE_CONNECTED);
        ESP_LOGI(TAG_WIFI, "Connected, IP: %s", s_ip);
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
    if (s_state == WIFI_STATE_CONNECTED) {
        refresh_ip_from_netif();
    }
    return s_state;
}

bool wifi_manager_is_connected(void)
{
    return wifi_manager_get_state() == WIFI_STATE_CONNECTED;
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
    if (strcmp(s_ip, "0.0.0.0") == 0) {
        refresh_ip_from_netif();
    }

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

    ESP_RETURN_ON_ERROR(save_credentials(ssid, pass ? pass : ""), TAG_WIFI,
                        "save wifi credentials failed");
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
