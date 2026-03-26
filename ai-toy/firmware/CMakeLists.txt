cmake_minimum_required(VERSION 3.16)

# 设置项目名称
set(PROJECT_NAME "ai-toy")

# 包含 ESP-IDF CMake 配置
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(${PROJECT_NAME})