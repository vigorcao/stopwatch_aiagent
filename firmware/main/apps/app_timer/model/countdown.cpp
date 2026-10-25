/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "countdown.h"
#include <hal/hal.h>

using namespace model;

void Countdown::start(uint32_t durationMs)
{
    _duration   = durationMs;
    _used_time  = 0;
    _start_time = millis();
    _state      = durationMs > 0 ? State_t::Running : State_t::Finished;
}

void Countdown::pause()
{
    if (_state == State_t::Running) {
        _used_time += millis() - _start_time;
        _state = State_t::Paused;
    }
}

void Countdown::resume()
{
    if (_state == State_t::Paused) {
        _start_time = millis();
        _state      = State_t::Running;
    }
}

void Countdown::reset()
{
    _used_time = 0;
    _state     = State_t::Idle;
}

void Countdown::update()
{
    if (_state == State_t::Running && getRemainingTime() == 0) {
        _used_time = _duration;
        _state     = State_t::Finished;
    }
}

uint32_t Countdown::getRemainingTime() const
{
    uint32_t used = _used_time;
    if (_state == State_t::Running) {
        used += millis() - _start_time;
    } else if (_state == State_t::Finished) {
        return 0;
    }
    return used >= _duration ? 0 : _duration - used;
}

float Countdown::getRemainingFraction() const
{
    if (_duration == 0) {
        return 0.0f;
    }
    return static_cast<float>(getRemainingTime()) / static_cast<float>(_duration);
}

uint32_t Countdown::millis() const
{
    return GetHAL().millis();
}
