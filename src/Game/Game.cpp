#include "Game.h"

#include <algorithm>
#include <string>

#include "../Core/Constants.h"
#include "../Core/Noise.h"

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

sf::Color toColor(BlockColor c)
{
    return sf::Color(c.r, c.g, c.b);
}

// The block an item would place, which is also what it looks like on the ground.
sf::Color itemColor(ItemType type)
{
    return toColor(blockInfo(itemInfo(type).placeBlock).color);
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

sf::Vector2f Game::cursorWorldPosition() const
{
    // The mouse is in pixels; the world is in world units under the camera's view.
    return window.mapPixelToCoords(sf::Mouse::getPosition(window), camera.view());
}

PlayerInput Game::readInput() const
{
    using Key = sf::Keyboard::Key;

    PlayerInput input;

    input.left = sf::Keyboard::isKeyPressed(Key::A) || sf::Keyboard::isKeyPressed(Key::Left);
    input.right = sf::Keyboard::isKeyPressed(Key::D) || sf::Keyboard::isKeyPressed(Key::Right);
    input.jump = sf::Keyboard::isKeyPressed(Key::Space);

    input.mine = window.hasFocus() && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    input.cursor = cursorWorldPosition();

    return input;
}

void Game::spawnDrop(const MineResult& result)
{
    const ItemType type = itemForBlock(result.block);

    if (type == ItemType::None)
        return;

    // Pop out of the ground with a small hashed kick, so a row of drops does not
    // land in a perfectly straight line.
    const float roll = noise::hashFloat(result.tileX, result.tileY, WORLD_SEED);

    const sf::Vector2f velocity{(roll - 0.5f) * 90.0f, -140.0f};

    // Centred in the tile it came from.
    const sf::Vector2f position{result.tileX * TILE_SIZE + (TILE_SIZE - ItemEntity::SIZE) * 0.5f,
                                result.tileY * TILE_SIZE + (TILE_SIZE - ItemEntity::SIZE) * 0.5f};

    drops.emplace_back(ItemStack{type, 1}, position, velocity);
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
    const MineResult result = player.update(readInput(), world, dt);

    if (result.broke)
    {
        // The player set the tile to air; the renderer has to be told about it.
        chunks.markDirty(result.tileX, result.tileY);

        spawnDrop(result);
    }

    for (ItemEntity& drop : drops)
        drop.update(world, dt);

    camera.follow(player.center(), dt);

    window.setTitle("Litharia  -  drops: " + std::to_string(drops.size()));
}

void Game::drawMiningHighlight()
{
    if (!player.isMining())
        return;

    const sf::Vector2i target = player.miningTarget();

    const sf::Vector2f corner{static_cast<float>(target.x * TILE_SIZE),
                              static_cast<float>(target.y * TILE_SIZE)};

    // The tile being worked on.
    sf::RectangleShape outline({TILE_SIZE, TILE_SIZE});
    outline.setPosition(corner);
    outline.setFillColor(sf::Color::Transparent);
    outline.setOutlineThickness(-2.0f);
    outline.setOutlineColor(sf::Color(255, 255, 255, 200));

    window.draw(outline);

    // How far through breaking it we are: the tile whitens as it cracks.
    const float progress = player.miningProgress();

    sf::RectangleShape crack({TILE_SIZE, TILE_SIZE});
    crack.setPosition(corner);
    crack.setFillColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(progress * 140.0f)));

    window.draw(crack);
}

void Game::render()
{
    window.clear(sf::Color(122, 184, 240));

    window.setView(camera.view());

    chunks.draw(window, camera.view());

    drawMiningHighlight();

    // Dropped stacks.
    sf::RectangleShape item({ItemEntity::SIZE, ItemEntity::SIZE});
    item.setOutlineThickness(-1.0f);
    item.setOutlineColor(sf::Color(30, 25, 20));

    for (const ItemEntity& drop : drops)
    {
        item.setPosition(drop.position());
        item.setFillColor(itemColor(drop.stack().type));

        window.draw(item);
    }

    // The player, until there is a sprite for one.
    sf::RectangleShape body({Player::WIDTH, Player::HEIGHT});
    body.setPosition(player.position());
    body.setFillColor(sf::Color(232, 90, 80));
    body.setOutlineThickness(-2.0f);
    body.setOutlineColor(sf::Color(40, 20, 20));

    window.draw(body);

    window.display();
}
