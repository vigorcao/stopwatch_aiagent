/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <cmath>
#include <cstdio>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;
using State = model::Countdown::State_t;

namespace {

constexpr int _screen_size = 466;

// Tick ring
constexpr int _tick_outer_r       = 226;
constexpr int _tick_major_inner_r = 194;
constexpr int _tick_minor_inner_r = 206;
constexpr int _tick_major_width   = 8;
constexpr int _tick_minor_width   = 5;
constexpr int _hue_full           = 120;  // green; 0 is red

constexpr int _btn_left_x  = -72;
constexpr int _btn_right_x = 72;
constexpr int _btn_y       = 128;

constexpr uint32_t _bg_color                = 0x000000;
constexpr uint32_t _label_color             = 0xFFFFFF;
constexpr uint32_t _total_color             = 0x738086;
constexpr uint32_t _finished_color          = 0xFF5A5A;
constexpr uint32_t _selector_color          = 0x343434;
constexpr uint32_t _selector_selected_color = 0x696969;
constexpr uint32_t _start_color             = 0x4AD78C;
constexpr uint32_t _start_text_color        = 0x0F5831;
constexpr uint32_t _btn_reset_color         = 0xB3CDFF;
constexpr uint32_t _btn_pause_color         = 0xFF9EAB;
constexpr uint32_t _btn_resume_color        = 0x9CF1B6;

// The alarm rings at most this long when nobody dismisses it
constexpr uint32_t _alarm_timeout_ms = 60 * 1000;

// Button A on the setup page steps through these (minutes)
constexpr int _presets[] = {1, 3, 5, 10, 15, 25, 30, 45, 60};

std::string build_number_options(int begin, int end)
{
    std::string options;
    for (int value = begin; value <= end; ++value) {
        char buffer[4] = {};
        std::snprintf(buffer, sizeof(buffer), "%02d", value);
        if (!options.empty()) {
            options.push_back('\n');
        }
        options += buffer;
    }
    return options;
}

std::string format_mm_ss(uint32_t seconds)
{
    return fmt::format("{:02d}:{:02d}", seconds / 60, seconds % 60);
}

void apply_selector_style(lv_obj_t* roller)
{
    lv_obj_set_style_radius(roller, 52, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, lv_color_hex(_selector_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(roller, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(roller, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_hex(_label_color), LV_PART_MAIN);
    lv_obj_set_style_text_align(roller, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(roller, 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, lv_color_hex(_selector_selected_color), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_font(roller, &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, lv_color_hex(_label_color), LV_PART_SELECTED);
    lv_obj_set_style_text_align(roller, LV_TEXT_ALIGN_CENTER, LV_PART_SELECTED);
    lv_obj_set_style_border_width(roller, 0, LV_PART_SELECTED);
}

void apply_button_style(Button* button, int x)
{
    button->setRadius(72 / 2 - 2);
    button->setBorderWidth(0);
    button->setShadowWidth(0);
    button->setSize(105, 72);
    button->align(LV_ALIGN_CENTER, x, _btn_y);
    button->label().setTextFont(&lv_font_maple_mono_medium_24);
}

void set_button_color(Button* button, uint32_t color)
{
    button->setBgColor(lv_color_hex(color));
    button->label().setTextColor(lv_color_hex(color::blend_in_difference(color, 0x858585).toHex()));
}

std::unique_ptr<Container> make_page(lv_obj_t* parent)
{
    auto page = std::make_unique<Container>(parent);
    page->align(LV_ALIGN_CENTER, 0, 0);
    page->setSize(_screen_size, _screen_size);
    page->setRadius(0);
    page->setBorderWidth(0);
    page->setPaddingAll(0);
    page->setBgOpa(LV_OPA_TRANSP);
    page->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

}  // namespace

TimerView::TimerView(model::Countdown& countdown, uint32_t& lastDurationMs)
    : _countdown(countdown), _last_duration(lastDurationMs)
{
}

TimerView::~TimerView()
{
    set_alarm(false);
}

void TimerView::init(lv_obj_t* parent, bool overlay)
{
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    _root = std::make_unique<Container>(parent);
    _root->align(LV_ALIGN_CENTER, 0, 0);
    _root->setSize(_screen_size, _screen_size);
    _root->setRadius(0);
    _root->setBorderWidth(0);
    _root->setPaddingAll(0);
    _root->setBgColor(lv_color_hex(_bg_color));
    _root->setBgOpa(LV_OPA_COVER);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    if (overlay) {
        _root->moveForeground();
    }

    create_setup_page();
    create_run_page();

    _shown_state = _countdown.getState();
    if (_shown_state == State::Finished) {
        _finished_time = GetHAL().millis();
        set_alarm(true);
    }
    apply_state();
}

void TimerView::create_setup_page()
{
    _setup_page = make_page(_root->get());

    _setup_title = std::make_unique<Label>(_setup_page->get());
    _setup_title->align(LV_ALIGN_TOP_MID, 0, 75);
    _setup_title->setText("Timer");
    _setup_title->setTextFont(&MontserratSemiBold26);
    _setup_title->setTextColor(lv_color_hex(_label_color));

    const uint32_t last_seconds = _last_duration / 1000;

    const auto minute_options = build_number_options(0, 99);
    _minute_selector          = std::make_unique<Roller>(_setup_page->get());
    _minute_selector->align(LV_ALIGN_CENTER, -90, -13);
    _minute_selector->setSize(160, 200);
    _minute_selector->setOptions(minute_options.c_str(), LV_ROLLER_MODE_INFINITE);
    _minute_selector->setVisibleRowCount(5);
    _minute_selector->setSelected(last_seconds / 60, LV_ANIM_OFF);
    apply_selector_style(_minute_selector->get());

    const auto second_options = build_number_options(0, 59);
    _second_selector          = std::make_unique<Roller>(_setup_page->get());
    _second_selector->align(LV_ALIGN_CENTER, 90, -13);
    _second_selector->setSize(160, 200);
    _second_selector->setOptions(second_options.c_str(), LV_ROLLER_MODE_INFINITE);
    _second_selector->setVisibleRowCount(5);
    _second_selector->setSelected(last_seconds % 60, LV_ANIM_OFF);
    apply_selector_style(_second_selector->get());

    _selector_colon = std::make_unique<Label>(_setup_page->get());
    _selector_colon->align(LV_ALIGN_CENTER, 0, -15);
    _selector_colon->setText(":");
    _selector_colon->setTextFont(&lv_font_montserrat_28);
    _selector_colon->setTextColor(lv_color_hex(_label_color));

    _btn_start = std::make_unique<Button>(_setup_page->get());
    _btn_start->align(LV_ALIGN_CENTER, 0, 175);
    _btn_start->setSize(374, 130);
    _btn_start->setRadius(77);
    _btn_start->setBorderWidth(0);
    _btn_start->setShadowWidth(0);
    _btn_start->setBgColor(lv_color_hex(_start_color));
    _btn_start->label().setText("START");
    _btn_start->label().setTextFont(&lv_font_montserrat_28);
    _btn_start->label().setTextColor(lv_color_hex(_start_text_color));
    _btn_start->label().align(LV_ALIGN_CENTER, 0, 0);
    _btn_start->onClick().connect([this]() { handle_start(); });
}

void TimerView::create_run_page()
{
    _run_page = make_page(_root->get());

    create_ticks();

    _label_remaining = std::make_unique<Label>(_run_page->get());
    _label_remaining->setTextFont(&CommissionerMedium108);
    _label_remaining->setTextColor(lv_color_hex(_label_color));
    _label_remaining->align(LV_ALIGN_CENTER, 0, -30);

    _label_total = std::make_unique<Label>(_run_page->get());
    _label_total->setTextFont(&lv_font_maple_mono_medium_24);
    _label_total->setTextColor(lv_color_hex(_total_color));
    _label_total->align(LV_ALIGN_CENTER, 0, 48);

    _btn_left = std::make_unique<Button>(_run_page->get());
    apply_button_style(_btn_left.get(), _btn_left_x);
    _btn_left->label().setText("RESET");
    set_button_color(_btn_left.get(), _btn_reset_color);
    _btn_left->onClick().connect([this]() { handle_btn_left_clicked(); });

    _btn_right = std::make_unique<Button>(_run_page->get());
    apply_button_style(_btn_right.get(), _btn_right_x);
    _btn_right->onClick().connect([this]() { handle_btn_right_clicked(); });

    // A tap anywhere stops the alarm when time is up
    lv_obj_add_flag(_run_page->get(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        _run_page->get(),
        [](lv_event_t* e) {
            auto* self = static_cast<TimerView*>(lv_event_get_user_data(e));
            if (self->_countdown.getState() == State::Finished) {
                self->handle_dismiss();
            }
        },
        LV_EVENT_CLICKED, this);
}

void TimerView::create_ticks()
{
    const float c = _screen_size / 2.0f;
    for (int i = 0; i < _tick_count; ++i) {
        const bool major = i % 5 == 0;
        // Tick 0 is at 12 o'clock, then clockwise
        const float a       = (i * 360.0f / _tick_count - 90.0f) * static_cast<float>(M_PI) / 180.0f;
        const float inner_r = major ? _tick_major_inner_r : _tick_minor_inner_r;

        auto& p = _tick_points[i];
        p[0].x  = c + inner_r * std::cos(a);
        p[0].y  = c + inner_r * std::sin(a);
        p[1].x  = c + _tick_outer_r * std::cos(a);
        p[1].y  = c + _tick_outer_r * std::sin(a);

        lv_obj_t* line = lv_line_create(_run_page->get());
        lv_line_set_points(line, p.data(), 2);
        lv_obj_set_pos(line, 0, 0);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_line_width(line, major ? _tick_major_width : _tick_minor_width, LV_PART_MAIN);
        lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN);
        _ticks[i]    = line;
        _tick_opa[i] = -1;
    }
}

void TimerView::update(bool updateButtonStates)
{
    if (updateButtonStates) {
        GetHAL().updateButtonStates();
    }

    _countdown.update();
    if (_countdown.getState() != _shown_state) {
        if (_countdown.getState() == State::Finished) {
            _finished_time = GetHAL().millis();
            set_alarm(true);
        }
        _shown_state = _countdown.getState();
        apply_state();
    }

    // Act on a short click, so the A+B hold that goes home does not also reset the timer
    if (GetHAL().btnA.wasClicked()) {
        handle_btn_left_clicked();
    } else if (GetHAL().btnB.wasClicked()) {
        handle_btn_right_clicked();
    }

    if (_shown_state == State::Finished) {
        // Blink 00:00 while the alarm rings
        const uint32_t since = GetHAL().millis() - _finished_time;
        _label_remaining->setOpa((since / 500) % 2 == 0 || !_alarm_on ? LV_OPA_COVER : LV_OPA_30);
        if (_alarm_on && since > _alarm_timeout_ms) {
            set_alarm(false);
        }
        return;
    }

    if (_shown_state != State::Idle) {
        update_ring();
        update_labels();
    }
}

void TimerView::handle_btn_left_clicked()
{
    switch (_countdown.getState()) {
        case State::Idle:
            apply_preset_next();
            break;
        case State::Running:
        case State::Paused:
            _countdown.reset();
            break;
        case State::Finished:
            handle_dismiss();
            break;
    }
}

void TimerView::handle_btn_right_clicked()
{
    switch (_countdown.getState()) {
        case State::Idle:
            handle_start();
            break;
        case State::Running:
            _countdown.pause();
            break;
        case State::Paused:
            _countdown.resume();
            break;
        case State::Finished:
            handle_dismiss();
            break;
    }
}

void TimerView::handle_start()
{
    const uint32_t seconds = _minute_selector->getSelected() * 60 + _second_selector->getSelected();
    if (seconds == 0) {
        return;
    }
    _last_duration = seconds * 1000;
    _countdown.start(_last_duration);
}

void TimerView::handle_dismiss()
{
    set_alarm(false);
    _countdown.reset();
    _dismissed = true;
}

void TimerView::apply_preset_next()
{
    constexpr int count = sizeof(_presets) / sizeof(_presets[0]);
    _preset_index       = (_preset_index + 1) % count;
    _minute_selector->setSelected(_presets[_preset_index], LV_ANIM_ON);
    _second_selector->setSelected(0, LV_ANIM_ON);
}

void TimerView::apply_state()
{
    const bool idle = _shown_state == State::Idle;
    _setup_page->setHidden(!idle);
    _run_page->setHidden(idle);
    if (idle) {
        return;
    }

    const bool finished = _shown_state == State::Finished;
    _btn_left->setHidden(finished);
    _btn_right->setHidden(finished);
    _label_remaining->setOpa(LV_OPA_COVER);
    _label_remaining->setTextColor(lv_color_hex(finished ? _finished_color : _label_color));

    if (finished) {
        _label_total->setText("TIME'S UP");
    } else {
        _label_total->setText(fmt::format("/ {}", format_mm_ss(_countdown.getDuration() / 1000)));
        const bool running = _shown_state == State::Running;
        _btn_right->label().setText(running ? "PAUSE" : "RESUME");
        set_button_color(_btn_right.get(), running ? _btn_pause_color : _btn_resume_color);
    }

    _shown_seconds = UINT32_MAX;
    update_ring();
    update_labels();
}

void TimerView::update_ring()
{
    const float f = _countdown.getRemainingFraction();

    const int hue = static_cast<int>(std::lround(_hue_full * f));
    if (hue != _tick_hue) {
        _tick_hue         = hue;
        const lv_color_t c = lv_color_hsv_to_rgb(hue, 80, 95);
        for (auto* tick : _ticks) {
            lv_obj_set_style_line_color(tick, c, LV_PART_MAIN);
        }
    }

    // Ticks before `gone` are used up. The tick at the edge fades out gradually.
    const float gone = (1.0f - f) * _tick_count;
    for (int i = 0; i < _tick_count; ++i) {
        float keep = 1.0f;
        if (gone >= i + 1) {
            keep = 0.0f;
        } else if (gone > i) {
            keep = 1.0f - (gone - i);
        }
        const int opa = static_cast<int>(keep * 255.0f);
        if (opa != _tick_opa[i]) {
            _tick_opa[i] = opa;
            lv_obj_set_style_line_opa(_ticks[i], opa, LV_PART_MAIN);
        }
    }
}

void TimerView::update_labels()
{
    // Round up so the label shows the full time at start and 00:00 only at the end
    const uint32_t seconds = (_countdown.getRemainingTime() + 999) / 1000;
    if (seconds != _shown_seconds) {
        _shown_seconds = seconds;
        _label_remaining->setText(format_mm_ss(seconds));
    }
}

void TimerView::set_alarm(bool on)
{
    if (on == _alarm_on) {
        return;
    }
    _alarm_on = on;
    if (on) {
        GetHAL().startAlarm();
    } else {
        GetHAL().stopAlarm();
    }
}
