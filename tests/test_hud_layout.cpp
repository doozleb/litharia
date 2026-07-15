#include "doctest.h"

#include "Hud/HudLayout.h"

TEST_CASE("tooltipTopLeft centers the panel above the anchor with a margin")
{
    const sf::Vector2f anchor{100.0f, 200.0f};
    const sf::Vector2f panelSize{60.0f, 40.0f};

    const sf::Vector2f topLeft = hudLayout::tooltipTopLeft(anchor, panelSize, 10.0f);

    // Horizontally centered on the anchor...
    CHECK(topLeft.x == doctest::Approx(70.0f));
    // ...and its bottom edge sits `margin` above the anchor's y.
    CHECK(topLeft.y == doctest::Approx(150.0f));
    CHECK(topLeft.y + panelSize.y + 10.0f == doctest::Approx(anchor.y));
}

TEST_CASE("gridSlotPosition lays slots out row-major from the origin")
{
    const sf::Vector2f origin{10.0f, 20.0f};

    sf::Vector2f p = hudLayout::gridSlotPosition(origin, 0, 10, 48.0f, 4.0f);
    CHECK(p.x == doctest::Approx(10.0f));
    CHECK(p.y == doctest::Approx(20.0f));

    p = hudLayout::gridSlotPosition(origin, 1, 10, 48.0f, 4.0f);
    CHECK(p.x == doctest::Approx(62.0f));
    CHECK(p.y == doctest::Approx(20.0f));

    p = hudLayout::gridSlotPosition(origin, 10, 10, 48.0f, 4.0f);
    CHECK(p.x == doctest::Approx(10.0f));
    CHECK(p.y == doctest::Approx(72.0f));

    p = hudLayout::gridSlotPosition(origin, 12, 10, 48.0f, 4.0f);
    CHECK(p.x == doctest::Approx(114.0f));
    CHECK(p.y == doctest::Approx(72.0f));
}

TEST_CASE("hitTestGrid finds the slot under a point")
{
    const sf::Vector2f origin{0.0f, 0.0f};

    // Comfortably inside slot 0.
    CHECK(hudLayout::hitTestGrid({10.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == 0);

    // Comfortably inside slot 1 (second column, first row).
    CHECK(hudLayout::hitTestGrid({60.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == 1);

    // Second row, first column.
    CHECK(hudLayout::hitTestGrid({10.0f, 60.0f}, origin, 10, 3, 48.0f, 4.0f) == 10);
}

TEST_CASE("hitTestGrid rejects points outside every slot")
{
    const sf::Vector2f origin{0.0f, 0.0f};

    // Left/above the grid entirely.
    CHECK(hudLayout::hitTestGrid({-5.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);
    CHECK(hudLayout::hitTestGrid({10.0f, -5.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);

    // In the gap between two slots (slot is 48px, gap is 4px: x=49 is in the gap).
    CHECK(hudLayout::hitTestGrid({49.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);

    // Past the last column/row.
    CHECK(hudLayout::hitTestGrid({10.0f, 500.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);
    CHECK(hudLayout::hitTestGrid({5000.0f, 10.0f}, origin, 10, 3, 48.0f, 4.0f) == -1);
}
