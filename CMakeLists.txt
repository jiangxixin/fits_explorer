cmake_minimum_required(VERSION 3.16)
project(fits_explorer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dependencies ----
find_package(Threads REQUIRED)
find_package(cfitsio REQUIRED)

# ---- ImGui (embedded) ----
include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.7
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    imgui_club_shaders
    GIT_REPOSITORY https://github.com/ocornut/imgui_club_shaders.git
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(imgui imgui_club_shaders)

# ---- ImGui File Dialog ----
FetchContent_Declare(
    imfilebrowser
    GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
    GIT_TAG v0.6.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imfilebrowser)

# ---- SQLite3 ----
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 sqlite3)
include_directories(${SQLITE3_INCLUDE_DIRS})

# ---- stb_image (thumbnail generation) ----
# Header-only, included directly

# ---- Application ----
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/database/database.cpp
    src/indexer/indexer.cpp
    src/thumbnail/thumbnail.cpp
    src/ui/ui.cpp
)

add_executable(fits_explorer ${SOURCES})

target_link_libraries(fits_explorer PRIVATE
    Threads::Threads
    CFITSIO::cfitsio
    ${SQLITE3_LIBRARIES}
    imgui
    ImGuiFileDialog
)

target_include_directories(fits_explorer PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${SQLITE3_INCLUDE_DIRS}
)

# Platform-specific
if(APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(fits_explorer PRIVATE X11::X11)
endif()

# ---- Warnings ----
if(MSVC)
    target_compile_options(fits_explorer PRIVATE /W4)
else()
    target_compile_options(fits_explorer PRIVATE -Wall -Wextra)
endif()

# ---- Install ----
install(TARGETS fits_explorer DESTINATION bin)
