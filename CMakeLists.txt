cmake_minimum_required(VERSION 3.20)
project(PowerTools VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: no vcruntime dependency, smaller working set.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_impl_win32.cpp
  ${IMGUI_DIR}/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS)
target_compile_options(imgui PRIVATE /utf-8 /O2 /GL- /W0)

add_executable(PowerTools WIN32
  src/entry.cpp
  src/util.cpp
  src/config.cpp
  src/tasks.cpp
  src/tweaks.cpp
  src/appx.cpp
  src/engine.cpp
  src/ui.cpp
  src/widgets.cpp
  src/pages.cpp
  resources/app.rc)

target_include_directories(PowerTools PRIVATE src)
target_compile_definitions(PowerTools PRIVATE
  UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS
  PT_VERSION="${PROJECT_VERSION}")
target_compile_options(PowerTools PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
target_compile_options(PowerTools PRIVATE $<$<CONFIG:Release>:/O1 /Gy /GL>)
target_link_options(PowerTools PRIVATE /MANIFEST:NO
  $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
target_link_libraries(PowerTools PRIVATE imgui
  d3d11 dxgi dwmapi shell32 advapi32 user32 gdi32 ole32 powrprof psapi comdlg32 shlwapi shcore)
