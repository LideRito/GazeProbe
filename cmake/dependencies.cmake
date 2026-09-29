include(FetchContent)

FetchContent_Declare(onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip
    URL_HASH SHA256=78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581
)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip
    URL_HASH SHA256=e1c46d676c2bcb7ced847ba27f50553e33a19db97b3cadaec7f8be64449139f8
)
FetchContent_MakeAvailable(onnxruntime imgui)

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll
    IMPORTED_IMPLIB ${onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib
    INTERFACE_INCLUDE_DIRECTORIES ${onnxruntime_SOURCE_DIR}/include
)

# Dear ImGui ships no build script, so its core and the Win32 and DirectX 11 backends are built here.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d11 dxgi)

# The face model is optional: without it the face channel reports itself off and the other four still run.
set(FACE_MODEL ${PROJECT_SOURCE_DIR}/models/anime_face_v14s.onnx)
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

# Windows ships an older onnxruntime.dll in System32, so ours has to sit next to each executable.
function(copy_onnxruntime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:onnxruntime> $<TARGET_FILE_DIR:${target}>)
endfunction()

function(copy_face_model target)
    if(EXISTS ${FACE_MODEL})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/models
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FACE_MODEL} $<TARGET_FILE_DIR:${target}>/models)
    endif()
endfunction()
