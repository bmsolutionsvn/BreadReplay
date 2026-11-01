cmake_minimum_required(VERSION 3.28)
project(BreadReplay LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/W4 /permissive- /utf-8 /EHsc)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)

# BreadReplay builds BreadCamera's libraries from its checkout instead of copying them (spec §3).
# BC_LIBS_ONLY stops BreadCamera's CMakeLists after bc_io, so none of its engine, tests or exes
# are built here.
set(BR_BREADCAMERA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../BreadCamera" CACHE PATH "BreadCamera checkout")
set(BC_LIBS_ONLY ON CACHE BOOL "" FORCE)
add_subdirectory("${BR_BREADCAMERA_DIR}" breadcamera-build)

find_package(doctest CONFIG REQUIRED)
# BreadCamera calls these inside its own directory, so their imported targets are not visible up
# here; ask again for the ones BreadReplay's targets link.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(unofficial-webview2 CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)

# BreadReplay's own shaders, built with the same fxc rule as BreadCamera's bc_shader.
find_program(FXC fxc REQUIRED)
set(BR_SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${BR_SHADER_DIR})
function(br_shader name profile)
  set(src ${CMAKE_CURRENT_SOURCE_DIR}/src/play/shaders/${name}.hlsl)
  set(out ${BR_SHADER_DIR}/${name}.h)
  add_custom_command(OUTPUT ${out}
    COMMAND ${FXC} /nologo /O3 /T ${profile} /E main /Vn g_${name} /Fh ${out} ${src}
    DEPENDS ${src} VERBATIM)
  set(BR_SHADER_HEADERS ${BR_SHADER_HEADERS} ${out} PARENT_SCOPE)
endfunction()
br_shader(mix_linear cs_5_0)

add_library(br_core STATIC
  src/core/vmx_codec.cpp
  src/core/timecode.cpp
  src/store/segment.cpp
  src/store/input_store.cpp
  src/store/retention.cpp
  src/record/recorder.cpp
  src/events/event_store.cpp
  src/events/show_file.cpp
  src/play/player.cpp
  src/play/frame_source.cpp
  src/play/renderer.cpp
  src/play/audio_follow.cpp
  src/play/output_player.cpp
  ${BR_SHADER_HEADERS}
)
target_include_directories(br_core PUBLIC src ${BR_SHADER_DIR})
target_link_libraries(br_core PUBLIC bc_core bc_gpu bc_io omthx libvmx nlohmann_json::nlohmann_json)

# The engine plus the BreadCamera pieces it uses by path (control server, preview encoder, hub,
# adapter factory and the chart-availability probe the test skips call).
add_library(br_engine STATIC
  src/engine/engine.cpp
  src/engine/command_router.cpp
  "${BR_BREADCAMERA_DIR}/src/engine/adapter_factory.cpp"
  "${BR_BREADCAMERA_DIR}/src/engine/preview_encoder.cpp"
  "${BR_BREADCAMERA_DIR}/src/engine/preview_hub.cpp"
  "${BR_BREADCAMERA_DIR}/src/engine/control_server.cpp"
  "${BR_BREADCAMERA_DIR}/src/engine/chart_detector.cpp"
)
target_include_directories(br_engine PUBLIC src)
target_link_libraries(br_engine PUBLIC br_core ixwebsocket::ixwebsocket bcrypt)
target_compile_definitions(br_engine PUBLIC BC_WITH_CHART_DETECT=0)

# The tests use BreadCamera's requirement skips and test media.
add_executable(br_tests
  tests/main.cpp
  "${BR_BREADCAMERA_DIR}/tests/support/requires.cpp"
  "${BR_BREADCAMERA_DIR}/tests/support/test_media.cpp"
  tests/vmx_codec_test.cpp
  tests/timecode_test.cpp
  tests/segment_test.cpp
  tests/input_store_test.cpp
  tests/retention_test.cpp
  tests/recorder_test.cpp
  tests/event_store_test.cpp
  tests/show_file_test.cpp
  tests/player_test.cpp
  tests/frame_source_test.cpp
  tests/renderer_test.cpp
  tests/audio_follow_test.cpp
  tests/output_player_test.cpp
  tests/router_test.cpp
  tests/engine_test.cpp
)
target_include_directories(br_tests PRIVATE "${BR_BREADCAMERA_DIR}/tests")
target_link_libraries(br_tests PRIVATE br_engine doctest::doctest)
target_compile_definitions(br_tests PRIVATE BC_WITH_MAXINE=0 BC_WITH_ONNX=0)
bc_copy_runtime(br_tests)
omthx_copy_runtime(br_tests)

# ---- engine executable ----
add_executable(breadreplay-engine src/engine/engine_main.cpp)
target_link_libraries(breadreplay-engine PRIVATE br_engine)
bc_copy_runtime(breadreplay-engine)
omthx_copy_runtime(breadreplay-engine)

# ---- shell ----
add_executable(BreadReplay WIN32
  src/shell/shell_main.cpp
  "${BR_BREADCAMERA_DIR}/src/shell/supervisor.cpp"
  "${BR_BREADCAMERA_DIR}/src/shell/process_launcher.cpp"
)
target_include_directories(BreadReplay PRIVATE "${BR_BREADCAMERA_DIR}/src")
target_link_libraries(BreadReplay PRIVATE br_engine bc_core ixwebsocket::ixwebsocket
  unofficial::webview2::webview2 WIL::WIL shell32 ole32 user32 dwmapi comctl32)
bc_copy_runtime(BreadReplay)
omthx_copy_runtime(BreadReplay)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui/dist/index.html")
  add_custom_command(TARGET BreadReplay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/ui/dist -DDST=$<TARGET_FILE_DIR:BreadReplay>/ui
            -P ${BR_BREADCAMERA_DIR}/cmake/CopyUi.cmake)
endif()

# ---- soak tool ----
add_executable(br_soak tools/soak/soak_main.cpp)
target_link_libraries(br_soak PRIVATE br_core)
bc_copy_runtime(br_soak)
omthx_copy_runtime(br_soak)