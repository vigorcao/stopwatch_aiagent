/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "model/countdown.h"
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>

/**
 * @brief Countdown timer. It keeps counting after you leave the app,
 * and rings over whatever app is open when the time is up.
 */
class AppTimer : public mooncake::AppAbility {
public:
    AppTimer();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onSleeping() override;
    void onClose() override;

private:
    model::Countdown _countdown;
    uint32_t _last_duration = 5 * 60 * 1000;
    std::unique_ptr<view::TimerView> _view;
    std::unique_ptr<input::KeyManager> _key_manager;

    void ring_in_background();
};
