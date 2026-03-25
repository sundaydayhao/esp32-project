/**
 * @file web_server.c
 * @brief HTTP Web 服务器实现
 */
#include "web_server.h"
#include "config.h"
#include "sensors/sensor_manager.h"
#include "alarm/alarm_manager.h"
#include "storage/data_logger.h"
#include "ota/ota_manager.h"
#include "mqtt_handler.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static httpd_handle_t s_server = NULL;

/* ================================================================
 * 辅助函数
 * ================================================================ */
static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return httpd_resp_send_500(req);
    esp_err_t ret = httpd_resp_sendstr(req, json_str);
    free(json_str);
    return ret;
}

static cJSON *env_data_to_json(const env_data_t *d)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "ts",          (double)d->unix_time);
    cJSON_AddNumberToObject(root, "temperature", (double)d->temperature);
    cJSON_AddNumberToObject(root, "humidity",    (double)d->humidity);
    cJSON_AddNumberToObject(root, "pressure",    (double)d->pressure);
    cJSON_AddNumberToObject(root, "altitude",    (double)d->altitude);
    cJSON_AddNumberToObject(root, "co2_ppm",     (double)d->co2_ppm);
    cJSON_AddNumberToObject(root, "pm1_0",       (double)d->pm1_0);
    cJSON_AddNumberToObject(root, "pm2_5",       (double)d->pm2_5);
    cJSON_AddNumberToObject(root, "pm10",        (double)d->pm10);
    cJSON_AddNumberToObject(root, "aqi_level",   (double)d->aqi_level);
    cJSON *health = cJSON_AddObjectToObject(root, "health");
    cJSON_AddNumberToObject(health, "sht31",   d->status_sht31);
    cJSON_AddNumberToObject(health, "bme280",  d->status_bme280);
    cJSON_AddNumberToObject(health, "mhz19",   d->status_mhz19);
    cJSON_AddNumberToObject(health, "pms5003", d->status_pms5003);
    return root;
}

/* ================================================================
 * GET /api/data
 * ================================================================ */
static esp_err_t handler_get_data(httpd_req_t *req)
{
    env_data_t data;
    sensor_manager_get_latest(&data);
    return send_json(req, env_data_to_json(&data));
}

/* ================================================================
 * GET /api/history?n=N
 * ================================================================ */
static esp_err_t handler_get_history(httpd_req_t *req)
{
    char param[16] = "60";
    httpd_req_get_url_query_str(req, param, sizeof(param));
    char val[8] = "60";
    httpd_query_key_value(param, "n", val, sizeof(val));
    uint32_t n = (uint32_t)atoi(val);
    if (n == 0 || n > 1440) n = 60;

    log_record_t *buf = malloc(n * sizeof(log_record_t));
    if (!buf) return httpd_resp_send_500(req);

    uint32_t actual = 0;
    data_logger_read_latest(buf, n, &actual);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total", data_logger_get_count());
    cJSON_AddNumberToObject(root, "count", actual);
    cJSON *arr = cJSON_AddArrayToObject(root, "records");

    for (uint32_t i = 0; i < actual; i++) {
        cJSON *rec = cJSON_CreateObject();
        cJSON_AddNumberToObject(rec, "ts",   (double)buf[i].unix_time);
        cJSON_AddNumberToObject(rec, "t",    (double)buf[i].temperature / 100.0);
        cJSON_AddNumberToObject(rec, "rh",   (double)buf[i].humidity / 100.0);
        cJSON_AddNumberToObject(rec, "p",    (double)buf[i].pressure);
        cJSON_AddNumberToObject(rec, "co2",  (double)buf[i].co2_ppm);
        cJSON_AddNumberToObject(rec, "pm25", (double)buf[i].pm2_5);
        cJSON_AddNumberToObject(rec, "pm10", (double)buf[i].pm10);
        cJSON_AddNumberToObject(rec, "aqi",  (double)buf[i].aqi_level);
        cJSON_AddItemToArray(arr, rec);
    }
    free(buf);
    return send_json(req, root);
}

/* ================================================================
 * GET /api/alarms
 * ================================================================ */
static esp_err_t handler_get_alarms(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    uint32_t bits = alarm_manager_get_active_bits();
    cJSON_AddNumberToObject(root, "active_bits", (double)bits);
    cJSON_AddBoolToObject(root, "any_active", bits != 0);

    alarm_thresholds_t thresh;
    alarm_manager_get_thresholds(&thresh);
    cJSON *t = cJSON_AddObjectToObject(root, "thresholds");
    cJSON_AddNumberToObject(t, "temp_high",     thresh.temp_high);
    cJSON_AddNumberToObject(t, "temp_low",      thresh.temp_low);
    cJSON_AddNumberToObject(t, "humi_high",     thresh.humi_high);
    cJSON_AddNumberToObject(t, "humi_low",      thresh.humi_low);
    cJSON_AddNumberToObject(t, "co2_high",      thresh.co2_high);
    cJSON_AddNumberToObject(t, "pm25_high",     thresh.pm25_high);
    cJSON_AddNumberToObject(t, "pm10_high",     thresh.pm10_high);
    cJSON_AddNumberToObject(t, "pressure_high", thresh.pressure_high);
    cJSON_AddNumberToObject(t, "pressure_low",  thresh.pressure_low);
    return send_json(req, root);
}

/* ================================================================
 * POST /api/thresholds
 * ================================================================ */
static esp_err_t handler_set_thresholds(httpd_req_t *req)
{
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request body");
    }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    alarm_thresholds_t thresh;
    alarm_manager_get_thresholds(&thresh);

#define GET_FLOAT(key, field) do { \
    cJSON *item = cJSON_GetObjectItem(root, key); \
    if (item && cJSON_IsNumber(item)) thresh.field = (float)item->valuedouble; \
} while(0)

    GET_FLOAT("temp_high",     temp_high);
    GET_FLOAT("temp_low",      temp_low);
    GET_FLOAT("humi_high",     humi_high);
    GET_FLOAT("humi_low",      humi_low);
    GET_FLOAT("co2_high",      co2_high);
    GET_FLOAT("pm25_high",     pm25_high);
    GET_FLOAT("pm10_high",     pm10_high);
    GET_FLOAT("pressure_high", pressure_high);
    GET_FLOAT("pressure_low",  pressure_low);
#undef GET_FLOAT

    cJSON_Delete(root);
    alarm_manager_set_thresholds(&thresh);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    return send_json(req, resp);
}

/* ================================================================
 * POST /api/ota
 * ================================================================ */
static esp_err_t handler_ota(httpd_req_t *req)
{
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid request body");
    }
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }

    cJSON *url_item = cJSON_GetObjectItem(root, "url");
    if (!url_item || !cJSON_IsString(url_item)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing url");
    }

    esp_err_t ret = ota_manager_start(url_item->valuestring);
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", ret == ESP_OK);
    if (ret != ESP_OK)
        cJSON_AddStringToObject(resp, "error", esp_err_to_name(ret));
    return send_json(req, resp);
}

/* ================================================================
 * POST /api/reboot
 * ================================================================ */
static esp_err_t handler_reboot(httpd_req_t *req)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "Rebooting in 1s...");
    send_json(req, resp);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

/* ================================================================
 * GET / — 内嵌 HTML 仪表盘
 * ================================================================ */
static const char DASHBOARD_HTML[] =
"<!DOCTYPE html><html lang='zh-CN'><head>"
"<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>IEM-3200 环境监测</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:Arial,sans-serif;background:#0d1117;color:#e6edf3;min-height:100vh}"
".header{background:#161b22;padding:16px 24px;border-bottom:1px solid #30363d;display:flex;align-items:center;gap:12px}"
".header h1{font-size:1.2rem;color:#58a6ff}"
".status-dot{width:10px;height:10px;border-radius:50%;background:#3fb950;animation:pulse 2s infinite}"
"@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:16px;padding:24px}"
".card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px}"
".card-title{font-size:.75rem;color:#8b949e;text-transform:uppercase;letter-spacing:.05em;margin-bottom:8px}"
".card-value{font-size:2rem;font-weight:700;color:#e6edf3}"
".card-unit{font-size:.9rem;color:#8b949e;margin-left:4px}"
".card-sub{font-size:.8rem;color:#8b949e;margin-top:4px}"
".alarm{border-color:#f85149!important;background:#1a0a0a!important}"
".alarm .card-value{color:#f85149}"
".aqi-0{color:#3fb950}.aqi-1{color:#a8d8a8}.aqi-2{color:#e3b341}.aqi-3{color:#f0883e}.aqi-4{color:#f85149}.aqi-5{color:#8b0000}"
".chart-container{padding:0 24px 24px}"
".chart-box{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px}"
".chart-box h3{font-size:.9rem;color:#8b949e;margin-bottom:12px}"
"canvas{width:100%!important;max-height:200px}"
".footer{text-align:center;padding:16px;color:#8b949e;font-size:.75rem;border-top:1px solid #30363d}"
"</style></head><body>"
"<div class='header'><div class='status-dot' id='dot'></div>"
"<h1>IEM-3200 工业环境监测节点</h1>"
"<span id='ts' style='margin-left:auto;font-size:.8rem;color:#8b949e'></span></div>"
"<div class='grid'>"
"<div class='card' id='c-temp'><div class='card-title'>温度</div>"
"<div><span class='card-value' id='v-temp'>--</span><span class='card-unit'>°C</span></div>"
"<div class='card-sub' id='s-temp'>SHT31</div></div>"
"<div class='card' id='c-humi'><div class='card-title'>相对湿度</div>"
"<div><span class='card-value' id='v-humi'>--</span><span class='card-unit'>%RH</span></div></div>"
"<div class='card' id='c-pres'><div class='card-title'>大气压</div>"
"<div><span class='card-value' id='v-pres'>--</span><span class='card-unit'>hPa</span></div>"
"<div class='card-sub' id='s-alt'>海拔: -- m</div></div>"
"<div class='card' id='c-co2'><div class='card-title'>CO₂ 浓度</div>"
"<div><span class='card-value' id='v-co2'>--</span><span class='card-unit'>ppm</span></div></div>"
"<div class='card' id='c-pm25'><div class='card-title'>PM2.5</div>"
"<div><span class='card-value' id='v-pm25'>--</span><span class='card-unit'>µg/m³</span></div></div>"
"<div class='card' id='c-pm10'><div class='card-title'>PM10</div>"
"<div><span class='card-value' id='v-pm10'>--</span><span class='card-unit'>µg/m³</span></div></div>"
"<div class='card' id='c-aqi'><div class='card-title'>AQI 等级</div>"
"<div class='card-value' id='v-aqi'>--</div>"
"<div class='card-sub' id='s-aqi'>--</div></div>"
"<div class='card' id='c-health'><div class='card-title'>传感器状态</div>"
"<div style='font-size:.85rem;line-height:1.8' id='v-health'>--</div></div>"
"</div>"
"<div class='footer'>IEM-3200 &nbsp;|&nbsp; FW v" FW_VERSION_STR " &nbsp;|&nbsp; "
"<a href='/api/data' style='color:#58a6ff'>API</a> &nbsp;|&nbsp; "
"<a href='/api/history?n=60' style='color:#58a6ff'>History</a></div>"
"<script>"
"const AQI=['优','良','轻度污染','中度污染','重度污染','严重污染'];"
"const AQI_CLS=['aqi-0','aqi-1','aqi-2','aqi-3','aqi-4','aqi-5'];"
"const ST=['OK','WARN','ERROR','OFFLINE'];"
"function fmt(v,d=1){return typeof v==='number'?v.toFixed(d):'--';}"
"function alarm(id,on){document.getElementById(id).classList.toggle('alarm',on);}"
"async function refresh(){"
"try{"
"const r=await fetch('/api/data');const d=await r.json();"
"document.getElementById('v-temp').textContent=fmt(d.temperature);"
"document.getElementById('v-humi').textContent=fmt(d.humidity);"
"document.getElementById('v-pres').textContent=fmt(d.pressure);"
"document.getElementById('s-alt').textContent='海拔: '+fmt(d.altitude,0)+' m';"
"document.getElementById('v-co2').textContent=d.co2_ppm||'--';"
"document.getElementById('v-pm25').textContent=d.pm2_5||'--';"
"document.getElementById('v-pm10').textContent=d.pm10||'--';"
"const aqi=d.aqi_level||0;"
"const aqiEl=document.getElementById('v-aqi');"
"aqiEl.textContent=aqi;aqiEl.className='card-value '+AQI_CLS[aqi];"
"document.getElementById('s-aqi').textContent=AQI[aqi]||'--';"
"const h=d.health||{};"
"document.getElementById('v-health').innerHTML="
"'SHT31: '+ST[h.sht31||0]+'<br>BME280: '+ST[h.bme280||0]+"
"'<br>MHZ19: '+ST[h.mhz19||0]+'<br>PMS5003: '+ST[h.pms5003||0];"
"document.getElementById('ts').textContent=new Date(d.ts*1000).toLocaleString();"
"document.getElementById('dot').style.background='#3fb950';"
"}catch(e){document.getElementById('dot').style.background='#f85149';}"
"}"
"refresh();setInterval(refresh,5000);"
"</script></body></html>";

static esp_err_t handler_dashboard(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, DASHBOARD_HTML);
}

/* ================================================================
 * 服务器启动/停止
 * ================================================================ */
esp_err_t web_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = WEB_SERVER_PORT;
    cfg.max_open_sockets = WEB_MAX_CONNECTIONS;
    cfg.lru_purge_enable = true;
    cfg.stack_size       = 8192;

    esp_err_t ret = httpd_start(&s_server, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WEB, "Web 服务器启动失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 注册路由 */
    httpd_uri_t routes[] = {
        { .uri = "/",               .method = HTTP_GET,  .handler = handler_dashboard      },
        { .uri = "/api/data",       .method = HTTP_GET,  .handler = handler_get_data       },
        { .uri = "/api/history",    .method = HTTP_GET,  .handler = handler_get_history    },
        { .uri = "/api/alarms",     .method = HTTP_GET,  .handler = handler_get_alarms     },
        { .uri = "/api/thresholds", .method = HTTP_POST, .handler = handler_set_thresholds },
        { .uri = "/api/ota",        .method = HTTP_POST, .handler = handler_ota            },
        { .uri = "/api/reboot",     .method = HTTP_POST, .handler = handler_reboot         },
    };

    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

    ESP_LOGI(TAG_WEB, "Web 服务器已启动 (port=%d)", WEB_SERVER_PORT);
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}
