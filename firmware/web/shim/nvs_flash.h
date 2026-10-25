// Web simulator shim for the subset of ESP-IDF NVS types used by settings.h.
#pragma once
#include <cstddef>
#include <cstdint>
typedef int esp_err_t;
typedef uint32_t nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
inline const char* esp_err_to_name(esp_err_t err) { return err == ESP_OK ? "ESP_OK" : "ESP_ERR"; }
