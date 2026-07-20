#pragma once

// A day/night clock: a single float that loops over one full day, and a
// smoothly-eased 0 (deep night) to 1 (noon) factor derived from it. Owns no
// SFML dependency at all - Game turns daylightFactor() into actual colors.
class DayNightClock
{
public:
    // 10 minutes of day, 5 of night - a full lap is 15 minutes real time.
    static constexpr float CYCLE_SECONDS = 900.0f;
    static constexpr float DAY_FRACTION = 10.0f / 15.0f;
    static constexpr float DAY_SECONDS = CYCLE_SECONDS * DAY_FRACTION; // 600

    // Dawn and dusk each ramp smoothly over this many seconds, rather than
    // snapping straight from full day to full night.
    static constexpr float TRANSITION_SECONDS = 60.0f;

    // Advances the clock by dt, wrapping back to 0 at the end of a lap.
    void tick(float dt);

    // 0 (deep night) to 1 (noon): flat 1 through the bulk of the day, flat 0
    // through the bulk of the night, smoothly eased across dusk and dawn.
    float daylightFactor() const;

private:
    // Starts at midday - a fresh world spawns in full daylight, not
    // mid-transition or at night.
    float time = DAY_SECONDS * 0.5f;
};
