#pragma once
#include <stdbool.h>

// Initialize HA REST API polling task
void mqtt_app_init(void);

// Simple on/off light
void mqtt_publish_command(const char *entity_id, const char *payload);

// Dimmable + color-temp light (brightness 0-255, color_temp_kelvin 0=unchanged)
void ha_set_light_with_params(const char *entity_id, bool on, int brightness, int color_temp_kelvin);

// Cover control (position 0=closed, 100=open)
void ha_cover_open(const char *entity_id);
void ha_cover_close(const char *entity_id);
void ha_cover_set_position(const char *entity_id, int position);
