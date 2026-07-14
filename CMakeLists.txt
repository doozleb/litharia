cmake_minimum_required(VERSION 3.20)

project(Litharia VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

set(SFML_DIR "${CMAKE_SOURCE_DIR}/libs/SFML/lib/cmake/SFML")

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

add_executable(Litharia
    src/main.cpp

    src/Game/Game.cpp

    src/Tile/Tile.cpp

    src/World/World.cpp
    src/World/TerrainGenerator.cpp
    src/World/Chunks.cpp

    src/Blocks/Blocks.cpp

    src/Player/Player.cpp

    src/Camera/Camera.cpp
)

target_link_libraries(Litharia PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
)

add_custom_command(TARGET Litharia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/libs/SFML/bin"
    "$<TARGET_FILE_DIR:Litharia>"
)