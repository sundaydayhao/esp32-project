#include <stdint.h>

#include "esp_log.h"
#include "model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {
constexpr char kTag[] = "person_detection";
constexpr int kTensorArenaSize = 160 * 1024;
uint8_t g_tensor_arena[kTensorArenaSize];
}  // namespace

extern "C" void app_main(void) {
  const tflite::Model* model = tflite::GetModel(g_person_detect_model);
  if (model == nullptr) {
    ESP_LOGE(kTag, "Failed to map model");
    return;
  }

  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(kTag, "Model schema mismatch");
    return;
  }

  tflite::MicroMutableOpResolver<6> op_resolver;
  op_resolver.AddAveragePool2D();
  op_resolver.AddConv2D();
  op_resolver.AddDepthwiseConv2D();
  op_resolver.AddReshape();
  op_resolver.AddSoftmax();
  op_resolver.AddQuantize();

  tflite::MicroInterpreter interpreter(model, op_resolver, g_tensor_arena,
                                       kTensorArenaSize);

  if (interpreter.AllocateTensors() != kTfLiteOk) {
    ESP_LOGE(kTag, "AllocateTensors failed");
    return;
  }

  TfLiteTensor* input = interpreter.input(0);
  TfLiteTensor* output = interpreter.output(0);
  if (input == nullptr || output == nullptr) {
    ESP_LOGE(kTag, "Input/output tensor unavailable");
    return;
  }

  ESP_LOGI(kTag, "person_detection project scaffold is ready");
  ESP_LOGI(kTag, "Model bytes: %u", g_person_detect_model_len);
  ESP_LOGI(kTag, "Input shape expects a grayscale image frame for inference");
  ESP_LOGI(kTag,
           "Next step: connect ESP32-S3 camera input, resize to model format, "
           "then invoke the interpreter and decode person/no-person scores.");
}