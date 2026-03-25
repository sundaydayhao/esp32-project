#include "esp_log.h"
#include "model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {
constexpr char kTag[] = "micro_speech";
constexpr int kPreprocessorArenaSize = 32 * 1024;
constexpr int kClassifierArenaSize = 32 * 1024;

uint8_t g_preprocessor_arena[kPreprocessorArenaSize];
uint8_t g_classifier_arena[kClassifierArenaSize];
}  // namespace

extern "C" void app_main(void) {
  const tflite::Model* preprocessor_model =
      tflite::GetModel(g_audio_preprocessor_model);
  const tflite::Model* classifier_model =
      tflite::GetModel(g_micro_speech_model);

  if (preprocessor_model == nullptr || classifier_model == nullptr) {
    ESP_LOGE(kTag, "Model mapping failed");
    return;
  }

  if (preprocessor_model->version() != TFLITE_SCHEMA_VERSION ||
      classifier_model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(kTag, "Model schema mismatch");
    return;
  }

  tflite::MicroMutableOpResolver<16> preprocessor_ops;
  tflite::MicroMutableOpResolver<5> classifier_ops;

  classifier_ops.AddConv2D();
  classifier_ops.AddDepthwiseConv2D();
  classifier_ops.AddFullyConnected();
  classifier_ops.AddReshape();
  classifier_ops.AddSoftmax();

  tflite::MicroInterpreter preprocessor_interpreter(
      preprocessor_model, preprocessor_ops, g_preprocessor_arena,
      kPreprocessorArenaSize);
  tflite::MicroInterpreter classifier_interpreter(
      classifier_model, classifier_ops, g_classifier_arena,
      kClassifierArenaSize);

  if (preprocessor_interpreter.AllocateTensors() != kTfLiteOk) {
    ESP_LOGE(kTag, "AllocateTensors failed for audio preprocessor");
    return;
  }

  if (classifier_interpreter.AllocateTensors() != kTfLiteOk) {
    ESP_LOGE(kTag, "AllocateTensors failed for classifier");
    return;
  }

  ESP_LOGI(kTag, "micro_speech project scaffold is ready");
  ESP_LOGI(kTag, "Preprocessor model bytes: %u", g_audio_preprocessor_model_len);
  ESP_LOGI(kTag, "Classifier model bytes: %u", g_micro_speech_model_len);
  ESP_LOGI(
      kTag,
      "Next step: connect I2S microphone capture and register required signal "
      "ops supported by esp-tflite-micro.");
}