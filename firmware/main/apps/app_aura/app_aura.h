/*
 * Aura: a field of glyphs you dial like a knob: turn clockwise to spread it,
 * counter-clockwise to fold it back; hold A to reset.
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <lvgl.h>
#include <mooncake.h>
#include <memory>

class AppAura : public mooncake::AppAbility {
public:
    AppAura();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

    // Current field state, read by the draw callback.
    struct Field {
        float yaw_deg    = 0.0f;  // integrated rotation about the screen normal
        float radius     = 40.0f;
        float center_x   = 0.0f;  // drift toward the low side
        float center_y   = 0.0f;
        float energy     = 0.0f;  // recent shake, 0..1
        uint32_t tick    = 0;
    };

private:
    std::unique_ptr<input::KeyManager> _key_manager;
    lv_obj_t* _canvas       = nullptr;
    Field _field;
    uint32_t _last_tick     = 0;
    float _spread           = 0.0f;  // 0 = core only, 1 = full screen
};
