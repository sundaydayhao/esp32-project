#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "demos/lv_demos.h"

static const char *TAG = "example";


#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (2 * 1000 * 1000)

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_DATA0 0
#define EXAMPLE_PIN_NUM_DATA1 1
#define EXAMPLE_PIN_NUM_DATA2 2
#define EXAMPLE_PIN_NUM_DATA3 3
#define EXAMPLE_PIN_NUM_DATA4 4
#define EXAMPLE_PIN_NUM_DATA5 5
#define EXAMPLE_PIN_NUM_DATA6 6
#define EXAMPLE_PIN_NUM_DATA7 7
#define EXAMPLE_PIN_NUM_DATA8 8
#define EXAMPLE_PIN_NUM_DATA9 9
#define EXAMPLE_PIN_NUM_DATA10 10
#define EXAMPLE_PIN_NUM_DATA11 11
#define EXAMPLE_PIN_NUM_DATA12 12
#define EXAMPLE_PIN_NUM_DATA13 13
#define EXAMPLE_PIN_NUM_DATA14 14
#define EXAMPLE_PIN_NUM_DATA15 15
#define EXAMPLE_PIN_NUM_PCLK 18
#define EXAMPLE_PIN_NUM_CS 17
#define EXAMPLE_PIN_NUM_DC 16
#define EXAMPLE_PIN_NUM_RST 20
#define EXAMPLE_PIN_NUM_BK_LIGHT 38

#define EXAMPLE_LCD_H_RES 800
#define EXAMPLE_LCD_V_RES 480

#define EXAMPLE_LCD_CMD_BITS 16
#define EXAMPLE_LCD_PARAM_BITS 16


#define EXAMPLE_LVGL_TICK_PERIOD_MS 2

#define EXAMPLE_PSRAM_DATA_ALIGNMENT 64

extern void example_lvgl_demo_ui(lv_disp_t *disp);

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
  lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
  lv_disp_flush_ready(disp_driver);
  return false;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
  esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
  int offsetx1 = area->x1;
  int offsetx2 = area->x2;
  int offsety1 = area->y1;
  int offsety2 = area->y2;
  // copy a buffer's content to a specific area of the display
  esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint16_t touchpad_x[1] = { 0 };
  uint16_t touchpad_y[1] = { 0 };
  uint8_t touchpad_cnt = 0;

  /* Read touch controller data */
  esp_lcd_touch_read_data(drv->user_data);

  /* Get coordinates */
  bool touchpad_pressed = esp_lcd_touch_get_coordinates(drv->user_data, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

  if (touchpad_pressed && touchpad_cnt > 0) {
    data->point.x = touchpad_x[0];
    data->point.y = touchpad_y[0];
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
#endif

static void example_increase_lvgl_tick(void *arg) {
  /* Tell LVGL how many milliseconds has elapsed */
  lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}


void setup() {
  Serial.begin(115200);

  pinMode(19, OUTPUT);
  digitalWrite(19, HIGH);

  //屏幕背光
  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);



  static lv_disp_draw_buf_t disp_buf;  // contains internal graphic buffer(s) called draw buffer(s)
  static lv_disp_drv_t disp_drv;       // contains callback functions


  esp_lcd_i80_bus_handle_t i80_bus = NULL;
  esp_lcd_i80_bus_config_t bus_config = {
    .dc_gpio_num = EXAMPLE_PIN_NUM_DC,
    .wr_gpio_num = EXAMPLE_PIN_NUM_PCLK,
    .clk_src = LCD_CLK_SRC_DEFAULT,
    .data_gpio_nums = {
      EXAMPLE_PIN_NUM_DATA0,
      EXAMPLE_PIN_NUM_DATA1,
      EXAMPLE_PIN_NUM_DATA2,
      EXAMPLE_PIN_NUM_DATA3,
      EXAMPLE_PIN_NUM_DATA4,
      EXAMPLE_PIN_NUM_DATA5,
      EXAMPLE_PIN_NUM_DATA6,
      EXAMPLE_PIN_NUM_DATA7,
      EXAMPLE_PIN_NUM_DATA8,
      EXAMPLE_PIN_NUM_DATA9,
      EXAMPLE_PIN_NUM_DATA10,
      EXAMPLE_PIN_NUM_DATA11,
      EXAMPLE_PIN_NUM_DATA12,
      EXAMPLE_PIN_NUM_DATA13,
      EXAMPLE_PIN_NUM_DATA14,
      EXAMPLE_PIN_NUM_DATA15,
    },
    .bus_width = 16,
    .max_transfer_bytes = EXAMPLE_LCD_H_RES * 100 * sizeof(uint16_t),
    .psram_trans_align = EXAMPLE_PSRAM_DATA_ALIGNMENT,
    .sram_trans_align = 4,
  };
  esp_lcd_new_i80_bus(&bus_config, &i80_bus);
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_i80_config_t io_config = {
    .cs_gpio_num = EXAMPLE_PIN_NUM_CS,
    .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
    .trans_queue_depth = 10,
    .on_color_trans_done = example_notify_lvgl_flush_ready,
    .user_ctx = &disp_drv,
    .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
    .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
    .dc_levels = {
      .dc_idle_level = 0,
      .dc_cmd_level = 0,
      .dc_dummy_level = 0,
      .dc_data_level = 1,
    },
    .flags = {
      .swap_color_bytes = 0,  // Swap can be done in LvGL (default) or DMA
    },
  };
  esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle);

  esp_lcd_panel_handle_t panel_handle = NULL;

  esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,
  };
  esp_lcd_new_panel_nt35510(io_handle, &panel_config, &panel_handle);

  esp_lcd_panel_reset(panel_handle);
  esp_lcd_panel_init(panel_handle);
  // Set inversion, x/y coordinate order, x/y mirror according to your LCD module spec
  // the gap is LCD panel specific, even panels with the same driver IC, can have different gap value
  esp_lcd_panel_swap_xy(panel_handle, true);
  esp_lcd_panel_mirror(panel_handle, true, false);


  // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
  esp_lcd_panel_disp_on_off(panel_handle, true);




  lv_init();



  static lv_color_t buf1[EXAMPLE_LCD_H_RES * 30];
  lv_disp_draw_buf_init(&disp_buf, buf1, NULL, EXAMPLE_LCD_H_RES * 30);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = EXAMPLE_LCD_H_RES;
  disp_drv.ver_res = EXAMPLE_LCD_V_RES;
  disp_drv.flush_cb = example_lvgl_flush_cb;
  disp_drv.draw_buf = &disp_buf;
  disp_drv.user_data = panel_handle;
  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

  // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t lvgl_tick_timer = NULL;
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);


  //下面编写自己的屏幕UI代码
   lv_demo_widgets();


  // lv_obj_t *label;

  // lv_obj_t *btn1 = lv_btn_create(lv_scr_act());
  // lv_obj_align(btn1, LV_ALIGN_CENTER, 0, -40);

  // label = lv_label_create(btn1);
  // lv_label_set_text(label, "Button");
  // lv_obj_center(label);

  // lv_obj_t *btn2 = lv_btn_create(lv_scr_act());
  // lv_obj_align(btn2, LV_ALIGN_CENTER, 0, 40);
  // lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
  // lv_obj_set_height(btn2, LV_SIZE_CONTENT);

  // label = lv_label_create(btn2);
  // lv_label_set_text(label, "Toggle");
  // lv_obj_center(label);
}

void loop() {

  delay(5);
  lv_timer_handler();
}
