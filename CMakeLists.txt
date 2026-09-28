cmake_minimum_required(VERSION 3.16)
project(GazeProbe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(/W4 /permissive- /utf-8)

add_library(gaze STATIC
    src/oklab.cpp
    src/filters.cpp
    src/saliency.cpp
    src/image_io.cpp
)
target_include_directories(gaze PUBLIC src)
target_link_libraries(gaze PUBLIC windowscodecs ole32)

add_executable(gazeprobe src/main.cpp)
target_link_libraries(gazeprobe PRIVATE gaze)

enable_testing()

foreach(name oklab filters saliency image_io)
    add_executable(test_${name} tests/test_${name}.cpp tests/check.cpp)
    target_link_libraries(test_${name} PRIVATE gaze)
    target_include_directories(test_${name} PRIVATE tests)
    add_test(NAME ${name} COMMAND test_${name})
endforeach()
