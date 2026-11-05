cmake_minimum_required(VERSION 3.24)

project(reach8 VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Reach8 solo se compila en Windows con MSVC (Visual Studio 2022, x64).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Reach8 debe compilarse en 64 bits (x64). MCC es un proceso de 64 bits.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime estático: el DLL no depende del Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

# --- MinHook (hooks de funciones) -------------------------------------------
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(minhook)

# --- Dear ImGui (menú en pantalla) ------------------------------------------
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

add_library(r8_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(r8_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# El mando lo entrega Reach8 (SDL), no el backend Win32 de ImGui.
target_compile_definitions(r8_imgui PUBLIC IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)

# --- SDL3 (lectura de hasta 16 mandos: XInput, Windows.Gaming.Input, HID) ---
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.8
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(SDL3)

# --- Reach8 -----------------------------------------------------------------
add_library(reach8 SHARED
    src/main.cpp
    src/proxy/wtsapi32_exports.cpp
    src/core/log.cpp
    src/core/paths.cpp
    src/core/config.cpp
    src/core/memory.cpp
    src/core/hooks.cpp
    src/core/crash.cpp
    src/mcc/mcc.cpp
    src/reach/reach_patches.cpp
    src/players/players.cpp
    src/input/input.cpp
    src/instance/instance.cpp
    src/netprobe/netprobe.cpp
    src/ui/overlay.cpp
    src/version.rc)

target_include_directories(reach8 PRIVATE src)
target_compile_definitions(reach8 PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
    R8_VERSION="${PROJECT_VERSION}")
target_compile_options(reach8 PRIVATE /utf-8 /W4 /permissive- /EHsc)
target_link_libraries(reach8 PRIVATE
    minhook r8_imgui SDL3::SDL3-static
    d3d11 dxgi dbghelp version shell32 ole32 xinput)

# MCC carga WTSAPI32.dll desde su carpeta: el DLL de Reach8 se llama así.
set_target_properties(reach8 PROPERTIES OUTPUT_NAME "WTSAPI32")
