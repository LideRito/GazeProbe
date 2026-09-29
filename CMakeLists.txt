cmake_minimum_required(VERSION 3.16)
project(GazeProbe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/dependencies.cmake)

add_compile_options(/W4 /permissive- /utf-8)

add_library(gaze STATIC
    src/oklab.cpp
    src/filters.cpp
    src/saliency.cpp
    src/image_io.cpp
    src/faces.cpp
    src/pixel_utils.cpp
    src/app_utils.cpp
    src/overlay_utils.cpp
)
target_include_directories(gaze PUBLIC src)
target_link_libraries(gaze PUBLIC windowscodecs ole32 PRIVATE onnxruntime)

add_executable(gazeprobe src/main.cpp)
target_link_libraries(gazeprobe PRIVATE gaze)
copy_onnxruntime(gazeprobe)
copy_face_model(gazeprobe)

add_executable(gazeprobe_viewer WIN32 src/viewer_main.cpp src/viewer.cpp src/renderer.cpp src/labels_ja.cpp)
target_link_libraries(gazeprobe_viewer PRIVATE gaze imgui)
copy_onnxruntime(gazeprobe_viewer)
copy_face_model(gazeprobe_viewer)

enable_testing()

foreach(name pixel_utils oklab filters saliency faces image_io)
    add_executable(test_${name} tests/test_${name}.cpp tests/check.cpp)
    target_link_libraries(test_${name} PRIVATE gaze)
    target_include_directories(test_${name} PRIVATE tests)
    copy_onnxruntime(test_${name})
    add_test(NAME ${name} COMMAND test_${name})
endforeach()
