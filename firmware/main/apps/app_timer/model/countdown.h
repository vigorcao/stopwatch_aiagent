/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>

namespace model {

class Countdown {
public:
    enum class State_t { Idle, Running, Paused, Finished };

    void start(uint32_t durationMs);
    void pause();
    void resume();
    void reset();

    // Moves Running to Finished once the time is used up
    void update();

    uint32_t getRemainingTime() const;
    uint32_t getDuration() const
    {
        return _duration;
    }
    // 1.0 at start, 0.0 when finished
    float getRemainingFraction() const;
    State_t getState() const
    {
        return _state;
    }

private:
    uint32_t _duration   = 0;
    uint32_t _start_time = 0;
    uint32_t _used_time  = 0;  // time used before the last resume
    State_t _state       = State_t::Idle;

    uint32_t millis() const;
};

}  // namespace model
