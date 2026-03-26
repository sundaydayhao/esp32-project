#include "fft_analyzer.h"
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "fft_analyzer";

/* ── Fixed-point radix-2 FFT ─────────────────────────────────────────── */
/* We use float arithmetic for simplicity on ESP32-S3 (has FPU)          */

typedef struct {
    float re;
    float im;
} complex_f_t;

static complex_f_t s_fft_buf[FFT_SIZE];
static float       s_window[FFT_SIZE];      /* Hann window coefficients  */
static float       s_input_buf[FFT_SIZE];   /* Accumulation buffer       */
static int         s_input_count = 0;

static fft_spectrum_t    s_latest_spectrum;
static volatile bool     s_spectrum_ready = false;
static SemaphoreHandle_t s_mutex          = NULL;

static fft_spectrum_cb_t s_callback      = NULL;
static void             *s_callback_data = NULL;

/* ── Hann window ─────────────────────────────────────────────────────── */
static void build_hann_window(void)
{
    for (int i = 0; i < FFT_SIZE; i++) {
        s_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
    }
}

/* ── Bit-reversal permutation ────────────────────────────────────────── */
static void bit_reverse(complex_f_t *buf, int n)
{
    int bits = 0;
    int tmp = n;
    while (tmp > 1) { bits++; tmp >>= 1; }

    for (int i = 0; i < n; i++) {
        int rev = 0;
        int x   = i;
        for (int b = 0; b < bits; b++) {
            rev = (rev << 1) | (x & 1);
            x >>= 1;
        }
        if (rev > i) {
            complex_f_t t = buf[i];
            buf[i]   = buf[rev];
            buf[rev] = t;
        }
    }
}

/* ── Cooley-Tukey FFT (in-place) ─────────────────────────────────────── */
static void fft_compute(complex_f_t *buf, int n)
{
    bit_reverse(buf, n);

    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * M_PI / len;
        complex_f_t wlen = { cosf(ang), sinf(ang) };

        for (int i = 0; i < n; i += len) {
            complex_f_t w = { 1.0f, 0.0f };
            for (int j = 0; j < len / 2; j++) {
                complex_f_t u = buf[i + j];
                complex_f_t v = {
                    buf[i + j + len/2].re * w.re - buf[i + j + len/2].im * w.im,
                    buf[i + j + len/2].re * w.im + buf[i + j + len/2].im * w.re,
                };
                buf[i + j]          = (complex_f_t){ u.re + v.re, u.im + v.im };
                buf[i + j + len/2]  = (complex_f_t){ u.re - v.re, u.im - v.im };

                float new_re = w.re * wlen.re - w.im * wlen.im;
                float new_im = w.re * wlen.im + w.im * wlen.re;
                w.re = new_re;
                w.im = new_im;
            }
        }
    }
}

/* ── Map FFT bins to log-scale bands ─────────────────────────────────── */
static void compute_bands(const complex_f_t *fft_out, fft_spectrum_t *spectrum)
{
    /* Frequency range: 20 Hz – 20 kHz (log scale) */
    const float f_min  = 20.0f;
    const float f_max  = 20000.0f;
    const float sr     = 44100.0f;   /* assume 44100; good enough for display */
    const float bin_hz = sr / FFT_SIZE;

    float log_min = log10f(f_min);
    float log_max = log10f(f_max);
    float log_step = (log_max - log_min) / FFT_BANDS;

    /* Peak magnitude for normalisation */
    float peak = 1e-6f;

    for (int b = 0; b < FFT_BANDS; b++) {
        float f_lo = powf(10.0f, log_min + b       * log_step);
        float f_hi = powf(10.0f, log_min + (b + 1) * log_step);

        int bin_lo = (int)(f_lo / bin_hz);
        int bin_hi = (int)(f_hi / bin_hz) + 1;
        if (bin_lo < 1)            bin_lo = 1;
        if (bin_hi >= FFT_SIZE/2)  bin_hi = FFT_SIZE/2 - 1;
        if (bin_hi <= bin_lo)      bin_hi = bin_lo + 1;

        float mag = 0.0f;
        for (int k = bin_lo; k <= bin_hi; k++) {
            float m = sqrtf(fft_out[k].re * fft_out[k].re +
                            fft_out[k].im * fft_out[k].im);
            if (m > mag) mag = m;
        }
        spectrum->bands[b] = mag;
        if (mag > peak) peak = mag;
    }

    /* Normalise to 0.0 – 1.0 using dB scale */
    for (int b = 0; b < FFT_BANDS; b++) {
        float db = 20.0f * log10f(spectrum->bands[b] / peak + 1e-9f);
        /* Map -60 dB .. 0 dB → 0.0 .. 1.0 */
        float norm = (db + 60.0f) / 60.0f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        spectrum->bands[b] = norm;
    }
}

/* ── Process a full window of samples ───────────────────────────────── */
static void process_window(void)
{
    /* Apply Hann window and load into FFT buffer */
    for (int i = 0; i < FFT_SIZE; i++) {
        s_fft_buf[i].re = s_input_buf[i] * s_window[i];
        s_fft_buf[i].im = 0.0f;
    }

    fft_compute(s_fft_buf, FFT_SIZE);

    fft_spectrum_t spectrum;
    spectrum.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    compute_bands(s_fft_buf, &spectrum);

    /* Store latest */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&s_latest_spectrum, &spectrum, sizeof(fft_spectrum_t));
    s_spectrum_ready = true;
    xSemaphoreGive(s_mutex);

    /* Fire callback */
    if (s_callback) {
        s_callback(&spectrum, s_callback_data);
    }
}

/* ── Public API ──────────────────────────────────────────────────────── */
esp_err_t fft_analyzer_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    build_hann_window();
    s_input_count    = 0;
    s_spectrum_ready = false;
    memset(&s_latest_spectrum, 0, sizeof(fft_spectrum_t));

    ESP_LOGI(TAG, "FFT analyzer initialized (size=%d, bands=%d)", FFT_SIZE, FFT_BANDS);
    return ESP_OK;
}

void fft_analyzer_feed(const int16_t *samples, int count)
{
    /* Expect stereo interleaved; take left channel only */
    for (int i = 0; i < count; i += 2) {
        float sample = samples[i] / 32768.0f;
        s_input_buf[s_input_count++] = sample;

        if (s_input_count >= FFT_SIZE) {
            process_window();
            /* 50% overlap */
            memmove(s_input_buf, s_input_buf + FFT_SIZE / 2,
                    (FFT_SIZE / 2) * sizeof(float));
            s_input_count = FFT_SIZE / 2;
        }
    }
}

void fft_analyzer_set_callback(fft_spectrum_cb_t cb, void *user_data)
{
    s_callback      = cb;
    s_callback_data = user_data;
}

bool fft_analyzer_get_spectrum(fft_spectrum_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool ready = s_spectrum_ready;
    if (ready) {
        memcpy(out, &s_latest_spectrum, sizeof(fft_spectrum_t));
        s_spectrum_ready = false;
    }
    xSemaphoreGive(s_mutex);
    return ready;
}