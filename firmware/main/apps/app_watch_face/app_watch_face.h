/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/watch_face.h"
#include <apps/common/key_manager/key_manager.h>
#include <apps/common/agent_detail/agent_detail.h>
#include <mooncake.h>
#include <memory>

/**
 * @brief Derived App
 *
 */
class AppWatchFace : public mooncake::AppAbility {
public:
    AppWatchFace();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<view::WatchFaceManager> _watch_face_manager;
    view::AgentDetailPage _detail;  // B: Claude -> Codex -> close
    std::unique_ptr<input::KeyManager> _key_manager;
};
