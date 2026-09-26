cmake_minimum_required(VERSION 3.21)
project(Mobilador VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(MOB_SCRCPY_SERVER_VERSION "3.1" CACHE STRING "Versão do scrcpy-server (deve casar com o arquivo empacotado)")

find_package(SDL2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
if(NOT FFMPEG_FOUND)
  find_package(FFMPEG QUIET)  # vcpkg: FindFFMPEG
endif()
if(NOT FFMPEG_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavutil)
endif()

set(SOURCES
  src/main.cpp
  src/core/Log.cpp src/core/Config.cpp src/core/Platform.cpp
  src/net/Socket.cpp
  src/device/Adb.cpp
  src/stream/Session.cpp
  src/video/Decoder.cpp
  src/input/Keymap.cpp src/input/InputMapper.cpp
  src/stats/Metrics.cpp
  src/bench/LatencyTester.cpp src/bench/Benchmark.cpp
  src/ui/Theme.cpp src/ui/App.cpp src/ui/Views.cpp src/ui/Editor.cpp)

if(WIN32)
  list(APPEND SOURCES resources/mobilador.rc)
  add_executable(Mobilador WIN32 ${SOURCES})
else()
  add_executable(Mobilador ${SOURCES})
endif()

target_compile_definitions(Mobilador PRIVATE
  MOB_VERSION="${PROJECT_VERSION}"
  MOB_SCRCPY_SERVER_VERSION="${MOB_SCRCPY_SERVER_VERSION}")

target_link_libraries(Mobilador PRIVATE imgui::imgui nlohmann_json::nlohmann_json
  $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
  $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)

if(FFMPEG_FOUND)
  target_include_directories(Mobilador PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(Mobilador PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(Mobilador PRIVATE ${FFMPEG_LIBRARIES})
else()
  target_link_libraries(Mobilador PRIVATE PkgConfig::LIBAV)
endif()

if(MSVC)
  target_compile_options(Mobilador PRIVATE /W3 /utf-8 /permissive- /MP)
  target_link_options(Mobilador PRIVATE /MANIFEST:NO)  # manifesto próprio (DPI PerMonitorV2) no .rc
  target_link_libraries(Mobilador PRIVATE ws2_32 winmm avrt pdh psapi shell32 ole32)
elseif(WIN32)
  target_link_libraries(Mobilador PRIVATE ws2_32 winmm avrt pdh psapi shell32 ole32)
else()
  target_compile_options(Mobilador PRIVATE -Wall -Wextra -Wno-unused-parameter)
  find_package(Threads REQUIRED)
  target_link_libraries(Mobilador PRIVATE Threads::Threads)
endif()

# Empacotamento: adb (platform-tools) e scrcpy-server ao lado do executável
set(MOB_THIRD_PARTY "${CMAKE_SOURCE_DIR}/third_party")
add_custom_command(TARGET Mobilador POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DSRC=${MOB_THIRD_PARTY} -DDST=$<TARGET_FILE_DIR:Mobilador>
          -P ${CMAKE_SOURCE_DIR}/cmake/CopyThirdParty.cmake)

install(TARGETS Mobilador RUNTIME DESTINATION .)
install(DIRECTORY ${MOB_THIRD_PARTY}/platform-tools DESTINATION . OPTIONAL)
install(FILES ${MOB_THIRD_PARTY}/scrcpy-server DESTINATION . OPTIONAL)
