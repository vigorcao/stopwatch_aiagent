/*
 * Aura app, see app_aura.h.
 *
 * Mapping (accel in g, gyro in deg/s):
 *   turn clockwise / counter  -> spread the bloom out / fold it back (full at ~120 deg)
 *   yaw   = integral gyro.z   -> grid rotation and colour hue
 *   accel.xy direction        -> the bloom drifts toward the low side
 *   |gyro| spikes             -> glyphs scramble faster
 *   hold A                    -> reset to the upright core and starting colours
 */
#include "app_aura.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <algorithm>
#include <cmath>

namespace {

constexpr int _screen_w     = 468;
constexpr int _screen_h     = 466;
constexpr int _spacing      = 25;
constexpr int _half_cells   = 10;  // grid spans (2*9+1) cells per side
constexpr float _min_radius = 58.0f;
constexpr float _max_radius = 250.0f;
constexpr float _hue_offset = 28.0f;
constexpr float _full_spread_deg = 120.0f;  // clockwise turn from core to full screen
constexpr float _spread_dir = 1.0f;         // flip to -1 if clockwise reads as negative gyro Z  // start on orange + green like the reference

const char* const _glyphs[] = {"+", "x", "#", "*", "=", "o", "%", "~", "/", "&"};
constexpr int _glyph_count  = sizeof(_glyphs) / sizeof(_glyphs[0]);

uint32_t hash3(int x, int y, uint32_t z)
{
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + z * 2246822519u;
    h          = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

void draw_field(lv_event_t* e)
{
    const auto* field = static_cast<const AppAura::Field*>(lv_event_get_user_data(e));
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_obj_t* obj     = static_cast<lv_obj_t*>(lv_event_get_target(e));

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const float cx = coords.x1 + _screen_w / 2.0f + field->center_x;
    const float cy = coords.y1 + _screen_h / 2.0f + field->center_y;

    // Palette follows the rotation: an accent hue plus its complement.
    const uint16_t hue       = static_cast<uint16_t>(std::fmod(std::fmod(field->yaw_deg + _hue_offset, 360.0f) + 360.0f, 360.0f));
    const lv_color_t accent  = lv_color_hsv_to_rgb(hue, 62, 100);
    const lv_color_t partner = lv_color_hsv_to_rgb((hue + 150) % 360, 70, 85);

    const float rad = field->yaw_deg * 3.14159265f / 180.0f;
    const float cs = std::cos(rad), sn = std::sin(rad);

    // Glyphs reshuffle every ~300 ms, faster while shaken.
    const uint32_t period = 150 - static_cast<uint32_t>(field->energy * 100.0f);
    const uint32_t epoch  = field->tick / std::max<uint32_t>(period, 40);

    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.font  = &lv_font_montserrat_22;
    label.align = LV_TEXT_ALIGN_CENTER;

    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);

    for (int gy = -_half_cells; gy <= _half_cells; ++gy) {
        for (int gx = -_half_cells; gx <= _half_cells; ++gx) {
            const float lx = gx * _spacing, ly = gy * _spacing;
            const float dist = std::sqrt(lx * lx + ly * ly);
            // Ragged edge: each cell gets its own reach.
            const uint32_t h   = hash3(gx, gy, 7);
            const float reach  = field->radius * (0.82f + 0.18f * ((h & 0xFF) / 255.0f));
            if (dist > reach) {
                continue;
            }

            const float px = cx + lx * cs - ly * sn;
            const float py = cy + lx * sn + ly * cs;
            if (px < coords.x1 - 10 || px > coords.x2 + 10 || py < coords.y1 - 10 || py > coords.y2 + 10) {
                continue;
            }

            const float fade      = 1.0f - dist / std::max(reach, 1.0f);
            const lv_opa_t opa    = static_cast<lv_opa_t>(std::clamp(120.0f + 135.0f * fade, 0.0f, 255.0f));
            const uint32_t pick   = hash3(gx, gy, epoch + ((h >> 8) & 3));
            const lv_color_t color = (pick % 3 == 0) ? partner : accent;

            if (pick % 5 == 0) {
                // Solid blocks and dots between the symbols.
                const int s = (pick % 2) ? 14 : 10;
                lv_area_t a = {static_cast<int32_t>(px - s / 2), static_cast<int32_t>(py - s / 2),
                               static_cast<int32_t>(px + s / 2), static_cast<int32_t>(py + s / 2)};
                dot.bg_color = color;
                dot.bg_opa   = opa;
                dot.radius   = (pick % 2) ? 2 : LV_RADIUS_CIRCLE;
                lv_draw_rect(layer, &dot, &a);
                continue;
            }

            label.text  = _glyphs[(pick >> 4) % _glyph_count];
            label.color = color;
            label.opa   = opa;
            lv_area_t a = {static_cast<int32_t>(px - 12), static_cast<int32_t>(py - 11), static_cast<int32_t>(px + 12),
                           static_cast<int32_t>(py + 11)};
            lv_draw_label(layer, &label, &a);
        }
    }
}

}  // namespace

AppAura::AppAura()
{
    setAppInfo().name = "Aura";
    setAppInfo().icon = (void*)&icon_aura;
}

void AppAura::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppAura::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();
    _field       = Field{};
    _spread      = 0.0f;
    _last_tick   = GetHAL().millis();

    LvglLockGuard lock;
    _canvas = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(_canvas);
    lv_obj_set_size(_canvas, _screen_w, _screen_h);
    lv_obj_center(_canvas);
    lv_obj_set_style_bg_color(_canvas, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_canvas, LV_OPA_COVER, 0);
    lv_obj_remove_flag(_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_canvas, draw_field, LV_EVENT_DRAW_MAIN, &_field);
}

void AppAura::onRunning()
{
    if (_key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    auto& hal          = GetHAL();
    const uint32_t now = hal.millis();
    const float dt     = std::clamp((now - _last_tick) / 1000.0f, 0.001f, 0.1f);
    _last_tick         = now;

    hal.updateImuData();
    const auto& imu = hal.getImuData();

    const float motion = std::sqrt(imu.gyroX * imu.gyroX + imu.gyroY * imu.gyroY + imu.gyroZ * imu.gyroZ);

    // Critically-damped-ish easing keeps the bloom fluid instead of jittery.
    const float k = 1.0f - std::exp(-dt * 10.0f);
    // Knob: clockwise spreads the bloom, counter-clockwise folds it back,
    // and it stays put when the watch is still.
    if (std::fabs(imu.gyroZ) > 3.0f) {
        _spread += _spread_dir * imu.gyroZ * dt / _full_spread_deg;
    }
    _spread = std::clamp(_spread, 0.0f, 1.0f);
    const float target = _min_radius + (_max_radius - _min_radius) * _spread;
    _field.radius += (target + _field.energy * 40.0f - _field.radius) * k;
    _field.center_x += (imu.accelX * 45.0f - _field.center_x) * k;
    _field.center_y += (imu.accelY * 45.0f - _field.center_y) * k;
    if (std::fabs(imu.gyroZ) > 3.0f) {  // ignore gyro bias at rest
        _field.yaw_deg += imu.gyroZ * dt;
    }
    _field.energy = std::max(_field.energy * std::exp(-dt * 2.5f), std::clamp((motion - 120.0f) / 400.0f, 0.0f, 1.0f));

    // Hold A to reset: fold back to the core, turn upright, return to the
    // starting palette (at most half a turn, never unwinding whole spins).
    if (hal.btnA.isPressed() && hal.btnB.isReleased()) {
        const float r = 1.0f - std::exp(-dt * 8.0f);
        _field.yaw_deg = std::remainder(_field.yaw_deg, 360.0f);
        _field.yaw_deg -= _field.yaw_deg * r;
        _spread -= _spread * r;
        _field.energy = 0.0f;
    }
    _field.tick   = now;

    LvglLockGuard lock;
    lv_obj_invalidate(_canvas);
}

void AppAura::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    _key_manager.reset();

    LvglLockGuard lock;
    if (_canvas) {
        lv_obj_delete(_canvas);
        _canvas = nullptr;
    }
}
