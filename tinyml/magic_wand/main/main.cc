#include "esp_log.h"

namespace {
constexpr char kTag[] = "magic_wand";
}

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "magic_wand generic IMU gesture scaffold is ready");
  ESP_LOGI(kTag, "This directory is a standalone ESP32-S3 project skeleton");
  ESP_LOGI(kTag, "Suggested next steps:");
  ESP_LOGI(kTag, "1. Add IMU driver (I2C/SPI), e.g. MPU6050/ICM42670/LIS3DH");
  ESP_LOGI(kTag, "2. Sample 3-axis accelerometer data at a fixed rate");
  ESP_LOGI(kTag, "3. Build a gesture window buffer");
  ESP_LOGI(kTag, "4. Add a TFLite Micro gesture classification model");
  ESP_LOGI(kTag, "5. Run inference and map output to gesture labels");
}