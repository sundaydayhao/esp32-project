#include "adsb_decoder.h"
#include "config.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "adsb_decoder";

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static aircraft_t s_aircraft[MAX_AIRCRAFT];
static int        s_aircraft_count = 0;
static SemaphoreHandle_t s_aircraft_mutex;
static adsb_stats_t s_stats = {0};

// ---------------------------------------------------------------------------
// CRC-24 for Mode-S
// ---------------------------------------------------------------------------

static const uint32_t CRC24_GENERATOR = 0xFFF409;

static uint32_t modes_crc(const uint8_t *msg, int len)
{
    uint32_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= ((uint32_t)msg[i]) << 16;
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if (crc & 0x1000000) {
                crc ^= CRC24_GENERATOR;
            }
        }
    }
    return crc & 0xFFFFFF;
}

// ---------------------------------------------------------------------------
// CPR (Compact Position Reporting) Decoding
// ---------------------------------------------------------------------------

// NL(lat): Number of longitude zones for a given latitude
static int cpr_nl(double lat)
{
    if (lat < 0) lat = -lat;
    if (lat < 10.47047130) return 59;
    if (lat < 14.82817437) return 58;
    if (lat < 18.18626357) return 57;
    if (lat < 21.02939493) return 56;
    if (lat < 23.54504487) return 55;
    if (lat < 25.82924707) return 54;
    if (lat < 27.93898710) return 53;
    if (lat < 29.91135686) return 52;
    if (lat < 31.77209708) return 51;
    if (lat < 33.53993436) return 50;
    if (lat < 35.22899598) return 49;
    if (lat < 36.85025108) return 48;
    if (lat < 38.41241892) return 47;
    if (lat < 39.92256684) return 46;
    if (lat < 41.38651832) return 45;
    if (lat < 42.80914012) return 44;
    if (lat < 44.19454951) return 43;
    if (lat < 45.54626723) return 42;
    if (lat < 46.86733252) return 41;
    if (lat < 48.16039128) return 40;
    if (lat < 49.42776439) return 39;
    if (lat < 50.67150166) return 38;
    if (lat < 51.89342469) return 37;
    if (lat < 53.09516153) return 36;
    if (lat < 54.27817472) return 35;
    if (lat < 55.44378444) return 34;
    if (lat < 56.59318756) return 33;
    if (lat < 57.72747354) return 32;
    if (lat < 58.84763776) return 31;
    if (lat < 59.95459277) return 30;
    if (lat < 61.04917774) return 29;
    if (lat < 62.13216659) return 28;
    if (lat < 63.20427479) return 27;
    if (lat < 64.26616523) return 26;
    if (lat < 65.31845310) return 25;
    if (lat < 66.36171008) return 24;
    if (lat < 67.39646774) return 23;
    if (lat < 68.42322022) return 22;
    if (lat < 69.44242631) return 21;
    if (lat < 70.45451075) return 20;
    if (lat < 71.45986473) return 19;
    if (lat < 72.45884545) return 18;
    if (lat < 73.45177442) return 17;
    if (lat < 74.43893416) return 16;
    if (lat < 75.42056257) return 15;
    if (lat < 76.39684391) return 14;
    if (lat < 77.36789461) return 13;
    if (lat < 78.33374083) return 12;
    if (lat < 79.29428225) return 11;
    if (lat < 80.24923213) return 10;
    if (lat < 81.19801349) return 9;
    if (lat < 82.13956981) return 8;
    if (lat < 83.07199445) return 7;
    if (lat < 83.99173563) return 6;
    if (lat < 84.89166191) return 5;
    if (lat < 85.75541621) return 4;
    if (lat < 86.53536998) return 3;
    if (lat < 87.00000000) return 2;
    return 1;
}

/**
 * @brief Decode CPR position from even/odd frame pair
 *
 * @param even_lat  Even frame CPR latitude (17 bits)
 * @param even_lon  Even frame CPR longitude (17 bits)
 * @param odd_lat   Odd frame CPR latitude (17 bits)
 * @param odd_lon   Odd frame CPR longitude (17 bits)
 * @param is_odd    1 if most recent frame is odd, 0 if even
 * @param lat_out   Decoded latitude (output)
 * @param lon_out   Decoded longitude (output)
 * @return true if decoding succeeded
 */
static bool cpr_decode_global(uint32_t even_lat, uint32_t even_lon,
                               uint32_t odd_lat,  uint32_t odd_lon,
                               int is_odd,
                               double *lat_out, double *lon_out)
{
    const double DLat0 = 360.0 / 60.0;  // 6 degrees
    const double DLat1 = 360.0 / 59.0;  // ~6.1 degrees

    // Latitude index
    double j = floor(59.0 * ((double)even_lat / 131072.0) -
                     60.0 * ((double)odd_lat  / 131072.0) + 0.5);

    double rlat0 = DLat0 * (fmod(j, 60.0) + (double)even_lat / 131072.0);
    double rlat1 = DLat1 * (fmod(j, 59.0) + (double)odd_lat  / 131072.0);

    if (rlat0 >= 270.0) rlat0 -= 360.0;
    if (rlat1 >= 270.0) rlat1 -= 360.0;

    // Check NL consistency
    if (cpr_nl(rlat0) != cpr_nl(rlat1)) {
        return false;
    }

    double lat = is_odd ? rlat1 : rlat0;
    int nl = cpr_nl(lat);

    // Longitude
    int ni, m;
    double lon;

    if (is_odd) {
        ni = (nl - 1 > 1) ? nl - 1 : 1;
        m = (int)floor((double)even_lon * (nl - 1) / 131072.0 -
                       (double)odd_lon  * nl       / 131072.0 + 0.5);
        lon = (360.0 / ni) * (fmod((double)m, (double)ni) + (double)odd_lon / 131072.0);
    } else {
        ni = (nl > 1) ? nl : 1;
        m = (int)floor((double)even_lon * (nl - 1) / 131072.0 -
                       (double)odd_lon  * nl       / 131072.0 + 0.5);
        lon = (360.0 / ni) * (fmod((double)m, (double)ni) + (double)even_lon / 131072.0);
    }

    if (lon >= 180.0) lon -= 360.0;

    *lat_out = lat;
    *lon_out = lon;
    return true;
}

// ---------------------------------------------------------------------------
// Aircraft Database Management
// ---------------------------------------------------------------------------

static aircraft_t *find_or_create_aircraft(uint32_t icao)
{
    // Search for existing entry
    for (int i = 0; i < s_aircraft_count; i++) {
        if (s_aircraft[i].icao == icao) {
            return &s_aircraft[i];
        }
    }

    // Create new entry
    if (s_aircraft_count < MAX_AIRCRAFT) {
        aircraft_t *ac = &s_aircraft[s_aircraft_count++];
        memset(ac, 0, sizeof(aircraft_t));
        ac->icao = icao;
        return ac;
    }

    // Database full: replace oldest entry
    int oldest_idx = 0;
    uint32_t oldest_time = s_aircraft[0].last_seen_ms;
    for (int i = 1; i < s_aircraft_count; i++) {
        if (s_aircraft[i].last_seen_ms < oldest_time) {
            oldest_time = s_aircraft[i].last_seen_ms;
            oldest_idx = i;
        }
    }
    aircraft_t *ac = &s_aircraft[oldest_idx];
    memset(ac, 0, sizeof(aircraft_t));
    ac->icao = icao;
    return ac;
}

static void remove_stale_aircraft(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t timeout_ms = AIRCRAFT_TIMEOUT_S * 1000;
    int i = 0;
    while (i < s_aircraft_count) {
        if ((now_ms - s_aircraft[i].last_seen_ms) > timeout_ms) {
            // Remove by swapping with last
            s_aircraft[i] = s_aircraft[s_aircraft_count - 1];
            s_aircraft_count--;
        } else {
            i++;
        }
    }
}

// ---------------------------------------------------------------------------
// ADS-B Message Decoding
// ---------------------------------------------------------------------------

// Decode aircraft identification (TC 1-4)
static void decode_identification(aircraft_t *ac, const uint8_t *me)
{
    static const char charset[] = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ#####_###############0123456789######";
    char cs[9] = {0};
    cs[0] = charset[(me[1] >> 2) & 0x3F];
    cs[1] = charset[((me[1] & 0x03) << 4) | ((me[2] >> 4) & 0x0F)];
    cs[2] = charset[((me[2] & 0x0F) << 2) | ((me[3] >> 6) & 0x03)];
    cs[3] = charset[me[3] & 0x3F];
    cs[4] = charset[(me[4] >> 2) & 0x3F];
    cs[5] = charset[((me[4] & 0x03) << 4) | ((me[5] >> 4) & 0x0F)];
    cs[6] = charset[((me[5] & 0x0F) << 2) | ((me[6] >> 6) & 0x03)];
    cs[7] = charset[me[6] & 0x3F];
    cs[8] = '\0';

    // Trim trailing spaces
    for (int i = 7; i >= 0; i--) {
        if (cs[i] == ' ' || cs[i] == '_' || cs[i] == '#') {
            cs[i] = '\0';
        } else {
            break;
        }
    }

    strncpy(ac->callsign, cs, CALLSIGN_LEN - 1);
    ac->callsign[CALLSIGN_LEN - 1] = '\0';
}

// Decode airborne position (TC 9-18, 20-22)
static void decode_airborne_position(aircraft_t *ac, const uint8_t *me, uint32_t now_ms)
{
    int F = (me[2] >> 2) & 0x01;  // CPR frame type: 0=even, 1=odd

    // Altitude (Gillham code or simple encoding)
    int alt_raw = ((me[1] & 0xFF) << 4) | ((me[2] >> 4) & 0x0F);
    int Q = (alt_raw >> 4) & 0x01;  // Q bit

    if (Q) {
        // 25-foot resolution
        int n = ((alt_raw & 0xFE0) >> 1) | (alt_raw & 0x0F);
        ac->altitude = n * 25 - 1000;
    }

    // CPR latitude and longitude (17 bits each)
    uint32_t cpr_lat = ((me[2] & 0x03) << 15) | (me[3] << 7) | (me[4] >> 1);
    uint32_t cpr_lon = ((me[4] & 0x01) << 16) | (me[5] << 8) | me[6];

    if (F == 0) {
        ac->cpr_even_lat    = cpr_lat;
        ac->cpr_even_lon    = cpr_lon;
        ac->cpr_even_time_ms = now_ms;
    } else {
        ac->cpr_odd_lat    = cpr_lat;
        ac->cpr_odd_lon    = cpr_lon;
        ac->cpr_odd_time_ms = now_ms;
    }

    // Decode position if we have both even and odd frames within 10 seconds
    if (ac->cpr_even_time_ms > 0 && ac->cpr_odd_time_ms > 0) {
        uint32_t time_diff = (ac->cpr_even_time_ms > ac->cpr_odd_time_ms)
                           ? (ac->cpr_even_time_ms - ac->cpr_odd_time_ms)
                           : (ac->cpr_odd_time_ms - ac->cpr_even_time_ms);

        if (time_diff < 10000) {  // Within 10 seconds
            int is_odd = (ac->cpr_odd_time_ms > ac->cpr_even_time_ms) ? 1 : 0;
            double lat, lon;
            if (cpr_decode_global(ac->cpr_even_lat, ac->cpr_even_lon,
                                  ac->cpr_odd_lat,  ac->cpr_odd_lon,
                                  is_odd, &lat, &lon)) {
                ac->lat = lat;
                ac->lon = lon;
                ac->position_valid = true;
            }
        }
    }
}

// Decode airborne velocity (TC 19)
static void decode_airborne_velocity(aircraft_t *ac, const uint8_t *me)
{
    int subtype = me[1] & 0x07;

    if (subtype == 1 || subtype == 2) {
        // Ground speed
        int dew = (me[2] >> 2) & 0x01;  // Direction E-W
        int vew = ((me[2] & 0x03) << 8) | me[3];  // Velocity E-W
        int dns = (me[4] >> 7) & 0x01;  // Direction N-S
        int vns = ((me[4] & 0x7F) << 3) | (me[5] >> 5);  // Velocity N-S

        if (vew != 0 && vns != 0) {
            vew -= 1;
            vns -= 1;
            if (dew) vew = -vew;
            if (dns) vns = -vns;

            ac->speed   = (uint16_t)sqrt((double)(vew * vew + vns * vns));
            ac->heading = (uint16_t)(atan2((double)vew, (double)vns) * 180.0 / M_PI);
            if (ac->heading < 0) ac->heading += 360;
        }

        // Vertical rate
        int vr_sign = (me[5] >> 3) & 0x01;
        int vr      = ((me[5] & 0x07) << 6) | (me[6] >> 2);
        if (vr != 0) {
            vr -= 1;
            ac->vertical_rate = (int16_t)(vr_sign ? -vr * 64 : vr * 64);
        }
    } else if (subtype == 3 || subtype == 4) {
        // Airspeed
        int hdg_valid = (me[2] >> 2) & 0x01;
        if (hdg_valid) {
            int hdg_raw = ((me[2] & 0x03) << 8) | me[3];
            ac->heading = (uint16_t)(hdg_raw * 360.0 / 1024.0);
        }
        int airspeed = ((me[4] & 0x7F) << 3) | (me[5] >> 5);
        if (airspeed != 0) {
            ac->speed = (uint16_t)(airspeed - 1);
        }
    }
}

// Process a complete Mode-S message (hex bytes)
static void process_modes_message(const uint8_t *msg, int len)
{
    if (len < 7) return;  // Minimum DF17 length is 14 bytes (112 bits)

    int df = (msg[0] >> 3) & 0x1F;  // Downlink Format

    // We only care about DF17 (ADS-B Extended Squitter)
    // DF18 (TIS-B) is similar but from ground stations
    if (df != 17 && df != 18) return;

    if (len < 14) return;

    // Verify CRC
    uint32_t crc_calc = modes_crc(msg, 11);
    uint32_t crc_msg  = ((uint32_t)msg[11] << 16) |
                        ((uint32_t)msg[12] << 8)  |
                         (uint32_t)msg[13];

    if (crc_calc != crc_msg) {
        s_stats.crc_errors++;
        return;
    }

    s_stats.df17_messages++;

    // Extract ICAO address (bytes 1-3)
    uint32_t icao = ((uint32_t)msg[1] << 16) |
                    ((uint32_t)msg[2] << 8)  |
                     (uint32_t)msg[3];

    // ME field (bytes 4-10, 7 bytes = 56 bits)
    const uint8_t *me = &msg[4];
    int tc = (me[0] >> 3) & 0x1F;  // Type Code

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    xSemaphoreTake(s_aircraft_mutex, portMAX_DELAY);

    aircraft_t *ac = find_or_create_aircraft(icao);
    ac->last_seen_ms = now_ms;
    ac->msg_count++;

    if (tc >= TC_IDENT_MIN && tc <= TC_IDENT_MAX) {
        decode_identification(ac, me);
    } else if ((tc >= TC_AIRPOS_MIN && tc <= TC_AIRPOS_MAX) ||
               (tc >= TC_AIRPOS2_MIN && tc <= TC_AIRPOS2_MAX)) {
        decode_airborne_position(ac, me, now_ms);
    } else if (tc == TC_VELOCITY) {
        decode_airborne_velocity(ac, me);
    }

    s_stats.aircraft_count = s_aircraft_count;

    xSemaphoreGive(s_aircraft_mutex);
}

// ---------------------------------------------------------------------------
// UART Receive Task
// ---------------------------------------------------------------------------
// Receives AVR format messages: *<HEX>;\r\n
// Example: *8D4840D6202CC371C32CE0576098;\r\n

static void adsb_uart_task(void *pvParameters)
{
    uint8_t *uart_buf = malloc(ADSB_UART_BUF_SIZE);
    if (!uart_buf) {
        ESP_LOGE(TAG, "Failed to allocate UART buffer");
        vTaskDelete(NULL);
        return;
    }

    char msg_buf[256];
    int  msg_pos = 0;
    bool in_msg  = false;

    ESP_LOGI(TAG, "ADS-B UART receiver started on UART%d (GPIO%d)",
             ADSB_UART_NUM, ADSB_RX_PIN);

    while (1) {
        int len = uart_read_bytes(ADSB_UART_NUM, uart_buf,
                                  ADSB_UART_BUF_SIZE - 1,
                                  pdMS_TO_TICKS(100));

        if (len <= 0) {
            // Periodically clean up stale aircraft
            static uint32_t last_cleanup = 0;
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            if (now - last_cleanup > 5000) {
                xSemaphoreTake(s_aircraft_mutex, portMAX_DELAY);
                remove_stale_aircraft();
                xSemaphoreGive(s_aircraft_mutex);
                last_cleanup = now;
            }
            continue;
        }

        s_stats.total_messages += len;

        for (int i = 0; i < len; i++) {
            char c = (char)uart_buf[i];

            if (c == '*') {
                // Start of AVR message
                in_msg  = true;
                msg_pos = 0;
            } else if (c == ';' && in_msg) {
                // End of AVR message - decode it
                msg_buf[msg_pos] = '\0';
                in_msg = false;

                // Convert hex string to bytes
                int hex_len = msg_pos;
                if (hex_len >= 14 && (hex_len % 2) == 0) {
                    uint8_t raw[32];
                    int byte_len = hex_len / 2;
                    bool valid = true;

                    for (int j = 0; j < byte_len && j < 32; j++) {
                        char h[3] = {msg_buf[j*2], msg_buf[j*2+1], '\0'};
                        char *endptr;
                        raw[j] = (uint8_t)strtol(h, &endptr, 16);
                        if (*endptr != '\0') {
                            valid = false;
                            break;
                        }
                    }

                    if (valid) {
                        s_stats.valid_messages++;
                        process_modes_message(raw, byte_len);
                    }
                }
                msg_pos = 0;
            } else if (in_msg && msg_pos < (int)sizeof(msg_buf) - 1) {
                // Accumulate hex characters
                if ((c >= '0' && c <= '9') ||
                    (c >= 'A' && c <= 'F') ||
                    (c >= 'a' && c <= 'f')) {
                    msg_buf[msg_pos++] = c;
                }
            }
        }
    }

    free(uart_buf);
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t adsb_decoder_init(void)
{
    ESP_LOGI(TAG, "Initializing ADS-B decoder...");
    ESP_LOGI(TAG, "UART%d, Baud: %d, RX GPIO: %d",
             ADSB_UART_NUM, ADSB_UART_BAUD, ADSB_RX_PIN);

    s_aircraft_mutex = xSemaphoreCreateMutex();
    if (!s_aircraft_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(s_aircraft, 0, sizeof(s_aircraft));
    s_aircraft_count = 0;
    memset(&s_stats, 0, sizeof(s_stats));

    // Configure UART
    uart_config_t uart_config = {
        .baud_rate  = ADSB_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(ADSB_UART_NUM,
                                        ADSB_UART_BUF_SIZE * 2, 0,
                                        0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ADSB_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ADSB_UART_NUM,
                                 ADSB_TX_PIN, ADSB_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Start receive task
    xTaskCreate(adsb_uart_task, "adsb_uart", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "ADS-B decoder initialized. Max aircraft: %d, Timeout: %ds",
             MAX_AIRCRAFT, AIRCRAFT_TIMEOUT_S);

    return ESP_OK;
}

void adsb_get_aircraft_list(aircraft_t **aircraft, int *count)
{
    xSemaphoreTake(s_aircraft_mutex, portMAX_DELAY);
    *aircraft = s_aircraft;
    *count    = s_aircraft_count;
}

void adsb_release_aircraft_list(void)
{
    xSemaphoreGive(s_aircraft_mutex);
}

void adsb_get_stats(adsb_stats_t *stats)
{
    xSemaphoreTake(s_aircraft_mutex, portMAX_DELAY);
    *stats = s_stats;
    xSemaphoreGive(s_aircraft_mutex);
}

void adsb_inject_raw_message(const uint8_t *msg, int len)
{
    // Called by RMT decoder to inject directly-decoded messages
    // bypassing the UART path
    s_stats.valid_messages++;
    process_modes_message(msg, len);
}
