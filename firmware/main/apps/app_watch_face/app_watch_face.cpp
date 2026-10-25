/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_watch_face.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>

using namespace mooncake;

AppWatchFace::AppWatchFace()
{
    setAppInfo().name = "WatchFace";
    setAppInfo().icon = (void*)&icon_watch_face;
}

// Called when the App is installed
void AppWatchFace::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppWatchFace::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    LvglLockGuard lock;

    _watch_face_manager = std::make_unique<view::WatchFaceManager>();
    _watch_face_manager->init();
}

// Called repeatedly while the App is running
void AppWatchFace::onRunning()
{
    if (_key_manager) {
        switch (_key_manager->update()) {
            case input::KeyEvent::GoHome:
                close();
                return;
            case input::KeyEvent::GoPrevious: {
                LvglLockGuard lock;
                if (_detail.isOpen()) {
                    _detail.close();
                } else if (_watch_face_manager) {
                    _watch_face_manager->goPrevious();
                }
                break;
            }
            case input::KeyEvent::GoNext: {
                // B pages through the agents' latest replies; swipe still changes the face.
                LvglLockGuard lock;
                if (!_detail.isOpen()) {
                    _detail.open(lv_screen_active(), VoiceAgent::Claude);
                } else if (_detail.agent() == VoiceAgent::Claude) {
                    _detail.open(lv_screen_active(), VoiceAgent::Gpt);
                } else {
                    _detail.close();
                }
                break;
            }
            default:
                break;
        }
    }

    LvglLockGuard lock;

    _watch_face_manager->update();
    _detail.update();
}

// Called when the App is closed
// You can destroy UI, release resources, etc. here
void AppWatchFace::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _detail.close();
    _watch_face_manager.reset();
}
