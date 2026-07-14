cmake_minimum_required(VERSION 3.20)

project(Litharia VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SFML_DIR "${CMAKE_SOURCE_DIR}/libs/SFML/lib/cmake/SFML")

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

# ---------------------------------------------------------------------------
# Simulation core. No window, no graphics: links SFML System only, which is what
# lets the tests run in a second instead of by playing the game.
# ---------------------------------------------------------------------------
add_library(Litharia_core STATIC
    src/Blocks/Blocks.cpp

    src/World/World.cpp
    src/World/TerrainGenerator.cpp
)

target_include_directories(Litharia_core PUBLIC src)

target_link_libraries(Litharia_core PUBLIC
    SFML::System
)

# ---------------------------------------------------------------------------
# The game. Only these files may touch SFML Graphics/Window.
# ---------------------------------------------------------------------------
add_executable(Litharia
    src/main.cpp

    src/Game/Game.cpp
    src/World/Chunks.cpp
    src/Camera/Camera.cpp
)

target_link_libraries(Litharia PRIVATE
    Litharia_core
    SFML::Graphics
    SFML::Window
    SFML::System
)

add_custom_command(TARGET Litharia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/libs/SFML/bin"
    "$<TARGET_FILE_DIR:Litharia>"
)

# ---------------------------------------------------------------------------
# Tests. Doctest, single vendored header, no window.
# ---------------------------------------------------------------------------
add_executable(Litharia_tests
    tests/test_main.cpp
    tests/test_world.cpp
)

target_include_directories(Litharia_tests PRIVATE libs/doctest)

target_link_libraries(Litharia_tests PRIVATE
    Litharia_core
)

enable_testing()
add_test(NAME Litharia_tests COMMAND Litharia_tests)
