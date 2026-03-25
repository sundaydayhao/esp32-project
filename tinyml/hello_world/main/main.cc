#include <math.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_data.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {
constexpr char kTag[] = "tinyml_hello_world";
constexpr int kTensorArenaSize = 8 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

float DequantizeInt8(int8_t value, float scale, int zero_point) {
  return (static_cast<int>(value) - zero_point) * scale;
}

int8_t QuantizeToInt8(float value, float scale, int zero_point) {
  int32_t quantized = static_cast<int32_t>(roundf(value / scale)) + zero_point;
  if (quantized > 127) {
    quantized = 127;
  }
  if (quantized < -128) {
    quantized = -128;
  }
  return static_cast<int8_t>(quantized);
}
}  // namespace

extern "C" void app_main(void) {
  const tflite::Model* model = tflite::GetModel(g_model);
  if (model == nullptr) {
    ESP_LOGE(kTag, "Failed to map TFLite model");
    return;
  }

  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(kTag, "Model schema mismatch: %d != %d", model->version(),
             TFLITE_SCHEMA_VERSION);
    return;
  }

  tflite::MicroMutableOpResolver<1> op_resolver;
  if (op_resolver.AddFullyConnected() != kTfLiteOk) {
    ESP_LOGE(kTag, "Failed to register FullyConnected op");
    return;
  }

  tflite::MicroInterpreter interpreter(model, op_resolver, tensor_arena,
                                       kTensorArenaSize);

  if (interpreter.AllocateTensors() != kTfLiteOk) {
    ESP_LOGE(kTag, "AllocateTensors failed, arena=%d bytes", kTensorArenaSize);
    return;
  }

  TfLiteTensor* input = interpreter.input(0);
  TfLiteTensor* output = interpreter.output(0);
  if (input == nullptr || output == nullptr) {
    ESP_LOGE(kTag, "Input/output tensor unavailable");
    return;
  }

  ESP_LOGI(kTag, "ESP32-S3 TFLite Micro hello_world started");
  ESP_LOGI(kTag,
           "Input: type=%d scale=%f zero_point=%d | Output: type=%d scale=%f "
           "zero_point=%d",
           input->type, input->params.scale, input->params.zero_point,
           output->type, output->params.scale, output->params.zero_point);

  while (true) {
    for (float x = 0.0f; x <= 6.28f; x += 0.314f) {
      input->data.int8[0] =
          QuantizeToInt8(x, input->params.scale, input->params.zero_point);

      if (interpreter.Invoke() != kTfLiteOk) {
        ESP_LOGE(kTag, "Invoke failed at x=%f", x);
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }

      const float y_pred = DequantizeInt8(output->data.int8[0],
                                          output->params.scale,
                                          output->params.zero_point);
      const float y_true = sinf(x);

      ESP_LOGI(kTag, "x=%0.3f, y_pred=%0.5f, sin(x)=%0.5f, err=%0.5f", x,
               y_pred, y_true, fabsf(y_true - y_pred));
      vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGI(kTag, "One inference sweep completed");
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}