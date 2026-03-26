#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// =============================================================================
// ADS-B Decoder - Mode-S / ADS-B Extended Squitter Decoder
// =============================================================================
// Receives raw Mode-S messages via UART in AVR format: *<HEX>;\r\n
// Decodes DF17 (ADS-B) messages and maintains aircraft database
// =============================================================================

// Maximum callsign length (8 chars + null terminator)
#define CALLSIGN_LEN    9

// ADS-B message type codes (TC field in ME)
#define TC_IDENT_MIN    1
#define TC_IDENT_MAX    4
#define TC_AIRPOS_MIN   9
#define TC_AIRPOS_MAX   18
#define TC_VELOCITY     19
#define TC_AIRPOS2_MIN  20
#define TC_AIRPOS2_MAX  22

/**
 * @brief Aircraft state structure
 */
typedef struct {
    uint32_t icao;              // ICAO 24-bit address (unique aircraft ID)
    char     callsign[CALLSIGN_LEN]; // Flight callsign (e.g., "CCA1234")
    double   lat;               // Latitude in degrees (-90 to +90)
    double   lon;               // Longitude in degrees (-180 to +180)
    int32_t  altitude;          // Altitude in feet (barometric)
    uint16_t speed;             // Ground speed in knots
    uint16_t heading;           // Track angle in degrees (0-359)
    int16_t  vertical_rate;     // Vertical rate in ft/min
    uint8_t  squawk;            // Squawk code (Mode-A)
    bool     position_valid;    // True if lat/lon are valid
    bool     on_ground;         // True if aircraft is on ground
    uint32_t last_seen_ms;      // Timestamp of last message (ms since boot)
    uint32_t msg_count;         // Total messages received from this aircraft

    // CPR (Compact Position Reporting) decoding state
    uint32_t cpr_even_lat;      // Even frame CPR latitude
    uint32_t cpr_even_lon;      // Even frame CPR longitude
    uint32_t cpr_odd_lat;       // Odd frame CPR latitude
    uint32_t cpr_odd_lon;       // Odd frame CPR longitude
    uint32_t cpr_even_time_ms;  // Timestamp of even CPR frame
    uint32_t cpr_odd_time_ms;   // Timestamp of odd CPR frame
} aircraft_t;

/**
 * @brief ADS-B statistics
 */
typedef struct {
    uint32_t total_messages;    // Total messages received
    uint32_t valid_messages;    // Valid Mode-S messages
    uint32_t df17_messages;     // DF17 (ADS-B) messages
    uint32_t crc_errors;        // CRC error count
    uint32_t aircraft_count;    // Current tracked aircraft count
} adsb_stats_t;

/**
 * @brief Initialize ADS-B decoder and start UART receive task
 *
 * @return ESP_OK on success
 */
esp_err_t adsb_decoder_init(void);

/**
 * @brief Get a snapshot of the current aircraft database
 *        Caller must call adsb_release_aircraft_list() when done
 *
 * @param aircraft  Pointer to aircraft array (output)
 * @param count     Number of aircraft in array (output)
 */
void adsb_get_aircraft_list(aircraft_t **aircraft, int *count);

/**
 * @brief Release the aircraft list lock
 */
void adsb_release_aircraft_list(void);

/**
 * @brief Get ADS-B statistics
 *
 * @param stats  Pointer to stats structure (output)
 */
void adsb_get_stats(adsb_stats_t *stats);

/**
 * @brief Inject a raw Mode-S message directly into the decoder
 *        Used by the RMT direct receiver to bypass UART
 *
 * @param msg     Raw message bytes
 * @param len     Message length in bytes (7 for short, 14 for long)
 */
void adsb_inject_raw_message(const uint8_t *msg, int len);
