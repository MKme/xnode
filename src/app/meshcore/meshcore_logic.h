#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

// Hardware-independent input rules. Native regression tests exercise these exact
// functions; callers must not silently truncate a MeshCore group message or key.
namespace xnode_meshcore {
constexpr uint8_t CHANNEL_COUNT = 8;
constexpr size_t MAX_NAME = 31;
constexpr size_t GROUP_TEXT_BYTES = 160; // upstream MAX_TEXT_LEN, including "name: "
constexpr uint32_t BROADCAST = UINT32_MAX;
inline bool valid_key_length(size_t bytes) { return bytes == 16 || bytes == 32; }
inline bool valid_name(const char *s, size_t capacity) {
    if (!s || !capacity) return false;
    const size_t n = strnlen(s, capacity);
    if (!n || n >= capacity) return false;
    for (size_t i = 0; i < n; ++i)
        if ((unsigned char)s[i] < 0x20 || s[i] == ':' || (unsigned char)s[i] == 0x7f) return false;
    return true;
}
inline bool valid_group_text(const char *name, const char *text) {
    if (!name || !text) return false;
    const size_t names = strnlen(name, MAX_NAME + 1);
    const size_t bytes = strnlen(text, GROUP_TEXT_BYTES + 1);
    return names && names <= MAX_NAME && bytes && names + 2 + bytes <= GROUP_TEXT_BYTES;
}
inline bool valid_radio(float frequency, float bandwidth, uint8_t sf, uint8_t cr, int8_t power) {
    return isfinite(frequency) && isfinite(bandwidth) &&
        (bandwidth == 62.5f || bandwidth == 125.0f || bandwidth == 250.0f) &&
        frequency >= 902.0f + bandwidth / 2000.0f &&
        frequency <= 928.0f - bandwidth / 2000.0f &&
        sf >= 7 && sf <= 12 && cr >= 5 && cr <= 8 && power >= -9 && power <= 22;
}
// Validate the exact supported upstream v1 wire envelope before the upstream
// dispatcher reads a header, optional transport codes or path-length byte.
// This group-chat client intentionally drops DM/path/command/trace traffic.
inline bool valid_radio_frame(const uint8_t *bytes, size_t size) {
    if (!bytes || size < 2 || size > 255 || (bytes[0] >> 6) != 0) return false;
    const unsigned route = bytes[0] & 3;
    const size_t header = (route == 0 || route == 3) ? 6 : 2;
    if (size < header) return false;
    const unsigned encoded_path = bytes[header - 1];
    const unsigned hash_size = (encoded_path >> 6) + 1;
    const size_t path_bytes = (encoded_path & 63) * hash_size;
    if (hash_size > 3 || path_bytes > 64 || header + path_bytes >= size) return false;
    const size_t payload = size - header - path_bytes;
    if (payload > 184) return false;
    switch ((bytes[0] >> 2) & 15) {
        case 4: { // Ed25519 advertisement plus at least an app flags byte
            if (payload < 101 || payload > 132) return false;
            const uint8_t flags = bytes[header + path_bytes + 100];
            const size_t app_min = 1 + ((flags & 0x10) ? 8 : 0) +
                ((flags & 0x20) ? 2 : 0) + ((flags & 0x40) ? 2 : 0);
            return payload - 100 >= app_min;
        }
        case 5: // channel text: channel hash, 2-byte MAC, whole AES blocks
        case 6: return payload >= 19 && (payload - 3) % 16 == 0;
        default: return false;
    }
}
inline bool time_due(uint32_t now, uint32_t due) { return (int32_t)(now - due) >= 0; }
inline bool valid_location(double lat, double lon) {
    return isfinite(lat) && isfinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}
// Group text authenticates possession of the channel key, not a sender identity.
// Split the upstream "name: text" convention only for display; never derive a
// routing identity from this name. from_node remains 0 for all group RX.
inline void split_group_text(const char *input, char *sender, size_t sender_size,
                             char *text, size_t text_size) {
    if (!input || !sender || !sender_size || !text || !text_size) return;
    const size_t len = strnlen(input, GROUP_TEXT_BYTES + 1);
    const char *delimiter = nullptr;
    for (size_t i = 0; i + 1 < len && i <= MAX_NAME; ++i)
        if (input[i] == ':' && input[i + 1] == ' ') { delimiter = input + i; break; }
    const char *body = input;
    if (delimiter && delimiter != input) {
        size_t n = (size_t)(delimiter - input);
        if (n >= sender_size) n = sender_size - 1;
        memcpy(sender, input, n); sender[n] = 0;
        body = delimiter + 2;
    } else {
        const char fallback[] = "Channel peer";
        size_t n = sizeof(fallback) - 1;
        if (n >= sender_size) n = sender_size - 1;
        memcpy(sender, fallback, n); sender[n] = 0;
    }
    size_t n = strnlen(body, GROUP_TEXT_BYTES + 1);
    if (n >= text_size) n = text_size - 1;
    memcpy(text, body, n); text[n] = 0;
}
}
