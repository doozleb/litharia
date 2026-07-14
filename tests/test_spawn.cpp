#include "doctest.h"

#include "Blocks/Blocks.h"
#include "Core/Constants.h"
#include "Player/Player.h"
#include "World/TerrainGenerator.h"
#include "World/World.h"

// Reproduces exactly what Game does at startup: generate the real world, put the
// player at the spawn point, and tick. The player must end up standing on the
// ground, not floating above it.
TEST_CASE("the player spawns standing on the generated surface")
{
    World world;
    const TerrainGenerator generator(1337);
    generator.generate(world);

    const int spawnTileX = WORLD_WIDTH / 2;
    const int surface = generator.surfaceHeight(spawnTileX);

    const float x = spawnTileX * TILE_SIZE + (TILE_SIZE - Player::WIDTH) * 0.5f;
    const float y = surface * TILE_SIZE - Player::HEIGHT;

    Player player({x, y});

    INFO("surface tile row = ", surface);
    INFO("spawn box top    = ", y);
    INFO("spawn box bottom = ", y + Player::HEIGHT);
    INFO("surface tile top = ", surface * TILE_SIZE);

    // The tile the player is standing on really is solid.
    REQUIRE(world.isSolid(spawnTileX, surface));

    for (int i = 0; i < 120; ++i)
        player.update({}, world, 1.0f / 60.0f);

    INFO("after settling: y = ", player.position().y, " grounded = ", player.isGrounded());

    CHECK(player.isGrounded());
    CHECK(player.box().bottom() == doctest::Approx(surface * TILE_SIZE).epsilon(0.01));
}
