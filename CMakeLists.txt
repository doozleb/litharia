cmake_minimum_required(VERSION 3.20)

project(Litharia VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSBuild launches multiple CL.EXE processes against one .pdb when a broadly-
# included header (e.g. MachineType.h) changes and forces many translation
# units to recompile at once; without /FS that races and fails with C1041.
if(MSVC)
    add_compile_options(/FS)
endif()

set(SFML_DIR "${CMAKE_SOURCE_DIR}/libs/SFML/lib/cmake/SFML")

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

# ---------------------------------------------------------------------------
# Simulation core. No window, no graphics: links SFML System only, which is what
# lets the tests run in a second instead of by playing the game.
# ---------------------------------------------------------------------------
add_library(Litharia_core STATIC
    src/Blocks/Blocks.cpp

    src/Core/Noise.cpp
    src/Core/Direction.cpp

    src/World/World.cpp
    src/World/TerrainGenerator.cpp

    src/Physics/Physics.cpp
    src/Player/Player.cpp

    src/Items/Items.cpp
    src/Items/ItemEntity.cpp
    src/Items/Inventory.cpp

    src/Machines/Recipes.cpp
    src/Machines/MachineRegistry.cpp
    src/Machines/Machines.cpp
    src/Machines/MachineStatus.cpp

    src/Hud/HudLayout.cpp
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
    src/Hud/Hud.cpp
    src/Machines/MachineRenderer.cpp
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
    tests/test_machines.cpp
    tests/test_world.cpp
    tests/test_terrain.cpp
    tests/test_physics.cpp
    tests/test_player.cpp
    tests/test_spawn.cpp
    tests/test_mining.cpp
    tests/test_inventory.cpp
    tests/test_pickup.cpp
    tests/test_placing.cpp
    tests/test_recipes.cpp
    tests/test_power.cpp
    tests/test_transport.cpp
    tests/test_processing.cpp
    tests/test_factory.cpp
    tests/test_machine_status.cpp
    tests/test_machine_inspect.cpp
    tests/test_extraction.cpp
    tests/test_hud_layout.cpp
)

target_include_directories(Litharia_tests PRIVATE libs/doctest)

target_link_libraries(Litharia_tests PRIVATE
    Litharia_core
)

enable_testing()
add_test(NAME Litharia_tests COMMAND Litharia_tests)
