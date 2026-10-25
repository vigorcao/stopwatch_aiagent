/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../model/countdown.h"
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <array>
#include <memory>
#include <string>

namespace view {

/**
 * @brief Countdown timer: pick minutes/seconds, then a ring of ticks vanishes
 * clockwise from 12 o'clock while its color goes from green to red.
 */
class TimerView {
public:
    TimerView(model::Countdown& countdown, uint32_t& lastDurationMs);
    ~TimerView();

    // overlay = shown over another app when the timer ends in the background
    void init(lv_obj_t* parent, bool overlay = false);
    void update(bool updateButtonStates = true);

    // The user acknowledged the finished alarm
    bool isDismissed() const
    {
        return _dismissed;
    }

private:
    static constexpr int _tick_count = 60;

    model::Countdown& _countdown;
    uint32_t& _last_duration;
    model::Countdown::State_t _shown_state = model::Countdown::State_t::Idle;
    bool _alarm_on          = false;
    bool _dismissed         = false;
    uint32_t _finished_time = 0;
    int _preset_index       = -1;

    std::unique_ptr<uitk::lvgl_cpp::Container> _root;

    // Setup page
    std::unique_ptr<uitk::lvgl_cpp::Container> _setup_page;
    std::unique_ptr<uitk::lvgl_cpp::Label> _setup_title;
    std::unique_ptr<uitk::lvgl_cpp::Roller> _minute_selector;
    std::unique_ptr<uitk::lvgl_cpp::Roller> _second_selector;
    std::unique_ptr<uitk::lvgl_cpp::Label> _selector_colon;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_start;

    // Countdown page
    std::unique_ptr<uitk::lvgl_cpp::Container> _run_page;
    std::array<lv_obj_t*, _tick_count> _ticks{};
    std::array<std::array<lv_point_precise_t, 2>, _tick_count> _tick_points{};
    std::array<int, _tick_count> _tick_opa{};
    int _tick_hue = -1;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_remaining;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_total;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_left;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_right;
    uint32_t _shown_seconds = UINT32_MAX;

    void create_setup_page();
    void create_run_page();
    void create_ticks();

    void handle_btn_left_clicked();
    void handle_btn_right_clicked();
    void handle_start();
    void handle_dismiss();
    void apply_preset_next();

    void apply_state();
    void update_ring();
    void update_labels();
    void set_alarm(bool on);
};

}  // namespace view
