#include "Game.h"

#include <algorithm>
#include <string>

#include "../Core/Constants.h"

namespace
{

constexpr unsigned WINDOW_WIDTH = 1280;
constexpr unsigned WINDOW_HEIGHT = 720;

constexpr std::uint32_t WORLD_SEED = 1337;

// Physics runs at exactly this rate no matter what the display does.
constexpr float FIXED_STEP = 1.0f / 60.0f;

// If the game stalls, simulate at most this much time before giving up and
// dropping the rest, rather than spiralling into an ever-growing catch-up.
constexpr float MAX_FRAME_TIME = 0.25f;

PlayerInput readInput()
{
    using Key = sf::Keyboard::Key;

    PlayerInput input;

    input.left = sf::Keyboard::isKeyPressed(Key::A) || sf::Keyboard::isKeyPressed(Key::Left);
    input.right = sf::Keyboard::isKeyPressed(Key::D) || sf::Keyboard::isKeyPressed(Key::Right);
    input.jump = sf::Keyboard::isKeyPressed(Key::Space);

    return input;
}

} // namespace

Game::Game()
    : window(sf::VideoMode({WINDOW_WIDTH, WINDOW_HEIGHT}), "Litharia")
    , generator(WORLD_SEED)
    , chunks(world)
    , camera({static_cast<float>(WINDOW_WIDTH), static_cast<float>(WINDOW_HEIGHT)})
    , player({0.0f, 0.0f})
{
    window.setFramerateLimit(60);

    generator.generate(world);
    chunks.markAllDirty();

    player = Player(findSpawn());
    camera.snapTo(player.center());
}

sf::Vector2f Game::findSpawn() const
{
    const int spawnTileX = WORLD_WIDTH / 2;
    const int surface = generator.surfaceHeight(spawnTileX);

    // Standing on the grass: bottom of the box flush with the top of the surface tile.
    const float x = spawnTileX * TILE_SIZE + (TILE_SIZE - Player::WIDTH) * 0.5f;
    const float y = surface * TILE_SIZE - Player::HEIGHT;

    return {x, y};
}

void Game::run()
{
    sf::Clock clock;
    float accumulator = 0.0f;

    while (window.isOpen())
    {
        const float frameTime = std::min(clock.restart().asSeconds(), MAX_FRAME_TIME);

        handleEvents();

        // Fixed timestep: consume the frame's time in whole 1/60 s steps and carry
        // the remainder into the next frame.
        accumulator += frameTime;

        while (accumulator >= FIXED_STEP)
        {
            fixedUpdate(FIXED_STEP);
            accumulator -= FIXED_STEP;
        }

        render();
    }
}

void Game::handleEvents()
{
    while (const auto event = window.pollEvent())
    {
        if (event->is<sf::Event::Closed>())
        {
            window.close();
        }
        else if (const auto* resized = event->getIf<sf::Event::Resized>())
        {
            camera.setViewSize({static_cast<float>(resized->size.x),
                                static_cast<float>(resized->size.y)});
        }
        else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
        {
            if (key->code == sf::Keyboard::Key::Escape)
                window.close();
        }
    }
}

void Game::fixedUpdate(float dt)
{
    player.update(readInput(), world, dt);

    camera.follow(player.center(), dt);

    window.setTitle("Litharia  -  chunks: " + std::to_string(chunks.lastDrawnChunks()) + "/" +
                    std::to_string(chunks.chunkCount()) +
                    (player.isGrounded() ? "  -  grounded" : "  -  airborne"));
}

void Game::render()
{
    window.clear(sf::Color(122, 184, 240));

    window.setView(camera.view());

    chunks.draw(window, camera.view());

    // The player, until there is a sprite for one.
    sf::RectangleShape body({Player::WIDTH, Player::HEIGHT});
    body.setPosition(player.position());
    body.setFillColor(sf::Color(232, 90, 80));
    body.setOutlineThickness(-2.0f);
    body.setOutlineColor(sf::Color(40, 20, 20));

    window.draw(body);

    window.display();
}
