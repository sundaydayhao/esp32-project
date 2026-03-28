/**
 * @file main.c
 * @brief ESP32-S3 Signal Generator – Main entry point & serial console
 *
 * Console commands
 * ────────────────
 *  start                      – start waveform output
 *  stop                       – stop waveform output
 *  wave  <sine|square|tri|saw> – select waveform
 *  freq  <Hz>                 – set frequency (1 – 20000 Hz)
 *  amp   <0.0-1.0>            – set amplitude
 *  offset <0.0-1.0>           – set DC offset (0.5 = mid-rail)
 *  status                     – print current configuration
 *  help                       – list all commands
 *
 * Connect a serial terminal at 115200 baud to UART0 (USB-JTAG on
 * most ESP32-S3 DevKit boards).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

#include "signal_generator.h"

static const char *TAG = "main";

/* ─── Console prompt ─────────────────────────────────────────────── */
#define PROMPT_STR  "siggen> "

/* ─── Command: start ─────────────────────────────────────────────── */

static int cmd_start(int argc, char **argv)
{
    esp_err_t err = signal_generator_start();
    if (err != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Output started.\n");
    return 0;
}

static void register_start(void)
{
    const esp_console_cmd_t cmd = {
        .command = "start",
        .help    = "Start waveform output",
        .hint    = NULL,
        .func    = &cmd_start,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ─── Command: stop ──────────────────────────────────────────────── */

static int cmd_stop(int argc, char **argv)
{
    esp_err_t err = signal_generator_stop();
    if (err != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Output stopped.\n");
    return 0;
}

static void register_stop(void)
{
    const esp_console_cmd_t cmd = {
        .command = "stop",
        .help    = "Stop waveform output",
        .hint    = NULL,
        .func    = &cmd_stop,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ─── Command: wave ──────────────────────────────────────────────── */

static struct {
    struct arg_str *type;
    struct arg_end *end;
} wave_args;

static int cmd_wave(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wave_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wave_args.end, argv[0]);
        return 1;
    }

    const char *type_str = wave_args.type->sval[0];
    wave_type_t wave;

    if      (strcasecmp(type_str, "sine") == 0 || strcasecmp(type_str, "sin") == 0) {
        wave = WAVE_SINE;
    } else if (strcasecmp(type_str, "square") == 0 || strcasecmp(type_str, "sq") == 0) {
        wave = WAVE_SQUARE;
    } else if (strcasecmp(type_str, "triangle") == 0 || strcasecmp(type_str, "tri") == 0) {
        wave = WAVE_TRIANGLE;
    } else if (strcasecmp(type_str, "sawtooth") == 0 || strcasecmp(type_str, "saw") == 0) {
        wave = WAVE_SAWTOOTH;
    } else {
        printf("Unknown waveform '%s'. Use: sine, square, triangle, sawtooth\n", type_str);
        return 1;
    }

    esp_err_t err = signal_generator_set_wave(wave);
    if (err != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Waveform set to: %s\n", signal_generator_wave_name(wave));
    return 0;
}

static void register_wave(void)
{
    wave_args.type = arg_str1(NULL, NULL, "<sine|square|triangle|sawtooth>",
                              "Waveform type");
    wave_args.end  = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "wave",
        .help    = "Set waveform type (sine, square, triangle, sawtooth)",
        .hint    = "<type>",
        .func    = &cmd_wave,
        .argtable = &wave_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ─── Command: freq ──────────────────────────────────────────────── */

static struct {
    struct arg_dbl *hz;
    struct arg_end *end;
} freq_args;

static int cmd_freq(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&freq_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, freq_args.end, argv[0]);
        return 1;
    }

    float freq = (float)freq_args.hz->dval[0];
    esp_err_t err = signal_generator_set_frequency(freq);
    if (err != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Frequency set to: %.2f Hz\n", (double)freq);
    return 0;
}

static void register_freq(void)
{
    freq_args.hz  = arg_dbl1(NULL, NULL, "<Hz>",
                             "Output frequency in Hz (1 – 20000)");
    freq_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command  = "freq",
        .help     = "Set output frequency in Hz",
        .hint     = "<Hz>",
        .func     = &cmd_freq,
        .argtable = &freq_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ─── Command: amp ───────────────────────────────────────────────── */

static struct {
    struct arg_dbl *val;
    struct arg_end *end;
} amp_args;

static int cmd_amp(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&amp_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, amp_args.end, argv[0]);
        return 1;
    }

    float amp = (float)amp_args.val->dval[0];
    esp_err_t err = signal_generator_set_amplitude(amp);
    if (err != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Amplitude set to: %.2f (%.0f%%)\n", (double)amp, (double)(amp * 100.0f));
    return 0;
}

static void register_amp(void)
{
    amp_args.val = arg_dbl1(NULL, NULL, "<0.0-1.0>",
                            "Amplitude (0.0 = off, 1.0 = full scale)");
    amp_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command  = "amp",
        .help     = "Set output amplitude (0.0 to 1.0)",
        .hint     = "<value>",
        .func     = &cmd_amp,
        .argtable = &amp_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ─── Command: offset ────────────────────────────────────────────── */

static struct {
    struct arg_dbl *val;
    struct arg_end *end;
} offset_args;

static int cmd_offset(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&offset_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, offset_args.end, argv[0]);
        return 1;
    }

    float offset = (float)offset_args.val->dval[0];
    esp_err_t err = signal_generator_set_offset(offset);
    if (err != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("DC offset set to: %.2f\n", (double)offset);
    return 0;
}

static void register_offset(void)
{
    offset_args.val = arg_dbl1(NULL, NULL, "<0.0-1.0>",
                               "DC offset (0.5 = mid-rail, no DC bias)");
    offset_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command  = "offset",
        .help     = "Set DC offset (0.0 to 1.0, default 0.5)",
        .hint     = "<value>",
        .func     = &cmd_offset,
        .argtable = &offset_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ─── Command: status ────────────────────────────────────────────── */

static int cmd_status(int argc, char **argv)
{
    siggen_config_t cfg;
    signal_generator_get_config(&cfg);

    printf("\n");
    printf("┌─────────────────────────────────────┐\n");
    printf("│       Signal Generator Status        │\n");
    printf("├─────────────────────────────────────┤\n");
    printf("│  State     : %-22s │\n", cfg.running ? "RUNNING" : "STOPPED");
    printf("│  Waveform  : %-22s │\n", signal_generator_wave_name(cfg.wave_type));
    printf("│  Frequency : %-18.2f Hz │\n", (double)cfg.frequency);
    printf("│  Amplitude : %-18.2f    │\n", (double)cfg.amplitude);
    printf("│  DC Offset : %-18.2f    │\n", (double)cfg.offset);
    printf("│  Output    : GPIO %-17d │\n", SIGGEN_OUTPUT_GPIO);
    printf("│  PWM Freq  : %-14d Hz    │\n", SIGGEN_LEDC_FREQ_HZ);
    printf("│  Sample Rt : %-14d Hz    │\n", SIGGEN_SAMPLE_RATE_HZ);
    printf("└─────────────────────────────────────┘\n");
    printf("\n");
    return 0;
}

static void register_status(void)
{
    const esp_console_cmd_t cmd = {
        .command = "status",
        .help    = "Print current signal generator configuration",
        .hint    = NULL,
        .func    = &cmd_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ─── Console initialisation ─────────────────────────────────────── */

static void console_init(void)
{
    /* Drain stdout before reconfiguring */
    fflush(stdout);
    fsync(fileno(stdout));

    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Minicom / screen / idf_monitor compatible UART settings */
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                        256, 0, 0, NULL, 0));
    esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);

    /* Initialise the console */
    esp_console_config_t console_config = {
        .max_cmdline_args   = 8,
        .max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
        .hint_color = atoi(LOG_COLOR_CYAN),
#endif
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    /* Configure linenoise */
    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback *)&esp_console_get_hint);
    linenoiseHistorySetMaxLen(20);
    linenoiseAllowEmpty(false);
}

/* ─── Register all commands ──────────────────────────────────────── */

static void register_all_commands(void)
{
    ESP_ERROR_CHECK(esp_console_register_help_command());
    register_start();
    register_stop();
    register_wave();
    register_freq();
    register_amp();
    register_offset();
    register_status();
}

/* ─── Banner ─────────────────────────────────────────────────────── */

static void print_banner(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║     ESP32-S3  Signal Generator  v1.0      ║\n");
    printf("╠═══════════════════════════════════════════╣\n");
    printf("║  Output : GPIO %-3d (PWM → RC filter)      ║\n", SIGGEN_OUTPUT_GPIO);
    printf("║  Waves  : Sine / Square / Triangle / Saw  ║\n");
    printf("║  Range  : 1 Hz – 20 kHz                   ║\n");
    printf("╠═══════════════════════════════════════════╣\n");
    printf("║  Type 'help' for available commands.       ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    printf("\n");
}

/* ─── app_main ───────────────────────────────────────────────────── */

void app_main(void)
{
    /* Initialise NVS (required by some components) */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Initialise signal generator hardware */
    ESP_ERROR_CHECK(signal_generator_init());

    /* Set sensible defaults */
    signal_generator_set_wave(WAVE_SINE);
    signal_generator_set_frequency(1000.0f);
    signal_generator_set_amplitude(0.8f);
    signal_generator_set_offset(0.5f);

    /* Initialise serial console */
    console_init();
    register_all_commands();

    print_banner();

    /* Auto-start with default settings */
    signal_generator_start();
    printf("Auto-started: Sine 1000 Hz, amplitude 80%%\n\n");

    /* ── REPL loop ─────────────────────────────────────────────── */
    while (true) {
        char *line = linenoise(PROMPT_STR);

        if (line == NULL) {
            /* EOF or Ctrl-D */
            continue;
        }

        /* Add non-empty lines to history */
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
        }

        /* Execute the command */
        int ret;
        esp_err_t cmd_err = esp_console_run(line, &ret);

        if (cmd_err == ESP_ERR_NOT_FOUND) {
            printf("Unknown command: '%s'\n", line);
            printf("Type 'help' for a list of commands.\n");
        } else if (cmd_err == ESP_ERR_INVALID_ARG) {
            /* Command was empty – ignore */
        } else if (cmd_err != ESP_OK) {
            printf("Command error: %s\n", esp_err_to_name(cmd_err));
        }

        linenoiseFree(line);
    }
}