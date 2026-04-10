#include "joystick.h"

#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* -----------------------------------------------------------------------
 * Hardware pin configuration – change these to match your wiring
 * -----------------------------------------------------------------------
 * Typical KY-023 / PS2 joystick module:
 *   VCC  -> 3.3 V
 *   GND  -> GND
 *   VRx  -> GPIO34  (ADC1_CH6)
 *   VRy  -> GPIO35  (ADC1_CH7)
 *   SW   -> GPIO0   (active LOW)
 * ----------------------------------------------------------------------- */
#define JOY_X_CHANNEL   ADC1_CHANNEL_6   /* GPIO34 */
#define JOY_Y_CHANNEL   ADC1_CHANNEL_7   /* GPIO35 */
#define JOY_BTN_GPIO    GPIO_NUM_0

/* ADC thresholds (12-bit, 0-4095).
 * Centre is ~2048.  Adjust if your joystick has a different centre. */
#define JOY_LOW_THRESH  1000
#define JOY_HIGH_THRESH 3000

static const char *TAG = "joystick";

void joystick_init(void)
{
    /* Configure ADC */
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(JOY_X_CHANNEL, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(JOY_Y_CHANNEL, ADC_ATTEN_DB_11);

    /* Configure button pin */
    gpio_config_t io_conf = {
        .pin_bit_mask  = (1ULL << JOY_BTN_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Joystick initialised  X=GPIO%d  Y=GPIO%d  BTN=GPIO%d",
             34, 35, JOY_BTN_GPIO);
}

joystick_dir_t joystick_read(void)
{
    /* Button has priority */
    if (gpio_get_level(JOY_BTN_GPIO) == 0) {
        return JOY_BUTTON;
    }

    int x = adc1_get_raw(JOY_X_CHANNEL);
    int y = adc1_get_raw(JOY_Y_CHANNEL);

    /* Prefer the axis with the larger deflection */
    int dx = x - 2048;
    int dy = y - 2048;

    if (abs(dx) > abs(dy)) {
        if (x < JOY_LOW_THRESH)  return JOY_LEFT;
        if (x > JOY_HIGH_THRESH) return JOY_RIGHT;
    } else {
        if (y < JOY_LOW_THRESH)  return JOY_UP;
        if (y > JOY_HIGH_THRESH) return JOY_DOWN;
    }

    return JOY_NONE;
}

const char *joystick_dir_to_json(joystick_dir_t dir)
{
    switch (dir) {
        case JOY_UP:     return "{\"dir\":\"UP\"}";
        case JOY_DOWN:   return "{\"dir\":\"DOWN\"}";
        case JOY_LEFT:   return "{\"dir\":\"LEFT\"}";
        case JOY_RIGHT:  return "{\"dir\":\"RIGHT\"}";
        case JOY_BUTTON: return "{\"dir\":\"BUTTON\"}";
        default:         return "{\"dir\":\"NONE\"}";
    }
}