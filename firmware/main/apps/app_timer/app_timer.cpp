/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_timer.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>

using namespace mooncake;

AppTimer::AppTimer()
{
    setAppInfo().name = "Timer";
    setAppInfo().icon = (void*)&icon_timer;
}

// Called when the App is installed
void AppTimer::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppTimer::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    _view = std::make_unique<view::TimerView>(_countdown, _last_duration);
    _view->init(lv_screen_active());
}

void AppTimer::onRunning()
{
    GetHAL().updateButtonStates();
    if (_key_manager && _key_manager->update(false) == input::KeyEvent::GoHome) {
        close();
        return;
    }

    LvglLockGuard lock;

    _view->update(false);
}

void AppTimer::onSleeping()
{
    _countdown.update();
    if (_countdown.getState() == model::Countdown::State_t::Finished) {
        ring_in_background();
    }
}

void AppTimer::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _view.reset();
}

// Same approach as the alarm clock: cover the open app and block until dismissed
void AppTimer::ring_in_background()
{
    mclog::tagInfo(getAppInfo().name, "time is up");

    std::unique_ptr<view::TimerView> overlay;
    {
        LvglLockGuard lock;
        overlay = std::make_unique<view::TimerView>(_countdown, _last_duration);
        overlay->init(lv_screen_active(), true);
    }

    while (true) {
        GetHAL().feedTheDog();
        GetHAL().delay(20);

        LvglLockGuard lock;
        overlay->update();
        if (overlay->isDismissed()) {
            overlay.reset();
            break;
        }
    }
}
