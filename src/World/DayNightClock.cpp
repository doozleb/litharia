#include "DayNightClock.h"

#include <algorithm>

namespace
{

float smoothstep(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void DayNightClock::tick(float dt)
{
    time += dt;

    if (time >= CYCLE_SECONDS)
        time -= CYCLE_SECONDS;
}

float DayNightClock::daylightFactor() const
{
    constexpr float DUSK_START = DAY_SECONDS - TRANSITION_SECONDS;   // 540
    constexpr float DAWN_START = CYCLE_SECONDS - TRANSITION_SECONDS; // 840

    if (time < DUSK_START)
        return 1.0f;

    if (time < DAY_SECONDS)
        return 1.0f - smoothstep((time - DUSK_START) / TRANSITION_SECONDS);

    if (time < DAWN_START)
        return 0.0f;

    return smoothstep((time - DAWN_START) / TRANSITION_SECONDS);
}
