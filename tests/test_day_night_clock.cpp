#include "doctest.h"

#include "World/DayNightClock.h"

// The clock starts at time = DAY_SECONDS * 0.5 = 300s (midday), not at 0 -
// every tick amount below is chosen relative to that known start so the
// resulting absolute `time` values line up with the segment boundaries
// (540 = dusk start, 600 = full night, 840 = dawn start, 900 = wraps to 0).

TEST_CASE("a fresh clock starts at full daylight (midday)")
{
    DayNightClock clock;
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));
}

TEST_CASE("daylightFactor stays flat at 1.0 through the bulk of the day")
{
    DayNightClock clock;
    clock.tick(100.0f); // time = 400, still well inside the flat-day segment
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));
}

TEST_CASE("daylightFactor eases from 1 to 0 across the 60s dusk transition")
{
    DayNightClock clock;

    clock.tick(240.0f); // time = 540: dusk starts, still 1.0 at the boundary
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));

    clock.tick(30.0f); // time = 570: halfway through dusk
    CHECK(clock.daylightFactor() == doctest::Approx(0.5f));

    clock.tick(30.0f); // time = 600: fully night
    CHECK(clock.daylightFactor() == doctest::Approx(0.0f));
}

TEST_CASE("daylightFactor stays flat at 0.0 through the bulk of the night")
{
    DayNightClock clock;
    clock.tick(400.0f); // time = 700, inside the flat-night segment [600, 840)
    CHECK(clock.daylightFactor() == doctest::Approx(0.0f));
}

TEST_CASE("daylightFactor eases from 0 to 1 across the 60s dawn transition")
{
    DayNightClock clock;

    clock.tick(540.0f); // time = 840: dawn starts, still 0.0 at the boundary
    CHECK(clock.daylightFactor() == doctest::Approx(0.0f));

    clock.tick(30.0f); // time = 870: halfway through dawn
    CHECK(clock.daylightFactor() == doctest::Approx(0.5f));

    clock.tick(30.0f); // time wraps to 0: back to full daylight
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));
}

TEST_CASE("tick wraps around at the end of a 900-second lap")
{
    DayNightClock clock;
    clock.tick(900.0f); // exactly one full lap back to the midday start
    CHECK(clock.daylightFactor() == doctest::Approx(1.0f));
}
