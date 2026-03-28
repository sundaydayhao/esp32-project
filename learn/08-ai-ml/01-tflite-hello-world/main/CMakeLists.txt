idf_component_register(
    SRCS "main.cc" "sine_model_data.cc"
    INCLUDE_DIRS "."
    REQUIRES esp_timer
)

# TFLite Micro 需要 C++17
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c++17)