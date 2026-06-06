cmake_minimum_required(VERSION 3.16)
project(WeatherDesktop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find system packages ---
find_package(SDL2 REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenGL REQUIRED)

# --- Dear ImGui (bundled source) ---
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

# --- Main executable ---
add_executable(WeatherDesktop
    src/main.cpp
    src/App.cpp
    src/WeatherTypes.cpp
    src/WeatherService.cpp
    src/LocationManager.cpp
    src/AlertEngine.cpp
    src/NotificationManager.cpp
    src/SystemTray.cpp
    src/Config.cpp
    src/UIRenderer.cpp
    src/Theme.cpp
    src/Texture.cpp
    src/SkyEvents.cpp
)

# On Windows, mark as a GUI application (no console window)
if(WIN32)
    set_target_properties(WeatherDesktop PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(WeatherDesktop PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(WeatherDesktop PRIVATE
    imgui
    SDL2::SDL2main
    SDL2::SDL2
    OpenGL::GL
    CURL::libcurl
    nlohmann_json::nlohmann_json
)

# Link Windows-specific system libraries
if(WIN32)
    target_link_libraries(WeatherDesktop PRIVATE
        shell32
        ole32
        shlwapi
        dwmapi
        windowscodecs   # WIC: decode the weather-icons PNG (see Texture.cpp)
    )
endif()

# Ship the runtime assets (font + weather sprite sheet) next to the exe so the
# app can load them by path. Distribution zips the exe + this resources/ dir.
add_custom_command(TARGET WeatherDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:WeatherDesktop>/resources
    COMMENT "Copying resources/ next to the executable")
