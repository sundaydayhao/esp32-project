#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "as608.h"
#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "as608_demo";

static as608_t s_sensor;
static uint16_t s_library_size = AS608_DEFAULT_LIBRARY_SIZE;
static bool s_uart_input_enabled;
static bool s_usb_input_enabled;

static void console_input_init(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

/*
 * ESP-IDF can print logs to a secondary console, but stdin only follows the
 * primary console. Read both common ESP32-S3 ports directly so the demo remains
 * interactive no matter which USB port idf.py monitor is attached to.
 */
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_driver_config_t usb_config =
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t usb_ret = usb_serial_jtag_driver_install(&usb_config);
    if (usb_ret == ESP_OK || usb_ret == ESP_ERR_INVALID_STATE) {
        s_usb_input_enabled = true;
        ESP_LOGI(TAG, "command input enabled on USB Serial/JTAG");
    } else {
        ESP_LOGW(TAG, "USB Serial/JTAG input disabled: %s",
                 esp_err_to_name(usb_ret));
    }
#endif

#if CONFIG_ESP_CONSOLE_UART
    if (CONFIG_ESP_CONSOLE_UART_NUM == CONFIG_AS608_UART_NUM) {
        ESP_LOGW(TAG, "console UART%d conflicts with AS608 UART%d",
                 CONFIG_ESP_CONSOLE_UART_NUM, CONFIG_AS608_UART_NUM);
        ESP_LOGW(TAG, "change either console UART or AS608 UART in menuconfig");
    } else {
        uart_config_t console_uart_config = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };

        esp_err_t ret = uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM,
                                          &console_uart_config);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "console UART config failed: %s", esp_err_to_name(ret));
        } else {
            ret = uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0,
                                      NULL, 0);
            if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
                s_uart_input_enabled = true;
                ESP_LOGI(TAG, "command input enabled on UART%d",
                         CONFIG_ESP_CONSOLE_UART_NUM);
            } else {
                ESP_LOGW(TAG, "console UART input disabled: %s",
                         esp_err_to_name(ret));
            }
        }
    }
#endif

    if (!s_usb_input_enabled && !s_uart_input_enabled) {
        ESP_LOGW(TAG, "no command input port is enabled");
    }
}

static bool console_read_byte(uint8_t *ch)
{
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    if (s_usb_input_enabled) {
        int len = usb_serial_jtag_read_bytes(ch, 1, pdMS_TO_TICKS(10));
        if (len > 0) {
            return true;
        }
    }
#endif

#if CONFIG_ESP_CONSOLE_UART
    if (s_uart_input_enabled) {
        int len = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, ch, 1,
                                  pdMS_TO_TICKS(10));
        if (len > 0) {
            return true;
        }
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(10));
    return false;
}

static void console_read_line(char *line, size_t line_size)
{
    static bool skip_lf;
    size_t len = 0;

    while (true) {
        uint8_t ch = 0;
        if (!console_read_byte(&ch)) {
            continue;
        }

        if (skip_lf && ch == '\n') {
            skip_lf = false;
            continue;
        }
        skip_lf = false;

        if (ch == '\r' || ch == '\n') {
            if (ch == '\r') {
                skip_lf = true;
            }
            line[len] = '\0';
            printf("\n");
            return;
        }

        if (ch == '\b' || ch == 0x7f) {
            if (len > 0) {
                --len;
                printf("\b \b");
            }
            continue;
        }

        if (ch == 0x03) {
            line[0] = '\0';
            printf("^C\n");
            return;
        }

        if (isprint((unsigned char)ch) && len + 1 < line_size) {
            line[len++] = (char)ch;
            putchar(ch);
        }
    }
}

static char *skip_spaces(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    return text;
}

static void strip_line_end(char *text)
{
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n')) {
        text[--len] = '\0';
    }
}

static bool parse_u16(char *text, uint16_t *value)
{
    char *arg = skip_spaces(text);
    if (*arg == '\0') {
        return false;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(arg, &end, 0);
    if (end == arg || parsed > UINT16_MAX) {
        return false;
    }

    *value = (uint16_t)parsed;
    return true;
}

static void print_help(void)
{
    printf("\n");
    printf("AS608 commands:\n");
    printf("  h              show this help\n");
    printf("  p              read system parameters\n");
    printf("  c              read stored template count\n");
    printf("  v              verify/search a finger\n");
    printf("  e <id>         enroll a finger to template id\n");
    printf("  d <id> [n]     delete n templates from id, default n=1\n");
    printf("  x yes          clear the whole fingerprint library\n");
    printf("\n");
}

static void log_sensor_status(esp_err_t err)
{
    uint8_t status = as608_get_last_status(&s_sensor);
    ESP_LOGW(TAG, "%s, module status 0x%02x (%s)",
             esp_err_to_name(err), status, as608_status_to_name(status));
}

static void read_params(void)
{
    as608_sys_params_t params;
    esp_err_t ret = as608_read_sys_params(&s_sensor, &params);
    if (ret != ESP_OK) {
        log_sensor_status(ret);
        return;
    }

    if (params.library_size > 0) {
        s_library_size = params.library_size;
    }

    ESP_LOGI(TAG,
             "status=0x%04x system=0x%04x capacity=%u security=%u addr=0x%08" PRIx32
             " packet=%u(code=%u) baud=%" PRIu32 "(code=%u)",
             params.status_register,
             params.system_id,
             params.library_size,
             params.security_level,
             params.address,
             params.packet_size,
             params.packet_size_code,
             params.baud_rate,
             params.baud_rate_code);
}

static void read_count(void)
{
    uint16_t count = 0;
    esp_err_t ret = as608_read_template_count(&s_sensor, &count);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "stored templates: %u / %u", count, s_library_size);
    } else {
        log_sensor_status(ret);
    }
}

static void verify_finger(void)
{
    ESP_LOGI(TAG, "place finger on AS608");

    as608_search_result_t result;
    esp_err_t ret = as608_verify(&s_sensor, s_library_size,
                                 CONFIG_AS608_WAIT_TIMEOUT_MS, &result);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "matched template id=%u score=%u",
                 result.page_id, result.score);
        (void)as608_wait_for_no_finger(&s_sensor, CONFIG_AS608_REMOVE_TIMEOUT_MS);
    } else {
        log_sensor_status(ret);
    }
}

static void enroll_finger(char *args)
{
    uint16_t id = 0;
    if (!parse_u16(args, &id)) {
        ESP_LOGW(TAG, "usage: e <id>");
        return;
    }

    if (id >= s_library_size) {
        ESP_LOGW(TAG, "id %u is outside module capacity %u", id, s_library_size);
        return;
    }

    ESP_LOGI(TAG, "enrolling template id=%u", id);
    esp_err_t ret = as608_enroll(&s_sensor, id,
                                 CONFIG_AS608_WAIT_TIMEOUT_MS,
                                 CONFIG_AS608_REMOVE_TIMEOUT_MS);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "enroll ok, template id=%u", id);
        read_count();
    } else {
        log_sensor_status(ret);
    }
}

static void delete_template(char *args)
{
    uint16_t id = 0;
    if (!parse_u16(args, &id)) {
        ESP_LOGW(TAG, "usage: d <id> [n]");
        return;
    }

    char *next = skip_spaces(args);
    while (*next != '\0' && !isspace((unsigned char)*next)) {
        ++next;
    }

    uint16_t count = 1;
    uint16_t parsed_count = 0;
    if (parse_u16(next, &parsed_count)) {
        count = parsed_count;
    }

    esp_err_t ret = as608_delete_char(&s_sensor, id, count);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "deleted %u template(s) from id=%u", count, id);
        read_count();
    } else {
        log_sensor_status(ret);
    }
}

static void clear_library(char *args)
{
    char *confirmation = skip_spaces(args);
    if (strcasecmp(confirmation, "yes") != 0) {
        ESP_LOGW(TAG, "usage: x yes");
        return;
    }

    esp_err_t ret = as608_empty_library(&s_sensor);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "fingerprint library cleared");
        read_count();
    } else {
        log_sensor_status(ret);
    }
}

static void command_loop(void)
{
    char line[80];

    print_help();

    while (true) {
        printf("as608> ");
        fflush(stdout);
        console_read_line(line, sizeof(line));

        strip_line_end(line);
        char *cmd = skip_spaces(line);
        if (*cmd == '\0') {
            continue;
        }

        char op = (char)tolower((unsigned char)*cmd++);
        char *args = skip_spaces(cmd);

        switch (op) {
        case 'h':
            print_help();
            break;
        case 'p':
            read_params();
            break;
        case 'c':
            read_count();
            break;
        case 'v':
            verify_finger();
            break;
        case 'e':
            enroll_finger(args);
            break;
        case 'd':
            delete_template(args);
            break;
        case 'x':
            clear_library(args);
            break;
        default:
            ESP_LOGW(TAG, "unknown command '%c'", op);
            print_help();
            break;
        }
    }
}

void app_main(void)
{
    console_input_init();

    as608_config_t config = {
        .uart_num = (uart_port_t)CONFIG_AS608_UART_NUM,
        .tx_gpio_num = CONFIG_AS608_UART_TX_GPIO,
        .rx_gpio_num = CONFIG_AS608_UART_RX_GPIO,
        .baud_rate = CONFIG_AS608_BAUD_RATE,
        .address = (uint32_t)CONFIG_AS608_DEVICE_ADDRESS,
        .password = (uint32_t)CONFIG_AS608_PASSWORD,
        .rx_timeout_ms = CONFIG_AS608_COMMAND_TIMEOUT_MS,
        .rx_buffer_size = CONFIG_AS608_UART_RX_BUFFER_SIZE,
        .power_on_delay_ms = CONFIG_AS608_POWER_ON_DELAY_MS,
    };

    ESP_LOGI(TAG, "AS608 wiring: ESP TX GPIO%d -> AS608 RX, ESP RX GPIO%d <- AS608 TX",
             config.tx_gpio_num, config.rx_gpio_num);
    ESP_LOGI(TAG, "UART%d baud=%" PRIu32 " addr=0x%08" PRIx32,
             config.uart_num, config.baud_rate, config.address);

    esp_err_t ret = as608_init(&s_sensor, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AS608 init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "check VCC/GND, cross TX/RX, baud rate, password, and UART pins");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    read_params();
    read_count();
    command_loop();
}
