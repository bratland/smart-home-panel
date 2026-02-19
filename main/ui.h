#pragma once

#include "lvgl.h"
#include <stdbool.h>

void ui_init(lv_display_t *display);

void ui_update_light_state(const char *entity_id, bool state);
void ui_update_light_params(const char *entity_id, int brightness, int color_temp_kelvin);
void ui_update_cover_state(const char *entity_id, int position);

// Kept for API compatibility
void ui_update_temperature(float temp);
