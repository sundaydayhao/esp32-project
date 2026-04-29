#include "audio_intercom.h"

#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "audio";

#if CONFIG_CAR_ENABLE_INTERCOM

#define AUDIO_SAMPLE_RATE CONFIG_CAR_AUDIO_SAMPLE_RATE_HZ
#define AUDIO_FRAME_MS CONFIG_CAR_AUDIO_FRAME_MS
#define AUDIO_FRAME_SAMPLES ((AUDIO_SAMPLE_RATE * AUDIO_FRAME_MS) / 1000)
#define AUDIO_PCM16_FRAME_BYTES (AUDIO_FRAME_SAMPLES * sizeof(int16_t))
#define AUDIO_MIC_HANGOVER_FRAMES ((CONFIG_CAR_AUDIO_NOISE_HANGOVER_MS + AUDIO_FRAME_MS - 1) / AUDIO_FRAME_MS)

#if CONFIG_CAR_AUDIO_MIC_32BIT
#define AUDIO_MIC_I2S_BITS I2S_DATA_BIT_WIDTH_32BIT
#define AUDIO_MIC_SAMPLE_BYTES sizeof(int32_t)
#else
#define AUDIO_MIC_I2S_BITS I2S_DATA_BIT_WIDTH_16BIT
#define AUDIO_MIC_SAMPLE_BYTES sizeof(int16_t)
#endif

#ifdef CONFIG_CAR_AUDIO_MIC_RIGHT_SLOT
#define AUDIO_MIC_SLOT_MASK I2S_STD_SLOT_RIGHT
#else
#define AUDIO_MIC_SLOT_MASK I2S_STD_SLOT_LEFT
#endif

typedef struct {
    size_t len;
    uint8_t data[];
} audio_play_frame_t;

static i2s_chan_handle_t s_mic_chan;
static i2s_chan_handle_t s_spk_chan;
static TaskHandle_t s_mic_task;
static TaskHandle_t s_spk_task;
static QueueHandle_t s_play_queue;
static SemaphoreHandle_t s_ws_lock;
static httpd_handle_t s_ws_server;
static int s_ws_fd = -1;
static volatile int s_ws_pending;
static bool s_audio_ready;

static void audio_ws_send_done(esp_err_t err, int socket, void *arg)
{
    (void)err;
    (void)socket;
    free(arg);
    __atomic_sub_fetch(&s_ws_pending, 1, __ATOMIC_RELAXED);
}

static bool audio_get_ws_client(httpd_handle_t *server, int *fd)
{
    bool ok = false;

    if (s_ws_lock == NULL) {
        return false;
    }

    xSemaphoreTake(s_ws_lock, portMAX_DELAY);
    if (s_ws_server != NULL && s_ws_fd >= 0) {
        *server = s_ws_server;
        *fd = s_ws_fd;
        ok = true;
    }
    xSemaphoreGive(s_ws_lock);

    return ok;
}

static esp_err_t audio_send_to_phone(const int16_t *pcm, size_t sample_count)
{
#if CONFIG_HTTPD_WS_SUPPORT
    httpd_handle_t server = NULL;
    int fd = -1;
    const size_t len = sample_count * sizeof(int16_t);

    if (!audio_get_ws_client(&server, &fd)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (httpd_ws_get_fd_info(server, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        audio_intercom_clear_ws_client(fd);
        return ESP_ERR_INVALID_STATE;
    }
    if (__atomic_load_n(&s_ws_pending, __ATOMIC_RELAXED) >= CONFIG_CAR_AUDIO_WS_MAX_PENDING) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t *payload = malloc(len);
    if (payload == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(payload, pcm, len);

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_BINARY,
        .payload = payload,
        .len = len,
    };

    __atomic_add_fetch(&s_ws_pending, 1, __ATOMIC_RELAXED);
    esp_err_t ret = httpd_ws_send_data_async(server, fd, &frame, audio_ws_send_done, payload);
    if (ret != ESP_OK) {
        __atomic_sub_fetch(&s_ws_pending, 1, __ATOMIC_RELAXED);
        free(payload);
        if (ret == ESP_ERR_INVALID_ARG || ret == ESP_FAIL) {
            audio_intercom_clear_ws_client(fd);
        }
    }
    return ret;
#else
    (void)pcm;
    (void)sample_count;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static uint32_t pcm_mean_abs(const int16_t *pcm, size_t sample_count)
{
    uint64_t sum = 0;

    for (size_t i = 0; i < sample_count; i++) {
        int32_t sample = pcm[i];
        sum += (uint32_t)(sample < 0 ? -sample : sample);
    }

    return sample_count == 0 ? 0 : (uint32_t)(sum / sample_count);
}

static void convert_mic_to_pcm16(const void *mic_data, int16_t *pcm, size_t sample_count)
{
#if CONFIG_CAR_AUDIO_MIC_32BIT
    const int32_t *src = (const int32_t *)mic_data;
    for (size_t i = 0; i < sample_count; i++) {
        pcm[i] = (int16_t)(src[i] >> CONFIG_CAR_AUDIO_MIC_SHIFT_BITS);
    }
#else
    memcpy(pcm, mic_data, sample_count * sizeof(int16_t));
#endif
}

static void mic_task(void *arg)
{
    (void)arg;

    uint8_t *mic_buf = heap_caps_malloc(AUDIO_FRAME_SAMPLES * AUDIO_MIC_SAMPLE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int16_t *pcm_buf = heap_caps_malloc(AUDIO_PCM16_FRAME_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (mic_buf == NULL || pcm_buf == NULL) {
        ESP_LOGE(TAG, "audio capture buffer allocation failed");
        free(mic_buf);
        free(pcm_buf);
        vTaskDelete(NULL);
        return;
    }

    ESP_ERROR_CHECK(i2s_channel_enable(s_mic_chan));

    uint32_t hangover = 0;
    while (true) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_mic_chan, mic_buf,
                                         AUDIO_FRAME_SAMPLES * AUDIO_MIC_SAMPLE_BYTES,
                                         &bytes_read, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK || bytes_read == 0) {
            ESP_LOGW(TAG, "mic read failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        size_t sample_count = bytes_read / AUDIO_MIC_SAMPLE_BYTES;
        if (sample_count > AUDIO_FRAME_SAMPLES) {
            sample_count = AUDIO_FRAME_SAMPLES;
        }

        convert_mic_to_pcm16(mic_buf, pcm_buf, sample_count);
        uint32_t level = pcm_mean_abs(pcm_buf, sample_count);
        if (level >= CONFIG_CAR_AUDIO_NOISE_GATE_THRESHOLD) {
            hangover = AUDIO_MIC_HANGOVER_FRAMES;
        } else if (hangover > 0) {
            hangover--;
        }

        if (hangover > 0) {
            (void)audio_send_to_phone(pcm_buf, sample_count);
        }
    }
}

static void speaker_task(void *arg)
{
    (void)arg;

    ESP_ERROR_CHECK(i2s_channel_enable(s_spk_chan));

    while (true) {
        audio_play_frame_t *frame = NULL;
        if (xQueueReceive(s_play_queue, &frame, portMAX_DELAY) != pdTRUE || frame == NULL) {
            continue;
        }

        size_t offset = 0;
        while (offset < frame->len) {
            size_t bytes_written = 0;
            esp_err_t ret = i2s_channel_write(s_spk_chan, frame->data + offset,
                                              frame->len - offset, &bytes_written,
                                              pdMS_TO_TICKS(100));
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "speaker write failed: %s", esp_err_to_name(ret));
                break;
            }
            offset += bytes_written;
        }
        free(frame);
    }
}

static esp_err_t audio_init_i2s(void)
{
    i2s_chan_config_t mic_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    mic_chan_cfg.dma_frame_num = AUDIO_FRAME_SAMPLES;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&mic_chan_cfg, NULL, &s_mic_chan), TAG, "create mic channel");

    i2s_std_config_t mic_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(AUDIO_MIC_I2S_BITS, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_CAR_AUDIO_MIC_BCLK_GPIO,
            .ws = CONFIG_CAR_AUDIO_MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_CAR_AUDIO_MIC_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    mic_std_cfg.slot_cfg.slot_mask = AUDIO_MIC_SLOT_MASK;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_mic_chan, &mic_std_cfg), TAG, "init mic channel");

    i2s_chan_config_t spk_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    spk_chan_cfg.dma_frame_num = AUDIO_FRAME_SAMPLES;
    spk_chan_cfg.auto_clear_after_cb = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&spk_chan_cfg, &s_spk_chan, NULL), TAG, "create speaker channel");

    i2s_std_config_t spk_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_CAR_AUDIO_SPK_BCLK_GPIO,
            .ws = CONFIG_CAR_AUDIO_SPK_WS_GPIO,
            .dout = CONFIG_CAR_AUDIO_SPK_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    spk_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_spk_chan, &spk_std_cfg), TAG, "init speaker channel");

    return ESP_OK;
}

esp_err_t audio_intercom_init(void)
{
    if (s_audio_ready) {
        return ESP_OK;
    }

    if (AUDIO_FRAME_SAMPLES == 0) {
        ESP_LOGE(TAG, "invalid audio frame size");
        return ESP_ERR_INVALID_ARG;
    }

    s_ws_lock = xSemaphoreCreateMutex();
    s_play_queue = xQueueCreate(CONFIG_CAR_AUDIO_PLAY_QUEUE_DEPTH, sizeof(audio_play_frame_t *));
    if (s_ws_lock == NULL || s_play_queue == NULL) {
        ESP_LOGE(TAG, "audio synchronization allocation failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(audio_init_i2s(), TAG, "i2s init");

    BaseType_t mic_ok = xTaskCreatePinnedToCore(mic_task, "audio_mic",
                                                CONFIG_CAR_AUDIO_TASK_STACK_SIZE,
                                                NULL, 4, &s_mic_task, 0);
    BaseType_t spk_ok = xTaskCreatePinnedToCore(speaker_task, "audio_spk",
                                                CONFIG_CAR_AUDIO_TASK_STACK_SIZE,
                                                NULL, 4, &s_spk_task, 0);
    if (mic_ok != pdPASS || spk_ok != pdPASS) {
        ESP_LOGE(TAG, "audio task creation failed");
        return ESP_ERR_NO_MEM;
    }

    s_audio_ready = true;
    ESP_LOGI(TAG, "intercom ready: %d Hz, %d ms frame", AUDIO_SAMPLE_RATE, AUDIO_FRAME_MS);
    return ESP_OK;
}

esp_err_t audio_intercom_set_ws_client(httpd_handle_t server, int fd)
{
#if CONFIG_HTTPD_WS_SUPPORT
    if (!s_audio_ready || server == NULL || fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ws_lock, portMAX_DELAY);
    s_ws_server = server;
    s_ws_fd = fd;
    xSemaphoreGive(s_ws_lock);

    ESP_LOGI(TAG, "phone audio client connected: fd=%d", fd);
    return ESP_OK;
#else
    (void)server;
    (void)fd;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void audio_intercom_clear_ws_client(int fd)
{
    if (s_ws_lock == NULL) {
        return;
    }

    xSemaphoreTake(s_ws_lock, portMAX_DELAY);
    if (fd < 0 || fd == s_ws_fd) {
        ESP_LOGI(TAG, "phone audio client disconnected: fd=%d", s_ws_fd);
        s_ws_server = NULL;
        s_ws_fd = -1;
    }
    xSemaphoreGive(s_ws_lock);
}

esp_err_t audio_intercom_play_pcm(const uint8_t *pcm, size_t len)
{
    if (!s_audio_ready || pcm == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((len & 1U) != 0 || len > CONFIG_CAR_AUDIO_MAX_INCOMING_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    audio_play_frame_t *frame = malloc(sizeof(audio_play_frame_t) + len);
    if (frame == NULL) {
        return ESP_ERR_NO_MEM;
    }
    frame->len = len;
    memcpy(frame->data, pcm, len);

    if (xQueueSend(s_play_queue, &frame, 0) != pdTRUE) {
        free(frame);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

bool audio_intercom_is_ready(void)
{
    return s_audio_ready;
}

#else

esp_err_t audio_intercom_init(void)
{
    ESP_LOGI(TAG, "intercom disabled");
    return ESP_OK;
}

esp_err_t audio_intercom_set_ws_client(httpd_handle_t server, int fd)
{
    (void)server;
    (void)fd;
    return ESP_ERR_NOT_SUPPORTED;
}

void audio_intercom_clear_ws_client(int fd)
{
    (void)fd;
}

esp_err_t audio_intercom_play_pcm(const uint8_t *pcm, size_t len)
{
    (void)pcm;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
}

bool audio_intercom_is_ready(void)
{
    return false;
}

#endif
