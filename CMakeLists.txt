cmake_minimum_required(VERSION 3.16)
project(GazeProbe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(/W4 /permissive- /utf-8)

include(FetchContent)
FetchContent_Declare(onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip
    URL_HASH SHA256=78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581
)
FetchContent_MakeAvailable(onnxruntime)

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll
    IMPORTED_IMPLIB ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib
    INTERFACE_INCLUDE_DIRECTORIES ${onnxruntime_SOURCE_DIR}/include
)

# The face model is optional: without it the face channel reports itself off and the other four still run.
set(FACE_MODEL ${CMAKE_SOURCE_DIR}/models/anime_face_v14s.onnx)
set(FACE_MODEL_SHA256 403b5bc93b6ff789b7d183418df4a1364049bac00c24acd927604a7ff6891483)
if(NOT EXISTS ${FACE_MODEL})
    message(STATUS "Downloading the face model (45 MB)")
    file(DOWNLOAD https://huggingface.co/deepghs/anime_face_detection/resolve/main/face_detect_v1.4_s/model.onnx
         ${FACE_MODEL}.part STATUS status)
    list(GET status 0 code)
    set(hash "")
    if(code EQUAL 0)
        file(SHA256 ${FACE_MODEL}.part hash)
    endif()
    if("${hash}" STREQUAL "${FACE_MODEL_SHA256}")
        file(RENAME ${FACE_MODEL}.part ${FACE_MODEL})
    else()
        file(REMOVE ${FACE_MODEL}.part)
        message(WARNING "Could not download the face model; the face channel will stay off")
    endif()
endif()

add_library(gaze STATIC
    src/oklab.cpp
    src/filters.cpp
    src/saliency.cpp
    src/image_io.cpp
    src/faces.cpp
    src/pixel_utils.cpp
)
target_include_directories(gaze PUBLIC src)
target_link_libraries(gaze PUBLIC windowscodecs ole32 PRIVATE onnxruntime)

# Windows ships an older onnxruntime.dll in System32, so ours has to sit next to each executable.
function(copy_onnxruntime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:onnxruntime> $<TARGET_FILE_DIR:${target}>)
endfunction()

add_executable(gazeprobe src/main.cpp)
target_link_libraries(gazeprobe PRIVATE gaze)
copy_onnxruntime(gazeprobe)
if(EXISTS ${FACE_MODEL})
    add_custom_command(TARGET gazeprobe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:gazeprobe>/models
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FACE_MODEL} $<TARGET_FILE_DIR:gazeprobe>/models)
endif()

enable_testing()

foreach(name pixel_utils oklab filters saliency faces image_io)
    add_executable(test_${name} tests/test_${name}.cpp tests/check.cpp)
    target_link_libraries(test_${name} PRIVATE gaze)
    target_include_directories(test_${name} PRIVATE tests)
    copy_onnxruntime(test_${name})
    add_test(NAME ${name} COMMAND test_${name})
endforeach()
