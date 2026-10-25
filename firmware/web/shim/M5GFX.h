// Web simulator shim: apps render through LVGL only; the M5GFX device/canvas
// accessors in hal.h are kept for API compatibility but never drawn to.
#pragma once
class LGFX_Device {};
class LGFX_Sprite {};
