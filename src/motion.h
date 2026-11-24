// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include "common.h"

inline constexpr int frames_per_tick = 4;
inline constexpr int gameplay_ticks_per_second = 15;
inline constexpr float gameplay_tick_ms = 1000.0f / gameplay_ticks_per_second;

inline vec2i interpolate_motion(vec2i previous, vec2i current, int frame)
{
    const int64_t dx = int64_t(current.x) - previous.x;
    const int64_t dy = int64_t(current.y) - previous.y;
    // Large displacements are teleports. Show them at the destination.
    if (dx < -128 || dx > 128 || dy < -128 || dy > 128) return current;
    const auto advance = [frame](int delta) {
        return (delta * frame + (delta >= 0 ? frames_per_tick / 2 : -frames_per_tick / 2)) / frames_per_tick;
    };
    return previous + vec2i(advance(int(dx)), advance(int(dy)));
}
