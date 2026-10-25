/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <lv_demos.h>
#include <apps/common/audio/audio.h>
#ifdef ESP_PLATFORM
#include <esp_task_wdt.h>
#endif

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();

    // Bring the Mac link up at boot so Claude / Codex status reaches the watch
    // face without first opening Voice Code.
    GetHAL().voiceLinkStart();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Install apps
    GetMooncake().installApp(std::make_unique<AppLauncher>());
    GetMooncake().installApp(std::make_unique<AppVoiceCode>(AppVoiceCode::claude()));
    GetMooncake().installApp(std::make_unique<AppVoiceCode>(AppVoiceCode::gpt()));
#ifdef __EMSCRIPTEN__
    // 4MB storage only fits heidou + meiqiu3d on the device; kurotamago is simulator-only.
    GetMooncake().installApp(std::make_unique<AppVoiceCode>(AppVoiceCode::kurotamago()));
#endif
    GetMooncake().installApp(std::make_unique<AppVoiceCode>(AppVoiceCode::meiqiu()));
    GetMooncake().installApp(std::make_unique<AppAura>());
    GetMooncake().installApp(std::make_unique<AppAlarmClock>());
    GetMooncake().installApp(std::make_unique<AppWatchFace>());
    GetMooncake().installApp(std::make_unique<AppStopWatch>());
    GetMooncake().installApp(std::make_unique<AppTimer>());
    GetMooncake().installApp(std::make_unique<AppBadge>());
    GetMooncake().installApp(std::make_unique<AppImu>());
    GetMooncake().installApp(std::make_unique<AppFft>());
    GetMooncake().installApp(std::make_unique<AppLuckyWheel>());
    GetMooncake().installApp(std::make_unique<AppSetup>());
    // GetMooncake().installApp(std::make_unique<AppTemplate>());

    // Main loop. Watched by the task watchdog: if it stalls for 10 s (e.g. a
    // deadlock) the watch panics and the core dump shows where it was stuck.
#ifdef ESP_PLATFORM
    esp_task_wdt_add(nullptr);
#endif
    while (1) {
#ifdef ESP_PLATFORM
        esp_task_wdt_reset();
#endif
        GetHAL().feedTheDog();
        GetMooncake().update();
    }
}
