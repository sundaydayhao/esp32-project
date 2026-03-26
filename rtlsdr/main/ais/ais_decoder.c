/**
 * @file ais_decoder.c
 * @brief AIS (Automatic Identification System) 船舶自动识别系统解码器
 *
 * 工作原理：
 *  1. RTL-SDR 调谐到 161.975 MHz（CH 87B）或 162.025 MHz（CH 88B）
 *  2. 以 250 kHz 采样率获取 IQ 数据
 *  3. FM 解调（GMSK 可近似为 FM）
 *  4. 9600 baud 时钟恢复与采样
 *  5. NRZI 解码
 *  6. HDLC 帧提取（标志字节 0x7E）
 *  7. 比特去填充（5个连续1后去掉0）
 *  8. CRC-16/CRC-CCITT 校验
 *  9. AIS 载荷解码（6-bit ASCII）
 * 10. 消息类型解析（Type 1/2/3/5/18/24）
 */

#include "ais_decoder.h"
#include "rtlsdr_usb.h"
#include "config.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "ais_decoder";

// ---------------------------------------------------------------------------
// 内部状态
// ---------------------------------------------------------------------------
static vessel_t     s_vessels[AIS_MAX_VESSELS];
static int          s_vessel_count = 0;
static ais_stats_t  s_stats        = {0};
static SemaphoreHandle_t s_mutex   = NULL;
static bool         s_running      = false;
static TaskHandle_t s_task_handle  = NULL;

// WebSocket 客户端
static httpd_handle_t s_server     = NULL;
static int  s_ws_clients[4];
static int  s_ws_count = 0;

// ---------------------------------------------------------------------------
// HDLC / NRZI 解码器状态
// ---------------------------------------------------------------------------
#define HDLC_MAX_FRAME  256   // 最大帧字节数

typedef struct {
    uint8_t  buf[HDLC_MAX_FRAME];
    int      bit_pos;         // 当前比特位置（0-7）
    int      byte_pos;        // 当前字节位置
    int      ones_count;      // 连续1计数（用于去填充）
    bool     in_frame;        // 是否在帧内
    uint8_t  flag_bits;       // 标志字节检测移位寄存器
    uint8_t  prev_bit;        // 上一比特（NRZI）
} hdlc_state_t;

// ---------------------------------------------------------------------------
// CRC-16/CRC-CCITT (多项式 0x1021，初值 0xFFFF)
// ---------------------------------------------------------------------------
static uint16_t crc16_ccitt(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else              crc <<= 1;
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// 6-bit ASCII 解码（AIS 载荷字符集）
// ---------------------------------------------------------------------------
static uint8_t ais_char_to_bits(char c)
{
    uint8_t v = (uint8_t)c - 48;
    if (v > 39) v -= 8;
    return v & 0x3F;
}

// 从比特流中提取 n 位无符号整数（MSB first）
static uint32_t ais_get_uint(const uint8_t *bits, int start, int len)
{
    uint32_t val = 0;
    for (int i = 0; i < len; i++) {
        val = (val << 1) | bits[start + i];
    }
    return val;
}

// 从比特流中提取 n 位有符号整数
static int32_t ais_get_int(const uint8_t *bits, int start, int len)
{
    uint32_t val = ais_get_uint(bits, start, len);
    if (val & (1u << (len - 1))) {
        val |= ~((1u << len) - 1);  // 符号扩展
    }
    return (int32_t)val;
}

// 从比特流中提取字符串（6-bit ASCII）
static void ais_get_str(const uint8_t *bits, int start, int len_bits, char *out)
{
    int chars = len_bits / 6;
    for (int i = 0; i < chars; i++) {
        uint8_t c = (uint8_t)ais_get_uint(bits, start + i * 6, 6);
        if (c < 32) c += 64;
        out[i] = (char)c;
    }
    // 去除尾部空格
    for (int i = chars - 1; i >= 0 && out[i] == ' '; i--) out[i] = '\0';
    out[chars] = '\0';
}

// ---------------------------------------------------------------------------
// 查找或创建船只记录
// ---------------------------------------------------------------------------
static vessel_t *find_or_create_vessel(uint32_t mmsi)
{
    // 查找已有记录
    for (int i = 0; i < s_vessel_count; i++) {
        if (s_vessels[i].mmsi == mmsi) return &s_vessels[i];
    }
    // 新建记录
    if (s_vessel_count < AIS_MAX_VESSELS) {
        vessel_t *v = &s_vessels[s_vessel_count++];
        memset(v, 0, sizeof(vessel_t));
        v->mmsi    = mmsi;
        v->heading = 511;
        v->rot     = -128;
        return v;
    }
    // 替换最旧的记录
    uint32_t oldest_ts = UINT32_MAX;
    int oldest_idx = 0;
    for (int i = 0; i < AIS_MAX_VESSELS; i++) {
        if (s_vessels[i].last_seen_ms < oldest_ts) {
            oldest_ts  = s_vessels[i].last_seen_ms;
            oldest_idx = i;
        }
    }
    memset(&s_vessels[oldest_idx], 0, sizeof(vessel_t));
    s_vessels[oldest_idx].mmsi    = mmsi;
    s_vessels[oldest_idx].heading = 511;
    s_vessels[oldest_idx].rot     = -128;
    return &s_vessels[oldest_idx];
}

// ---------------------------------------------------------------------------
// AIS 消息解析
// ---------------------------------------------------------------------------
static void parse_ais_message(const uint8_t *bits, int bit_len)
{
    if (bit_len < 38) return;

    uint8_t  msg_type = (uint8_t)ais_get_uint(bits, 0, 6);
    uint32_t mmsi     = ais_get_uint(bits, 8, 30);

    if (mmsi == 0) return;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    vessel_t *v = find_or_create_vessel(mmsi);
    v->last_seen_ms  = now_ms;
    v->msg_count++;
    v->last_msg_type = msg_type;
    xSemaphoreGive(s_mutex);

    switch (msg_type) {
    case 1: case 2: case 3:
        // 位置报告 Class A
        if (bit_len < 168) break;
        {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            v->nav_status = (uint8_t)ais_get_uint(bits, 38, 4);
            v->rot        = (int8_t)ais_get_int(bits, 42, 8);
            v->sog        = ais_get_uint(bits, 50, 10) / 10.0f;
            int32_t lon_raw = ais_get_int(bits, 61, 28);
            int32_t lat_raw = ais_get_int(bits, 89, 27);
            if (lon_raw != 0x6791AC0 && lat_raw != 0x3412140) {
                v->lon      = lon_raw / 600000.0f;
                v->lat      = lat_raw / 600000.0f;
                v->pos_valid = (v->lat >= -90 && v->lat <= 90 &&
                                v->lon >= -180 && v->lon <= 180);
            }
            v->cog     = ais_get_uint(bits, 116, 12) / 10.0f;
            v->heading = (uint16_t)ais_get_uint(bits, 128, 9);
            xSemaphoreGive(s_mutex);
        }
        break;

    case 5:
        // 静态和航次数据 Class A
        if (bit_len < 426) break;
        {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            v->imo       = ais_get_uint(bits, 40, 30);
            ais_get_str(bits, 70,  42, v->callsign);
            ais_get_str(bits, 112, 120, v->name);
            v->ship_type = (uint8_t)ais_get_uint(bits, 232, 8);
            v->dim_bow      = (uint16_t)ais_get_uint(bits, 240, 9);
            v->dim_stern    = (uint16_t)ais_get_uint(bits, 249, 9);
            v->dim_port     = (uint8_t)ais_get_uint(bits, 258, 6);
            v->dim_starboard= (uint8_t)ais_get_uint(bits, 264, 6);
            ais_get_str(bits, 302, 120, v->destination);
            xSemaphoreGive(s_mutex);
        }
        break;

    case 18:
        // 标准 Class B 位置报告
        if (bit_len < 168) break;
        {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            v->sog = ais_get_uint(bits, 46, 10) / 10.0f;
            int32_t lon_raw = ais_get_int(bits, 57, 28);
            int32_t lat_raw = ais_get_int(bits, 85, 27);
            if (lon_raw != 0x6791AC0 && lat_raw != 0x3412140) {
                v->lon      = lon_raw / 600000.0f;
                v->lat      = lat_raw / 600000.0f;
                v->pos_valid = (v->lat >= -90 && v->lat <= 90 &&
                                v->lon >= -180 && v->lon <= 180);
            }
            v->cog     = ais_get_uint(bits, 112, 12) / 10.0f;
            v->heading = (uint16_t)ais_get_uint(bits, 124, 9);
            xSemaphoreGive(s_mutex);
        }
        break;

    case 24:
        // Class B 静态数据
        {
            uint8_t part = (uint8_t)ais_get_uint(bits, 38, 2);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if (part == 0 && bit_len >= 160) {
                ais_get_str(bits, 40, 120, v->name);
            } else if (part == 1 && bit_len >= 168) {
                v->ship_type = (uint8_t)ais_get_uint(bits, 40, 8);
                ais_get_str(bits, 90, 42, v->callsign);
            }
            xSemaphoreGive(s_mutex);
        }
        break;

    default:
        break;
    }

    ESP_LOGD(TAG, "AIS Type%d MMSI=%u", msg_type, mmsi);
}

// ---------------------------------------------------------------------------
// NMEA AIVDM/AIVDO 句子解析
// ---------------------------------------------------------------------------
static void parse_nmea_sentence(const char *sentence)
{
    // 格式: !AIVDM,1,1,,A,<payload>,0*XX
    if (strncmp(sentence, "!AIVDM", 6) != 0 &&
        strncmp(sentence, "!AIVDO", 6) != 0) return;

    // 找到载荷字段（第5个逗号后）
    const char *p = sentence;
    int commas = 0;
    while (*p && commas < 5) {
        if (*p++ == ',') commas++;
    }
    if (commas < 5) return;

    // 提取载荷
    const char *payload_start = p;
    const char *payload_end   = strchr(p, ',');
    if (!payload_end) payload_end = strchr(p, '*');
    if (!payload_end) return;

    int payload_len = payload_end - payload_start;
    if (payload_len < 1 || payload_len > 56) return;

    // 将 6-bit ASCII 载荷转换为比特流
    uint8_t bits[56 * 6];
    int bit_count = 0;
    for (int i = 0; i < payload_len; i++) {
        uint8_t v = ais_char_to_bits(payload_start[i]);
        for (int b = 5; b >= 0; b--) {
            bits[bit_count++] = (v >> b) & 1;
        }
    }

    s_stats.valid_frames++;
    parse_ais_message(bits, bit_count);
}

// ---------------------------------------------------------------------------
// GMSK/FM 解调 + HDLC 帧提取
// ---------------------------------------------------------------------------

// 简单 FM 解调：计算相邻 IQ 样本的相位差
static float fm_demod(int8_t i0, int8_t q0, int8_t i1, int8_t q1)
{
    // phase_diff = atan2(I0*Q1 - I1*Q0, I0*I1 + Q0*Q1)
    float cross = (float)i0 * q1 - (float)i1 * q0;
    float dot   = (float)i0 * i1 + (float)q0 * q1;
    return atan2f(cross, dot);
}

// HDLC 帧处理
static void hdlc_process_bit(hdlc_state_t *h, uint8_t bit, bool ch_b)
{
    // NRZI 解码：0 = 跳变，1 = 不变
    uint8_t decoded = (bit == h->prev_bit) ? 1 : 0;
    h->prev_bit = bit;

    // 标志字节检测（0x7E = 01111110）
    h->flag_bits = (h->flag_bits << 1) | decoded;

    if (h->flag_bits == 0x7E) {
        if (h->in_frame && h->byte_pos >= 2) {
            // 帧结束：校验 CRC
            int frame_bytes = h->byte_pos;
            if (frame_bytes >= 4) {
                uint16_t crc_calc = crc16_ccitt(h->buf, frame_bytes - 2);
                uint16_t crc_recv = (uint16_t)h->buf[frame_bytes - 2] |
                                    ((uint16_t)h->buf[frame_bytes - 1] << 8);
                if (crc_calc == crc_recv) {
                    // 有效帧：构造 NMEA 句子并解析
                    // 将字节流转换为比特流
                    uint8_t bits[(HDLC_MAX_FRAME - 2) * 8];
                    int bit_count = 0;
                    for (int i = 0; i < frame_bytes - 2; i++) {
                        for (int b = 7; b >= 0; b--) {
                            bits[bit_count++] = (h->buf[i] >> b) & 1;
                        }
                    }
                    s_stats.total_frames++;
                    if (ch_b) s_stats.ch_b_frames++;
                    else      s_stats.ch_a_frames++;
                    parse_ais_message(bits, bit_count);
                } else {
                    s_stats.crc_errors++;
                }
            }
        }
        // 开始新帧
        h->in_frame  = true;
        h->byte_pos  = 0;
        h->bit_pos   = 0;
        h->ones_count = 0;
        return;
    }

    if (!h->in_frame) return;

    // 比特去填充：5个连续1后跳过0
    if (decoded == 1) {
        h->ones_count++;
    } else {
        if (h->ones_count == 5) {
            h->ones_count = 0;
            return;  // 去掉填充的0
        }
        h->ones_count = 0;
    }

    // 写入比特
    if (h->byte_pos >= HDLC_MAX_FRAME) {
        h->in_frame = false;
        return;
    }
    if (h->bit_pos == 0) h->buf[h->byte_pos] = 0;
    h->buf[h->byte_pos] |= (decoded << (7 - h->bit_pos));
    h->bit_pos++;
    if (h->bit_pos == 8) {
        h->bit_pos = 0;
        h->byte_pos++;
    }
}

// ---------------------------------------------------------------------------
// IQ 数据处理任务
// ---------------------------------------------------------------------------
#define PROC_BUF_SIZE   (AIS_SAMPLE_RATE / 10)  // 100ms 数据

static void ais_process_task(void *pvParameters)
{
    ESP_LOGI(TAG, "AIS processing task started");

    hdlc_state_t hdlc_a = {0};
    hdlc_state_t hdlc_b = {0};

    // 时钟恢复：每 (sample_rate / baud_rate) 个样本采一个比特
    const int samples_per_bit = AIS_SAMPLE_RATE / AIS_BAUD_RATE;  // ~26
    int sample_counter = 0;

    // 低通滤波器状态（简单 IIR）
    float lp_state = 0.0f;
    const float lp_alpha = 0.3f;

    // 当前信道（交替扫描）
    bool ch_b = false;
    uint32_t ch_switch_ms = 0;

    int8_t *iq_buf = malloc(PROC_BUF_SIZE * 2);
    if (!iq_buf) {
        ESP_LOGE(TAG, "Failed to allocate IQ buffer");
        vTaskDelete(NULL);
        return;
    }

    while (s_running) {
        // 每 5 秒切换信道
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms - ch_switch_ms > 5000) {
            ch_b = !ch_b;
            ch_switch_ms = now_ms;
            uint32_t freq = ch_b ? AIS_CH_B_FREQ : AIS_CH_A_FREQ;
            rtlsdr_set_frequency(freq);
            ESP_LOGD(TAG, "Switched to CH %s (%.3f MHz)",
                     ch_b ? "B" : "A", freq / 1e6);
        }

        // 读取 IQ 数据
        int bytes_read = rtlsdr_read_sync(iq_buf, PROC_BUF_SIZE * 2);
        if (bytes_read <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int samples = bytes_read / 2;
        hdlc_state_t *hdlc = ch_b ? &hdlc_b : &hdlc_a;

        for (int i = 0; i < samples - 1; i++) {
            int8_t i0 = iq_buf[i * 2];
            int8_t q0 = iq_buf[i * 2 + 1];
            int8_t i1 = iq_buf[(i + 1) * 2];
            int8_t q1 = iq_buf[(i + 1) * 2 + 1];

            // FM 解调
            float demod = fm_demod(i0, q0, i1, q1);

            // 低通滤波
            lp_state = lp_alpha * demod + (1.0f - lp_alpha) * lp_state;

            // 时钟采样
            sample_counter++;
            if (sample_counter >= samples_per_bit) {
                sample_counter = 0;
                uint8_t bit = (lp_state > 0) ? 1 : 0;
                hdlc_process_bit(hdlc, bit, ch_b);
            }
        }

        // 清理超时船只
        if (now_ms % 10000 < 100) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            for (int i = 0; i < s_vessel_count; ) {
                if ((now_ms - s_vessels[i].last_seen_ms) / 1000 > AIS_VESSEL_TIMEOUT) {
                    s_vessels[i] = s_vessels[--s_vessel_count];
                } else {
                    i++;
                }
            }
            s_stats.vessel_count = s_vessel_count;
            xSemaphoreGive(s_mutex);
        }
    }

    free(iq_buf);
    ESP_LOGI(TAG, "AIS processing task stopped");
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// WebSocket 广播
// ---------------------------------------------------------------------------
static char *build_vessels_json(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "vessels");
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    for (int i = 0; i < s_vessel_count; i++) {
        vessel_t *v = &s_vessels[i];
        cJSON *obj = cJSON_CreateObject();

        char mmsi_str[10];
        snprintf(mmsi_str, sizeof(mmsi_str), "%09u", v->mmsi);
        cJSON_AddStringToObject(obj, "mmsi",        mmsi_str);
        cJSON_AddStringToObject(obj, "name",        v->name);
        cJSON_AddStringToObject(obj, "callsign",    v->callsign);
        cJSON_AddStringToObject(obj, "destination", v->destination);
        cJSON_AddBoolToObject(obj,   "pos_valid",   v->pos_valid);
        if (v->pos_valid) {
            cJSON_AddNumberToObject(obj, "lat", v->lat);
            cJSON_AddNumberToObject(obj, "lon", v->lon);
        }
        cJSON_AddNumberToObject(obj, "sog",         v->sog);
        cJSON_AddNumberToObject(obj, "cog",         v->cog);
        cJSON_AddNumberToObject(obj, "heading",     v->heading);
        cJSON_AddNumberToObject(obj, "rot",         v->rot);
        cJSON_AddNumberToObject(obj, "nav_status",  v->nav_status);
        cJSON_AddStringToObject(obj, "nav_str",     ais_nav_status_str(v->nav_status));
        cJSON_AddNumberToObject(obj, "ship_type",   v->ship_type);
        cJSON_AddStringToObject(obj, "ship_str",    ais_ship_type_str(v->ship_type));
        cJSON_AddNumberToObject(obj, "imo",         v->imo);
        cJSON_AddNumberToObject(obj, "msgs",        v->msg_count);
        cJSON_AddNumberToObject(obj, "seen",        (now_ms - v->last_seen_ms) / 1000);
        cJSON_AddItemToArray(arr, obj);
    }

    xSemaphoreGive(s_mutex);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

void ais_ws_broadcast(void)
{
    if (!s_server || s_ws_count == 0) return;

    char *json = build_vessels_json();
    if (!json) return;

    httpd_ws_frame_t pkt = {
        .final = true, .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    int i = 0;
    while (i < s_ws_count) {
        if (httpd_ws_send_frame_async(s_server, s_ws_clients[i], &pkt) != ESP_OK) {
            s_ws_clients[i] = s_ws_clients[--s_ws_count];
        } else {
            i++;
        }
    }
    free(json);
}

// ---------------------------------------------------------------------------
// HTTP 处理器
// ---------------------------------------------------------------------------
static esp_err_t ais_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        for (int i = 0; i < s_ws_count; i++) {
            if (s_ws_clients[i] == fd) return ESP_OK;
        }
        if (s_ws_count < 4) s_ws_clients[s_ws_count++] = fd;
        ESP_LOGI(TAG, "AIS WS client connected fd=%d", fd);
        return ESP_OK;
    }
    httpd_ws_frame_t pkt = {0};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK || pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        for (int i = 0; i < s_ws_count; i++) {
            if (s_ws_clients[i] == fd) {
                s_ws_clients[i] = s_ws_clients[--s_ws_count];
                break;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t ais_start_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = ais_decoder_start();
    if (ret == ESP_OK) {
        httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"ok\":false}");
    }
    return ESP_OK;
}

static esp_err_t ais_stop_handler(httpd_req_t *req)
{
    ais_decoder_stop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t ais_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    ais_stats_t stats;
    ais_get_stats(&stats);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root,   "active",        s_running);
    cJSON_AddNumberToObject(root, "vessel_count",  stats.vessel_count);
    cJSON_AddNumberToObject(root, "valid_frames",  stats.valid_frames);
    cJSON_AddNumberToObject(root, "crc_errors",    stats.crc_errors);
    cJSON_AddNumberToObject(root, "ch_a_frames",   stats.ch_a_frames);
    cJSON_AddNumberToObject(root, "ch_b_frames",   stats.ch_b_frames);
    cJSON_AddNumberToObject(root, "uptime_s",      stats.uptime_s);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

static esp_err_t ais_vessels_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    char *json = build_vessels_json();
    if (!json) { httpd_resp_send_500(req); return ESP_FAIL; }
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// 广播任务
static void ais_broadcast_task(void *pvParameters)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (s_running) {
            s_stats.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
            ais_ws_broadcast();
        }
    }
}

// ---------------------------------------------------------------------------
// 公共 API
// ---------------------------------------------------------------------------
esp_err_t ais_decoder_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    memset(s_vessels, 0, sizeof(s_vessels));
    memset(&s_stats,  0, sizeof(s_stats));
    s_vessel_count = 0;
    s_running      = false;
    ESP_LOGI(TAG, "AIS decoder initialized");
    return ESP_OK;
}

esp_err_t ais_decoder_start(void)
{
    if (s_running) return ESP_OK;

    esp_err_t ret = rtlsdr_set_frequency(AIS_CH_A_FREQ);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set RTL-SDR frequency");
        return ret;
    }
    ret = rtlsdr_set_sample_rate(AIS_SAMPLE_RATE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set RTL-SDR sample rate");
        return ret;
    }

    s_running = true;
    xTaskCreate(ais_process_task, "ais_proc", 8192, NULL, 5, &s_task_handle);
    ESP_LOGI(TAG, "AIS decoder started (CH A: %.3f MHz)", AIS_CH_A_FREQ / 1e6);
    return ESP_OK;
}

void ais_decoder_stop(void)
{
    s_running = false;
    if (s_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(200));
        s_task_handle = NULL;
    }
    ESP_LOGI(TAG, "AIS decoder stopped");
}

void ais_get_vessel_list(vessel_t **vessels, int *count)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *vessels = s_vessels;
    *count   = s_vessel_count;
}

void ais_release_vessel_list(void)
{
    xSemaphoreGive(s_mutex);
}

void ais_get_stats(ais_stats_t *stats)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *stats = s_stats;
    xSemaphoreGive(s_mutex);
}

esp_err_t ais_register_handlers(httpd_handle_t server)
{
    s_server   = server;
    s_ws_count = 0;

    httpd_uri_t ws = {
        .uri = "/ais/ws", .method = HTTP_GET,
        .handler = ais_ws_handler, .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws);

    httpd_uri_t start = { .uri = "/ais/start", .method = HTTP_POST, .handler = ais_start_handler };
    httpd_register_uri_handler(server, &start);

    httpd_uri_t stop = { .uri = "/ais/stop", .method = HTTP_POST, .handler = ais_stop_handler };
    httpd_register_uri_handler(server, &stop);

    httpd_uri_t status = { .uri = "/ais/status", .method = HTTP_GET, .handler = ais_status_handler };
    httpd_register_uri_handler(server, &status);

    httpd_uri_t vessels = { .uri = "/ais/vessels", .method = HTTP_GET, .handler = ais_vessels_handler };
    httpd_register_uri_handler(server, &vessels);

    xTaskCreate(ais_broadcast_task, "ais_bcast", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "AIS HTTP handlers registered");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// 辅助字符串
// ---------------------------------------------------------------------------
const char *ais_nav_status_str(uint8_t status)
{
    static const char *strs[] = {
        "航行中(机动)", "锚泊", "失控", "操纵受限",
        "吃水受限", "系泊", "搁浅", "捕鱼中", "帆船航行",
        "保留9", "保留10", "保留11", "保留12",
        "保留13", "AIS-SART", "未定义"
    };
    if (status > 15) status = 15;
    return strs[status];
}

const char *ais_ship_type_str(uint8_t type)
{
    if (type == 0)                    return "未知";
    if (type >= 20 && type <= 29)     return "翼地效应船";
    if (type >= 30 && type <= 39) {
        if (type == 30) return "渔船";
        if (type == 31 || type == 32) return "拖船";
        if (type == 33) return "疏浚船";
        if (type == 34) return "潜水作业船";
        if (type == 35) return "军事船";
        if (type == 36) return "帆船";
        if (type == 37) return "游艇";
        return "特殊船";
    }
    if (type >= 40 && type <= 49)     return "高速船";
    if (type >= 50 && type <= 59) {
        if (type == 50) return "引航船";
        if (type == 51) return "搜救船";
        if (type == 52) return "拖船";
        if (type == 53) return "港口供应船";
        if (type == 54) return "防污染船";
        if (type == 55) return "执法船";
        if (type == 58) return "医疗船";
        return "特殊用途船";
    }
    if (type >= 60 && type <= 69)     return "客船";
    if (type >= 70 && type <= 79)     return "货船";
    if (type >= 80 && type <= 89)     return "油轮";
    if (type >= 90 && type <= 99)     return "其他船舶";
    return "未知";
}