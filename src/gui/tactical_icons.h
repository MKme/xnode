#pragma once
#include "config.h"
#include "gui/icon.h"

// Reuse the bundled functional artwork with category colors and full-cell taps.
void tactical_icon_bind(icon_t *icon, const char *name, lv_event_cb_t callback);
void tactical_icon_refresh(icon_t *icon);
