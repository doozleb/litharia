#include "Game.h"

#include <string>

#include "../Core/Constants.h"

namespace
{

constexpr unsigned WINDOW_WIDTH = 1280;
constexpr unsigned WINDOW_HEIGHT = 720;

constexpr std::uint32_t WORLD_SEED = 1337;

// Temporary until step 3, when the camera starts following the player.
constexpr float PAN_SPEED = 900.0f;

sf::Vector2f viewSize(const sf::RenderWindow& window)
{
    return {static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y)};
}

} // namespace

Game::Game()
    : window(sf::VideoMode({WINDOW_WIDTH, WINDOW_HEIGHT}), "Litharia")
    , generator(WORLD_SEED)
    , chunks(world)
    , camera({static_cast<float>(WINDOW_WIDTH), static_cast<float>(WINDOW_HEIGHT)})
{
    window.setFramerateLimit(60);

    generator.generate(world);
    chunks.markAllDirty();

    // Start at the middle of the world, at the surface.
    camera.snapTo({WORLD_WIDTH * TILE_SIZE * 0.5f, 200.0f * TILE_SIZE});
}

void Game::run()
{
    sf::Clock clock;

    while (window.isOpen())
    {
        const float dt = clock.restart().asSeconds();

        handleEvents();
        update(dt);
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

void Game::update(float dt)
{
    // Temporary free camera so the world can be inspected before the player exists.
    sf::Vector2f pan{0.0f, 0.0f};

    using Key = sf::Keyboard::Key;

    if (sf::Keyboard::isKeyPressed(Key::A) || sf::Keyboard::isKeyPressed(Key::Left))
        pan.x -= 1.0f;
    if (sf::Keyboard::isKeyPressed(Key::D) || sf::Keyboard::isKeyPressed(Key::Right))
        pan.x += 1.0f;
    if (sf::Keyboard::isKeyPressed(Key::W) || sf::Keyboard::isKeyPressed(Key::Up))
        pan.y -= 1.0f;
    if (sf::Keyboard::isKeyPressed(Key::S) || sf::Keyboard::isKeyPressed(Key::Down))
        pan.y += 1.0f;

    camera.pan(pan * PAN_SPEED * dt);

    window.setTitle("Litharia  -  chunks drawn: " + std::to_string(chunks.lastDrawnChunks()) +
                    " / " + std::to_string(chunks.chunkCount()));
}

void Game::render()
{
    window.clear(sf::Color(122, 184, 240));

    window.setView(camera.view());
    chunks.draw(window, camera.view());

    window.display();
}
