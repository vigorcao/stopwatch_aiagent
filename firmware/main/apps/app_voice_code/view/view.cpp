/*
 * Voice Code mascot: a hand-drawn orange box with stubby legs, one arm raised,
 * dot eyes and blush, standing on a grass line (screen is 468x466, round).
 */
#include "view.h"
#include <cmath>

using namespace view;

namespace {

constexpr uint32_t _color_bg      = 0xF1ECE2;
constexpr uint32_t _color_body    = 0xE09B63;
constexpr uint32_t _color_offline = 0xC8B9AC;
constexpr uint32_t _color_outline = 0x2E1E16;
constexpr uint32_t _color_hatch   = 0xC57B45;
constexpr uint32_t _color_blush   = 0xF29C9C;
constexpr uint32_t _color_ground  = 0x6B6B6B;
constexpr uint32_t _color_title   = 0x2E1E16;
constexpr uint32_t _color_detail  = 0x6B5B50;
constexpr uint32_t _color_hint    = 0x9A8C80;

constexpr int _outline = 4;

// Body box and parts, in root (screen) coordinates.
constexpr int _body_x = 104, _body_y = 145, _body_w = 260, _body_h = 140;
constexpr int _leg_y = 272, _leg_w = 24, _leg_h = 56;
constexpr int _leg_x[4] = {124, 170, 273, 318};
constexpr int _ground_y = 328;
constexpr int _ground_x0 = 64, _ground_x1 = 404;
constexpr int _grass_spacing = 34;

// Eyes / blush relative to the body's content area (inside the outline).
constexpr int _eye_x[2] = {55, 151};
constexpr int _eye_y = 36, _eye_w = 24, _eye_h = 36;
constexpr int _blush_x[2] = {33, 167};
constexpr int _blush_y = 73;

constexpr int _hatch_count = 15;
lv_point_precise_t _hatch_points[_hatch_count][2];
lv_point_precise_t _grass_points[2] = {{0, 12}, {7, 0}};
lv_point_precise_t _ground_points[2] = {{_ground_x0, 0}, {_ground_x1, 0}};

void style_plain(lv_obj_t* obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int y)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, 360);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

float wave(uint32_t now, float speed)
{
    return std::sin(static_cast<float>(now) * speed);
}

}  // namespace

lv_obj_t* VoiceCodeView::make_part(lv_obj_t* parent, int x, int y, int w, int h, int radius)
{
    lv_obj_t* obj = lv_obj_create(parent);
    style_plain(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(_color_body), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(_color_outline), 0);
    lv_obj_set_style_border_width(obj, _outline, 0);
    return obj;
}

void VoiceCodeView::init(lv_obj_t* parent)
{
    _screen = lv_obj_create(parent);
    style_plain(_screen);
    lv_obj_set_size(_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(_screen, 0, 0);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(_color_bg), 0);

    // Ground line + grass ticks stay put while the mascot bobs.
    lv_obj_t* ground = lv_line_create(_screen);
    lv_line_set_points(ground, _ground_points, 2);
    lv_obj_set_pos(ground, 0, _ground_y);
    lv_obj_set_style_line_width(ground, 3, 0);
    lv_obj_set_style_line_rounded(ground, true, 0);
    lv_obj_set_style_line_color(ground, lv_color_hex(_color_ground), 0);
    for (auto& tick : _grass) {
        tick = lv_line_create(_screen);
        lv_line_set_points(tick, _grass_points, 2);
        lv_obj_set_style_line_width(tick, 2, 0);
        lv_obj_set_style_line_color(tick, lv_color_hex(_color_ground), 0);
    }
    layout_grass(0);

    _root = lv_obj_create(_screen);
    style_plain(_root);
    lv_obj_set_style_bg_opa(_root, LV_OPA_TRANSP, 0);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(_root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // Limbs first so the body covers their joints.
    for (int i = 0; i < 4; ++i) {
        _legs[i] = make_part(_root, _leg_x[i], _leg_y, _leg_w, _leg_h, 3);
    }
    _arm_left = make_part(_root, _body_x - 54, _body_y + 35, 64, 28, 3);
    lv_obj_set_style_transform_pivot_x(_arm_left, 60, 0);
    lv_obj_set_style_transform_pivot_y(_arm_left, 14, 0);
    _arm_right = make_part(_root, _body_x + _body_w - 8, _body_y + 51, 38, 34, 3);
    lv_obj_set_style_transform_pivot_x(_arm_right, 4, 0);
    lv_obj_set_style_transform_pivot_y(_arm_right, 17, 0);

    _body = make_part(_root, _body_x, _body_y, _body_w, _body_h, 14);

    // Pencil hatching like the reference drawing.
    for (int i = 0; i < _hatch_count; ++i) {
        const lv_value_precise_t x0 = -50 + i * 22;
        _hatch_points[i][0]         = {x0, _body_h};
        _hatch_points[i][1]         = {x0 + 64, 0};
        lv_obj_t* line      = lv_line_create(_body);
        lv_line_set_points(line, _hatch_points[i], 2);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(_color_hatch), 0);
        lv_obj_set_style_line_opa(line, LV_OPA_40, 0);
    }

    for (int i = 0; i < 2; ++i) {
        _blush[i] = lv_obj_create(_body);
        style_plain(_blush[i]);
        lv_obj_set_size(_blush[i], 30, 14);
        lv_obj_set_pos(_blush[i], _blush_x[i], _blush_y);
        lv_obj_set_style_radius(_blush[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(_blush[i], lv_color_hex(_color_blush), 0);
        lv_obj_set_style_bg_opa(_blush[i], LV_OPA_70, 0);

        _eyes[i] = lv_obj_create(_body);
        style_plain(_eyes[i]);
        lv_obj_set_size(_eyes[i], _eye_w, _eye_h);
        lv_obj_set_pos(_eyes[i], _eye_x[i], _eye_y);
        lv_obj_set_style_radius(_eyes[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(_eyes[i], lv_color_hex(_color_outline), 0);

        lv_obj_t* glint = lv_obj_create(_eyes[i]);
        style_plain(glint);
        lv_obj_set_size(glint, 6, 6);
        lv_obj_set_pos(glint, 5, 6);
        lv_obj_set_style_radius(glint, 1, 0);
        lv_obj_set_style_bg_color(glint, lv_color_white(), 0);
    }

    _title  = make_label(_screen, &lv_font_montserrat_28, _color_title, 70);
    _detail = make_label(_screen, &lv_font_montserrat_18, _color_detail, 348);
    _hint   = make_label(_screen, &lv_font_montserrat_16, _color_hint, 384);

    _mood = Mood::Offline;  // force setMood to apply
    setMood(Mood::Idle);
}

void VoiceCodeView::destroy()
{
    if (_screen) {
        lv_obj_delete(_screen);
        _screen = nullptr;
    }
}

void VoiceCodeView::layout_grass(float offset)
{
    const int span = static_cast<int>(_grass.size()) * _grass_spacing;
    for (std::size_t i = 0; i < _grass.size(); ++i) {
        int x = _ground_x0 + 10 + static_cast<int>(i * _grass_spacing - offset);
        x     = _ground_x0 + 10 + ((x - _ground_x0 - 10) % span + span) % span;
        lv_obj_set_pos(_grass[i], x, _ground_y - 13);
    }
}

void VoiceCodeView::setMood(Mood mood, std::string_view detail)
{
    const bool changed = mood != _mood;
    _mood              = mood;
    if (changed) {
        _mood_since = lv_tick_get();
    }

    const char* title = "";
    const char* hint  = "Hold A to talk";
    switch (mood) {
        case Mood::Idle: title = "Ready"; break;
        case Mood::Listening: title = "Listening..."; hint = "Release A to send"; break;
        case Mood::Sending: title = "Sending..."; hint = ""; break;
        case Mood::Thinking: title = "Thinking..."; break;
        case Mood::Working: title = "Working"; break;
        case Mood::Permission: title = "Needs you!"; hint = "Check your Mac"; break;
        case Mood::Done: title = "Done!"; break;
        case Mood::Error: title = "Oops"; break;
        case Mood::Offline:
            title  = "Not connected";
            detail = detail.empty() ? "Pair \"M5 Voice Code\" in Mac Bluetooth" : detail;
            hint   = "A+B hold: home";
            break;
    }
    lv_label_set_text(_title, title);
    lv_label_set_text(_detail, std::string(detail).c_str());
    lv_label_set_text(_hint, hint);

    const uint32_t body_color = mood == Mood::Offline ? _color_offline : _color_body;
    for (lv_obj_t* part : {_body, _arm_left, _arm_right, _legs[0], _legs[1], _legs[2], _legs[3]}) {
        lv_obj_set_style_bg_color(part, lv_color_hex(body_color), 0);
    }
}

void VoiceCodeView::update(uint32_t now)
{
    const float dt = _last_update ? static_cast<float>(now - _last_update) : 0.0f;
    _last_update   = now;
    const float t  = static_cast<float>(now - _mood_since) / 1000.0f;

    int root_x = 0, root_y = 0;
    int arm_left_rot = 150, arm_right_rot = 0;  // 0.1 degree, clockwise
    int leg_swing = 0;
    int eye_dx = 0, eye_dy = 0, eye_h = _eye_h;

    switch (_mood) {
        case Mood::Idle:
            root_y       = static_cast<int>(wave(now, 0.003f) * 3);
            arm_left_rot = 250 + static_cast<int>(wave(now, 0.004f) * 60);
            break;
        case Mood::Listening:
            root_y       = static_cast<int>(wave(now, 0.012f) * 4);
            arm_left_rot = 350 + static_cast<int>(wave(now, 0.02f) * 180);
            eye_h        = _eye_h + 6;
            break;
        case Mood::Sending:
            root_y       = static_cast<int>(-std::fabs(wave(now, 0.01f)) * 8);
            arm_left_rot = 500;
            arm_right_rot = -300;
            break;
        case Mood::Thinking:
            root_y = static_cast<int>(wave(now, 0.002f) * 2);
            eye_dx = static_cast<int>(wave(now, 0.004f) * 10);
            eye_dy = -4;
            break;
        case Mood::Working:
            root_y    = static_cast<int>(-std::fabs(wave(now, 0.012f)) * 5);
            leg_swing = static_cast<int>(wave(now, 0.012f) * 6);
            arm_left_rot  = 150 + static_cast<int>(wave(now, 0.012f) * 120);
            arm_right_rot = static_cast<int>(wave(now, 0.012f) * 120);
            _ground_offset += dt * 0.12f;
            layout_grass(_ground_offset);
            break;
        case Mood::Permission:
            root_y        = static_cast<int>(-std::fabs(wave(now, 0.008f)) * 28);
            arm_left_rot  = 450 + static_cast<int>(wave(now, 0.03f) * 150);
            arm_right_rot = -300 + static_cast<int>(wave(now, 0.03f) * 150);
            eye_h         = _eye_h + 6;
            break;
        case Mood::Done:
            if (t < 1.2f) {
                root_y = static_cast<int>(-std::fabs(std::sin(t * 3.14159f * 2.5f)) * 30);
            }
            arm_left_rot  = 600;
            arm_right_rot = -450;
            eye_h         = 8;  // happy squint
            break;
        case Mood::Error:
            if (t < 0.6f) {
                root_x = static_cast<int>(wave(now, 0.06f) * 6);
            }
            eye_h = 12;
            break;
        case Mood::Offline:
            eye_h = 14;
            arm_left_rot = 100;
            break;
    }

    // Blink every few seconds unless the pose already fixes the eye shape.
    if (now >= _next_blink) {
        _blink_until = now + 120;
        _next_blink  = now + 2600 + (now * 7919) % 2400;
    }
    if (now < _blink_until && eye_h >= _eye_h) {
        eye_h = 4;
    }

    lv_obj_set_pos(_root, root_x, root_y);
    lv_obj_set_style_transform_rotation(_arm_left, arm_left_rot, 0);
    lv_obj_set_style_transform_rotation(_arm_right, arm_right_rot, 0);
    for (int i = 0; i < 4; ++i) {
        lv_obj_set_y(_legs[i], _leg_y + ((i % 2) ? leg_swing : -leg_swing));
    }
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_pos(_eyes[i], _eye_x[i] + eye_dx, _eye_y + eye_dy + (_eye_h - eye_h) / 2);
        lv_obj_set_height(_eyes[i], eye_h);
    }
}
