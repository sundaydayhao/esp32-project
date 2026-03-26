cmake_minimum_required(VERSION 3.16)

# Download minimp3 single-header library from GitHub if not present
set(MINIMP3_H "${CMAKE_CURRENT_SOURCE_DIR}/minimp3.h")

if(NOT EXISTS "${MINIMP3_H}")
    message(STATUS "Downloading minimp3.h from GitHub...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h"
        "${MINIMP3_H}"
        SHOW_PROGRESS
        STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR
            "Failed to download minimp3.h (status ${STATUS_CODE}).\n"
            "Please download it manually:\n"
            "  curl -L https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h "
            "-o ${MINIMP3_H}"
        )
    endif()
    message(STATUS "minimp3.h downloaded successfully")
endif()

idf_component_register(
    SRCS
        "minimp3_impl.c"
    INCLUDE_DIRS
        "."
)