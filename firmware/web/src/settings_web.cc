// Web simulator implementation of Settings (NVS) backed by browser localStorage,
// so values survive page reloads the same way NVS survives reboots.
#include "hal/utils/settings/settings.h"
#include <emscripten.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// clang-format off
EM_JS(char*, web_ls_get, (const char* key), {
    const v = localStorage.getItem("m5sw:" + UTF8ToString(key));
    if (v === null) return 0;
    return stringToNewUTF8(v);
});
EM_JS(void, web_ls_set, (const char* key, const char* value), {
    localStorage.setItem("m5sw:" + UTF8ToString(key), UTF8ToString(value));
});
EM_JS(void, web_ls_remove, (const char* key), {
    localStorage.removeItem("m5sw:" + UTF8ToString(key));
});
EM_JS(void, web_ls_clear_ns, (const char* ns), {
    const prefix = "m5sw:" + UTF8ToString(ns) + ".";
    Object.keys(localStorage).filter(k => k.startsWith(prefix)).forEach(k => localStorage.removeItem(k));
});
// clang-format on

namespace {

bool read_raw(const std::string& full_key, std::string& out)
{
    char* v = web_ls_get(full_key.c_str());
    if (v == nullptr) {
        return false;
    }
    out = v;
    free(v);
    return true;
}

}  // namespace

Settings::Settings(const std::string& ns, bool read_write) : ns_(ns), read_write_(read_write)
{
}

Settings::~Settings()
{
}

std::string Settings::GetString(const std::string& key, const std::string& default_value)
{
    std::string v;
    return read_raw(ns_ + "." + key, v) ? v : default_value;
}

void Settings::SetString(const std::string& key, const std::string& value)
{
    web_ls_set((ns_ + "." + key).c_str(), value.c_str());
}

int32_t Settings::GetInt(const std::string& key, int32_t default_value)
{
    std::string v;
    return read_raw(ns_ + "." + key, v) ? static_cast<int32_t>(std::strtol(v.c_str(), nullptr, 10)) : default_value;
}

void Settings::SetInt(const std::string& key, int32_t value)
{
    SetString(key, std::to_string(value));
}

bool Settings::GetBool(const std::string& key, bool default_value)
{
    return GetInt(key, default_value ? 1 : 0) != 0;
}

void Settings::SetBool(const std::string& key, bool value)
{
    SetInt(key, value ? 1 : 0);
}

esp_err_t Settings::GetBlob(const std::string& key, void* value, size_t* length)
{
    std::string hex;
    if (!read_raw(ns_ + "." + key, hex)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    const size_t size = hex.size() / 2;
    if (value == nullptr) {
        *length = size;
        return ESP_OK;
    }
    if (*length < size) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    auto* bytes = static_cast<uint8_t*>(value);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<uint8_t>(std::strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16));
    }
    *length = size;
    return ESP_OK;
}

esp_err_t Settings::SetBlob(const std::string& key, const void* value, size_t length)
{
    std::string hex;
    hex.reserve(length * 2);
    char buf[3];
    for (size_t i = 0; i < length; ++i) {
        std::snprintf(buf, sizeof(buf), "%02x", static_cast<const uint8_t*>(value)[i]);
        hex += buf;
    }
    SetString(key, hex);
    return ESP_OK;
}

void Settings::EraseKey(const std::string& key)
{
    web_ls_remove((ns_ + "." + key).c_str());
}

void Settings::EraseAll()
{
    web_ls_clear_ns(ns_.c_str());
}
