#include "rtlsdr_usb.h"
#include "adsb/adsb_decoder.h"
#include "config.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "usb/usb_host.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "rtlsdr";

// ---------------------------------------------------------------------------
// RTL2832U 寄存器块定义
// ---------------------------------------------------------------------------
#define BLOCK_DEMOD     0
#define BLOCK_USB       1
#define BLOCK_SYS       2
#define BLOCK_TUN       3
#define BLOCK_ROM       5

#define R820T2_I2C_ADDR 0x34

#define CTRL_IN  (USB_BM_REQUEST_TYPE_DIR_IN  | USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_DEVICE)
#define CTRL_OUT (USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_DEVICE)

#define IQ_BUF_SIZE         (16 * 1024)
#define IQ_NUM_BUFS         4
#define ADSB_LONG_BITS      112
#define ADSB_SHORT_BITS     56

// ---------------------------------------------------------------------------
// 内部状态
// ---------------------------------------------------------------------------
static rtlsdr_state_t           s_state      = RTLSDR_STATE_DISCONNECTED;
static uint32_t                 s_freq_hz    = RTLSDR_ADSB_FREQ;
static usb_host_client_handle_t s_client_hdl = NULL;
static usb_device_handle_t      s_dev_hdl    = NULL;
static EventGroupHandle_t       s_event_group = NULL;
static bool                     s_streaming  = false;

// IQ 数据回调（NULL = 使用内置 ADS-B 解码）
static rtlsdr_iq_callback_t s_iq_callback = NULL;

#define EVT_DEVICE_CONNECTED    BIT0
#define EVT_DEVICE_DISCONNECTED BIT1

// ---------------------------------------------------------------------------
// USB 控制传输（同步）
// ---------------------------------------------------------------------------

typedef struct {
    SemaphoreHandle_t sem;
    esp_err_t         result;
} ctrl_ctx_t;

static void ctrl_transfer_cb(usb_transfer_t *transfer)
{
    ctrl_ctx_t *ctx = (ctrl_ctx_t *)transfer->context;
    ctx->result = (transfer->status == USB_TRANSFER_STATUS_COMPLETED) ? ESP_OK : ESP_FAIL;
    xSemaphoreGive(ctx->sem);
}

static esp_err_t rtlsdr_ctrl_transfer(uint8_t bmRequestType, uint8_t bRequest,
                                       uint16_t wValue, uint16_t wIndex,
                                       uint8_t *data, uint16_t wLength)
{
    if (s_dev_hdl == NULL) return ESP_ERR_INVALID_STATE;

    usb_transfer_t *xfer = NULL;
    esp_err_t ret = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + wLength, 0, &xfer);
    if (ret != ESP_OK) return ret;

    ctrl_ctx_t ctx = {
        .sem    = xSemaphoreCreateBinary(),
        .result = ESP_FAIL,
    };

    xfer->device_handle    = s_dev_hdl;
    xfer->bEndpointAddress = 0;
    xfer->callback         = ctrl_transfer_cb;
    xfer->context          = &ctx;
    xfer->num_bytes        = sizeof(usb_setup_packet_t) + wLength;
    xfer->timeout_ms       = 1000;

    usb_setup_packet_t *setup = (usb_setup_packet_t *)xfer->data_buffer;
    setup->bmRequestType = bmRequestType;
    setup->bRequest      = bRequest;
    setup->wValue        = wValue;
    setup->wIndex        = wIndex;
    setup->wLength       = wLength;

    if (!(bmRequestType & USB_BM_REQUEST_TYPE_DIR_IN) && data && wLength > 0) {
        memcpy(xfer->data_buffer + sizeof(usb_setup_packet_t), data, wLength);
    }

    ret = usb_host_transfer_submit_control(s_client_hdl, xfer);
    if (ret == ESP_OK) {
        xSemaphoreTake(ctx.sem, pdMS_TO_TICKS(2000));
        ret = ctx.result;
        if ((bmRequestType & USB_BM_REQUEST_TYPE_DIR_IN) && data && wLength > 0 && ret == ESP_OK) {
            memcpy(data, xfer->data_buffer + sizeof(usb_setup_packet_t), wLength);
        }
    }

    vSemaphoreDelete(ctx.sem);
    usb_host_transfer_free(xfer);
    return ret;
}

// ---------------------------------------------------------------------------
// RTL2832U 寄存器读写
// ---------------------------------------------------------------------------

static esp_err_t rtl_write_reg(uint8_t block, uint16_t addr, uint16_t val, uint8_t len)
{
    uint16_t index = (block << 8) | (addr >> 8);
    uint8_t data[2];
    if (len == 1) { data[0] = val & 0xFF; }
    else          { data[0] = (val >> 8) & 0xFF; data[1] = val & 0xFF; }
    return rtlsdr_ctrl_transfer(CTRL_OUT, 0x00, addr & 0xFF, index, data, len);
}

static esp_err_t rtl_read_reg(uint8_t block, uint16_t addr, uint8_t *val, uint8_t len)
{
    uint16_t index = (block << 8) | (addr >> 8);
    return rtlsdr_ctrl_transfer(CTRL_IN, 0x00, addr & 0xFF, index, val, len);
}

static esp_err_t rtl_demod_write(uint8_t page, uint16_t addr, uint8_t val)
{
    return rtl_write_reg(BLOCK_DEMOD, (page << 8) | addr, val, 1);
}

// ---------------------------------------------------------------------------
// R820T2 调谐器
// ---------------------------------------------------------------------------

static esp_err_t r820t2_write_reg(uint8_t reg, uint8_t val)
{
    esp_err_t ret = rtl_write_reg(BLOCK_TUN, 0x00, R820T2_I2C_ADDR, 1);
    if (ret != ESP_OK) return ret;
    return rtlsdr_ctrl_transfer(CTRL_OUT, 0x00, reg, (BLOCK_TUN << 8) | 0x01, &val, 1);
}

static const uint8_t r820t2_init_regs[][2] = {
    {0x05, 0x90}, {0x06, 0x80}, {0x07, 0x60}, {0x08, 0x80},
    {0x09, 0x40}, {0x0A, 0xA0}, {0x0B, 0x6F}, {0x0C, 0x40},
    {0x0D, 0x63}, {0x0E, 0x75}, {0x0F, 0x68}, {0x10, 0x6C},
    {0x11, 0xE3}, {0x12, 0x53}, {0x13, 0x75}, {0x14, 0x68},
    {0x15, 0x6C}, {0x16, 0xE3}, {0x17, 0x53}, {0x18, 0x75},
    {0x19, 0x68}, {0x1A, 0x6C}, {0x1B, 0xE3}, {0x1C, 0x53},
};

static esp_err_t r820t2_init(void)
{
    ESP_LOGI(TAG, "Initializing R820T2 tuner...");
    for (int i = 0; i < (int)(sizeof(r820t2_init_regs) / sizeof(r820t2_init_regs[0])); i++) {
        esp_err_t ret = r820t2_write_reg(r820t2_init_regs[i][0], r820t2_init_regs[i][1]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "R820T2 init failed at reg 0x%02X", r820t2_init_regs[i][0]);
            return ret;
        }
    }
    ESP_LOGI(TAG, "R820T2 initialized");
    return ESP_OK;
}

static esp_err_t r820t2_set_freq(uint32_t freq_hz)
{
    ESP_LOGI(TAG, "Setting R820T2 to %lu Hz", (unsigned long)freq_hz);

    uint32_t vco_freq = freq_hz + RTLSDR_IF_FREQ;
    uint32_t pll_ref  = RTLSDR_XTAL_FREQ;
    uint32_t n_int    = vco_freq / (2 * pll_ref);
    uint32_t n_frac   = (uint32_t)(((uint64_t)(vco_freq % (2 * pll_ref)) * 65536) / (2 * pll_ref));

    uint8_t reg10 = ((n_int & 0x3F) << 1) | ((n_frac >> 15) & 0x01);
    uint8_t reg11 = (n_frac >> 7) & 0xFF;
    uint8_t reg12 = ((n_frac & 0x7F) << 1);

    esp_err_t ret;
    ret = r820t2_write_reg(0x10, reg10); if (ret != ESP_OK) return ret;
    ret = r820t2_write_reg(0x11, reg11); if (ret != ESP_OK) return ret;
    ret = r820t2_write_reg(0x12, reg12); if (ret != ESP_OK) return ret;

    // 带宽：ADS-B(1090MHz) 用 1.75MHz，FM 用宽带
    uint8_t bw_reg = (freq_hz >= 1000000000UL) ? 0x6F : 0x0F;
    ret = r820t2_write_reg(0x0B, bw_reg); if (ret != ESP_OK) return ret;
    ret = r820t2_write_reg(0x05, 0x90);   if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "R820T2 PLL: N=%lu frac=%lu", (unsigned long)n_int, (unsigned long)n_frac);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// RTL2832U 初始化
// ---------------------------------------------------------------------------

static esp_err_t rtl2832u_init(void)
{
    ESP_LOGI(TAG, "Initializing RTL2832U...");
    esp_err_t ret;

    ret = rtl_write_reg(BLOCK_USB, 0x0010, 0x01, 1);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "USB init failed"); return ret; }

    ret = rtl_demod_write(1, 0x01, 0x14); if (ret != ESP_OK) return ret;
    ret = rtl_demod_write(1, 0x01, 0x10); if (ret != ESP_OK) return ret;

    uint32_t rsamp_ratio = (uint32_t)((RTLSDR_XTAL_FREQ * 2) / RTLSDR_SAMPLE_RATE);
    rsamp_ratio &= 0x0FFFFFFC;
    uint32_t real_rsamp_ratio = rsamp_ratio | ((rsamp_ratio & 0x08000000) << 1);
    double real_rate = (RTLSDR_XTAL_FREQ * 2.0) / real_rsamp_ratio;
    ESP_LOGI(TAG, "Sample rate: %.0f Hz", real_rate);

    ret = rtl_demod_write(1, 0x19, (rsamp_ratio >> 24) & 0xFF); if (ret != ESP_OK) return ret;
    ret = rtl_demod_write(1, 0x1A, (rsamp_ratio >> 16) & 0xFF); if (ret != ESP_OK) return ret;
    ret = rtl_demod_write(1, 0x1B, (rsamp_ratio >>  8) & 0xFF); if (ret != ESP_OK) return ret;
    ret = rtl_demod_write(1, 0x1C,  rsamp_ratio        & 0xFF); if (ret != ESP_OK) return ret;
    ret = rtl_demod_write(1, 0x15, 0x00);                       if (ret != ESP_OK) return ret;
    ret = rtl_write_reg(BLOCK_SYS, 0x0002, 0x01, 1);            if (ret != ESP_OK) return ret;

    ret = r820t2_init();                    if (ret != ESP_OK) return ret;
    ret = r820t2_set_freq(RTLSDR_ADSB_FREQ); if (ret != ESP_OK) return ret;

    s_state = RTLSDR_STATE_CONFIGURED;
    ESP_LOGI(TAG, "RTL2832U ready @ %.1f MHz", RTLSDR_ADSB_FREQ / 1e6);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// IQ 数据处理 —— ADS-B 解码（无外部回调时使用）
// ---------------------------------------------------------------------------

static void process_iq_adsb(const uint8_t *buf, size_t len)
{
    size_t num_samples = len / 2;
    if (num_samples < 16) return;

    static uint16_t mag[IQ_BUF_SIZE / 2];
    size_t mag_len = (num_samples < IQ_BUF_SIZE / 2) ? num_samples : IQ_BUF_SIZE / 2;

    for (size_t i = 0; i < mag_len; i++) {
        int16_t I  = (int16_t)buf[i * 2]     - 127;
        int16_t Q  = (int16_t)buf[i * 2 + 1] - 127;
        uint16_t ai = (uint16_t)(I < 0 ? -I : I);
        uint16_t aq = (uint16_t)(Q < 0 ? -Q : Q);
        mag[i] = (ai > aq) ? (ai + (aq * 2 / 5)) : (aq + (ai * 2 / 5));
    }

    uint32_t sum = 0;
    for (size_t i = 0; i < mag_len; i++) sum += mag[i];
    uint16_t mean      = (uint16_t)(sum / mag_len);
    uint16_t threshold = mean * 2;

    for (size_t i = 0; i + 16 + ADSB_LONG_BITS * 2 < mag_len; i++) {
        if (mag[i]   < threshold) continue;
        if (mag[i+1] > threshold) continue;
        if (mag[i+2] < threshold) continue;
        if (mag[i+3] > threshold) continue;
        if (mag[i+4] > threshold) continue;
        if (mag[i+5] > threshold) continue;
        if (mag[i+6] > threshold) continue;
        if (mag[i+7] < threshold) continue;
        if (mag[i+8] > threshold) continue;
        if (mag[i+9] < threshold) continue;

        size_t data_start = i + 16;
        uint8_t first_byte = 0;
        for (int b = 0; b < 8; b++) {
            size_t t = data_start + b * 2;
            if (t + 1 >= mag_len) goto next;
            if (mag[t] > mag[t + 1]) first_byte |= (1 << (7 - b));
        }

        {
            int df = (first_byte >> 3) & 0x1F;
            int msg_bits;
            switch (df) {
                case 0: case 4: case 5: case 11:
                    msg_bits = ADSB_SHORT_BITS; break;
                case 16: case 17: case 18: case 19:
                case 20: case 21: case 24:
                    msg_bits = ADSB_LONG_BITS; break;
                default: goto next;
            }
            if (data_start + msg_bits * 2 > mag_len) goto next;

            uint8_t msg[14] = {0};
            for (int b = 0; b < msg_bits; b++) {
                size_t t = data_start + b * 2;
                if (mag[t] > mag[t + 1]) msg[b / 8] |= (1 << (7 - (b % 8)));
            }
            adsb_inject_raw_message(msg, msg_bits / 8);
            i += 16 + msg_bits * 2 - 1;
        }
        next:;
    }
}

// ---------------------------------------------------------------------------
// USB Bulk 传输回调
// ---------------------------------------------------------------------------

static void bulk_transfer_cb(usb_transfer_t *transfer)
{
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && s_streaming) {
        if (s_iq_callback != NULL) {
            s_iq_callback(transfer->data_buffer, transfer->actual_num_bytes);
        } else {
            process_iq_adsb(transfer->data_buffer, transfer->actual_num_bytes);
        }
        usb_host_transfer_submit(transfer);
    } else if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "Bulk transfer error: %d", transfer->status);
        if (s_streaming) {
            vTaskDelay(pdMS_TO_TICKS(10));
            usb_host_transfer_submit(transfer);
        }
    }
}

// ---------------------------------------------------------------------------
// USB Host 客户端事件回调
// ---------------------------------------------------------------------------

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        ESP_LOGI(TAG, "USB device connected, address=%d", event_msg->new_dev.address);
        xEventGroupSetBits(s_event_group, EVT_DEVICE_CONNECTED);
    } else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGI(TAG, "USB device disconnected");
        s_state     = RTLSDR_STATE_DISCONNECTED;
        s_streaming = false;
        s_dev_hdl   = NULL;
        xEventGroupSetBits(s_event_group, EVT_DEVICE_DISCONNECTED);
    }
}

// ---------------------------------------------------------------------------
// USB Host 主任务
// ---------------------------------------------------------------------------

static void usb_host_task(void *arg)
{
    ESP_LOGI(TAG, "USB Host task started");

    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    usb_host_client_config_t client_config = {
        .is_synchronous    = false,
        .max_num_event_msg = 8,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg          = NULL,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &s_client_hdl));

    ESP_LOGI(TAG, "Waiting for RTL-SDR (VID:0x%04X)...", RTLSDR_VID);

    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(pdMS_TO_TICKS(10), &event_flags);
        usb_host_client_handle_events(s_client_hdl, pdMS_TO_TICKS(10));

        EventBits_t bits = xEventGroupGetBits(s_event_group);

        if (bits & EVT_DEVICE_CONNECTED) {
            xEventGroupClearBits(s_event_group, EVT_DEVICE_CONNECTED);

            uint8_t dev_addr_list[8];
            int num_devs = 0;
            usb_host_device_addr_list_fill(sizeof(dev_addr_list), dev_addr_list, &num_devs);

            for (int i = 0; i < num_devs; i++) {
                usb_device_handle_t dev_hdl;
                if (usb_host_device_open(s_client_hdl, dev_addr_list[i], &dev_hdl) != ESP_OK) continue;

                const usb_device_desc_t *dev_desc;
                usb_host_get_device_descriptor(dev_hdl, &dev_desc);
                ESP_LOGI(TAG, "Device: VID=0x%04X PID=0x%04X",
                         dev_desc->idVendor, dev_desc->idProduct);

                if (dev_desc->idVendor == RTLSDR_VID &&
                    (dev_desc->idProduct == RTLSDR_PID_2838 ||
                     dev_desc->idProduct == RTLSDR_PID_2832 ||
                     dev_desc->idProduct == RTLSDR_PID_2837)) {

                    ESP_LOGI(TAG, "RTL-SDR detected! PID=0x%04X", dev_desc->idProduct);
                    s_dev_hdl = dev_hdl;
                    s_state   = RTLSDR_STATE_CONNECTED;

                    const usb_config_desc_t *config_desc;
                    usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
                    usb_host_interface_claim(s_client_hdl, dev_hdl, 0, 0);

                    if (rtl2832u_init() == ESP_OK) {
                        ESP_LOGI(TAG, "RTL-SDR ready, starting ADS-B streaming...");
                        rtlsdr_start_streaming();
                    }
                } else {
                    usb_host_device_close(s_client_hdl, dev_hdl);
                }
            }
        }

        if (bits & EVT_DEVICE_DISCONNECTED) {
            xEventGroupClearBits(s_event_group, EVT_DEVICE_DISCONNECTED);
            ESP_LOGW(TAG, "RTL-SDR disconnected, waiting for reconnection...");
        }
    }
}

// ---------------------------------------------------------------------------
// 公开 API
// ---------------------------------------------------------------------------

esp_err_t rtlsdr_start_streaming(void)
{
    if (s_dev_hdl == NULL || s_state < RTLSDR_STATE_CONFIGURED) {
        return ESP_ERR_INVALID_STATE;
    }

    usb_transfer_t *xfer = NULL;
    esp_err_t ret = usb_host_transfer_alloc(IQ_BUF_SIZE, 0, &xfer);
    if (ret != ESP_OK) return ret;

    xfer->device_handle    = s_dev_hdl;
    xfer->bEndpointAddress = 0x81;   // EP1 IN
    xfer->callback         = bulk_transfer_cb;
    xfer->context          = NULL;
    xfer->num_bytes        = IQ_BUF_SIZE;
    xfer->timeout_ms       = 0;

    s_streaming = true;
    s_state     = RTLSDR_STATE_STREAMING;

    ret = usb_host_transfer_submit(xfer);
    if (ret != ESP_OK) {
        s_streaming = false;
        s_state     = RTLSDR_STATE_CONFIGURED;
        usb_host_transfer_free(xfer);
        ESP_LOGE(TAG, "Failed to start bulk transfer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Streaming started @ %.3f MHz", s_freq_hz / 1e6);
    return ESP_OK;
}

void rtlsdr_stop_streaming(void)
{
    s_streaming = false;
    s_state = (s_dev_hdl != NULL) ? RTLSDR_STATE_CONFIGURED : RTLSDR_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "Streaming stopped");
}

esp_err_t rtlsdr_set_freq(uint32_t freq_hz)
{
    if (s_state < RTLSDR_STATE_CONFIGURED) return ESP_ERR_INVALID_STATE;
    s_freq_hz = freq_hz;
    ESP_LOGI(TAG, "Switching to %.3f MHz", freq_hz / 1e6);
    return r820t2_set_freq(freq_hz);
}

void rtlsdr_set_iq_callback(rtlsdr_iq_callback_t cb)
{
    s_iq_callback = cb;
    ESP_LOGI(TAG, "IQ callback %s", cb ? "set (FM mode)" : "cleared (ADS-B mode)");
}

rtlsdr_state_t rtlsdr_get_state(void) { return s_state; }
uint32_t       rtlsdr_get_freq(void)  { return s_freq_hz; }

esp_err_t rtlsdr_init(void)
{
    ESP_LOGI(TAG, "Initializing RTL-SDR USB Host driver");
    ESP_LOGI(TAG, "Connect: D+->GPIO20, D-->GPIO19, GND->GND, VBUS->5V");

    s_event_group = xEventGroupCreate();
    if (!s_event_group) return ESP_ERR_NO_MEM;

    xTaskCreate(usb_host_task, "usb_host", 8192, NULL, 8, NULL);
    ESP_LOGI(TAG, "USB Host task started, waiting for RTL-SDR...");
    return ESP_OK;
}