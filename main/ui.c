/*
 * Smart Home Panel UI - LVGL-based touchscreen interface
 *
 * Layout (480x800 portrait, card bottom-aligned):
 * - Background image
 * - Arbetsrum card:
 *     Guldlampan  (on/off)
 *     Videolampor (on/off + brightness + color temp)
 *     Iris        (on/off + brightness)
 *     Solskydd    (position slider 0-100)
 */

#include "ui.h"
#include "fonts.h"
#include "img_bg.h"
#include "esp_log.h"
#include "mqtt_client_app.h"
#include <string.h>

static const char *TAG = "ui";

// ---- Guldlampan ----
static lv_obj_t *sw_guld = NULL;

// ---- Videolampor ----
static lv_obj_t *sw_video      = NULL;
static lv_obj_t *slider_bright = NULL;
static lv_obj_t *slider_ct     = NULL;
static lv_obj_t *label_bright  = NULL;
static lv_obj_t *label_ct      = NULL;

// ---- Iris ----
static lv_obj_t *sw_iris             = NULL;
static lv_obj_t *slider_iris_bright  = NULL;
static lv_obj_t *label_iris_bright   = NULL;

// ---- Solskydd ----
static lv_obj_t *slider_solskydd = NULL;
static lv_obj_t *label_solskydd  = NULL;

// Guard against feedback loops when polling updates sliders
static bool s_updating_from_poll = false;

// ---- Event callbacks ----

static void guld_switch_cb(lv_event_t *e)
{
    bool is_on = lv_obj_has_state(sw_guld, LV_STATE_CHECKED);
    ESP_LOGI(TAG, "guldlampan -> %s", is_on ? "ON" : "OFF");
    mqtt_publish_command("light.guldlampan", is_on ? "ON" : "OFF");
}

static void video_switch_cb(lv_event_t *e)
{
    bool is_on = lv_obj_has_state(sw_video, LV_STATE_CHECKED);
    int bright = lv_slider_get_value(slider_bright);
    int ct_raw = lv_slider_get_value(slider_ct);
    int ct_k   = 2900 + (ct_raw * (7000 - 2900)) / 100;
    ha_set_light_with_params("light.videolampor", is_on, bright, ct_k);
}

static void bright_slider_cb(lv_event_t *e)
{
    if (s_updating_from_poll) return;
    int val = lv_slider_get_value(slider_bright);
    lv_label_set_text_fmt(label_bright, "%d%%", (val * 100) / 255);
    bool is_on = lv_obj_has_state(sw_video, LV_STATE_CHECKED);
    int ct_raw = lv_slider_get_value(slider_ct);
    int ct_k   = 2900 + (ct_raw * (7000 - 2900)) / 100;
    ha_set_light_with_params("light.videolampor", is_on, val, ct_k);
}

static void ct_slider_cb(lv_event_t *e)
{
    if (s_updating_from_poll) return;
    int ct_raw = lv_slider_get_value(slider_ct);
    int ct_k   = 2900 + (ct_raw * (7000 - 2900)) / 100;
    lv_label_set_text_fmt(label_ct, "%dK", ct_k);
    bool is_on = lv_obj_has_state(sw_video, LV_STATE_CHECKED);
    int bright = lv_slider_get_value(slider_bright);
    ha_set_light_with_params("light.videolampor", is_on, bright, ct_k);
}

static void iris_switch_cb(lv_event_t *e)
{
    bool is_on = lv_obj_has_state(sw_iris, LV_STATE_CHECKED);
    int bright = lv_slider_get_value(slider_iris_bright);
    ha_set_light_with_params("light.iris_golvlampa", is_on, bright, 0);
}

static void iris_bright_slider_cb(lv_event_t *e)
{
    if (s_updating_from_poll) return;
    int val = lv_slider_get_value(slider_iris_bright);
    lv_label_set_text_fmt(label_iris_bright, "%d%%", (val * 100) / 255);
    bool is_on = lv_obj_has_state(sw_iris, LV_STATE_CHECKED);
    ha_set_light_with_params("light.iris_golvlampa", is_on, val, 0);
}

static void solskydd_slider_cb(lv_event_t *e)
{
    if (s_updating_from_poll) return;
    int pos = lv_slider_get_value(slider_solskydd);
    lv_label_set_text_fmt(label_solskydd, "%d%%", pos);
    ha_cover_set_position("cover.persienn_arbetsrum", pos);
}

// ---- Layout helpers ----

static lv_obj_t *make_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(card, 210, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 10, 0);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &font_sv_18, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xCDD6F4), 0);

    return card;
}

static lv_obj_t *make_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *make_switch(lv_obj_t *parent)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 50, 26);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x45475A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x89B4FA), LV_PART_INDICATOR | LV_STATE_CHECKED);
    return sw;
}

static lv_obj_t *make_slider(lv_obj_t *parent, lv_color_t indicator_color)
{
    lv_obj_t *s = lv_slider_create(parent);
    lv_obj_set_width(s, LV_PCT(95));
    lv_obj_set_style_bg_color(s, lv_color_hex(0x45475A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, indicator_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(0xCDD6F4), LV_PART_KNOB);
    return s;
}

static void add_separator(lv_obj_t *parent)
{
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_size(sep, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x45475A), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    return lbl;
}

// ---- Public API ----

void ui_init(lv_display_t *display)
{
    ESP_LOGI(TAG, "Building Smart Home UI");

    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // Background image
    lv_obj_t *bg = lv_image_create(screen);
    lv_image_set_src(bg, &img_bg);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_size(bg, 480, 800);

    // Scrollable column on top of background
    lv_obj_t *col = lv_obj_create(screen);
    lv_obj_set_size(col, 480, 800);
    lv_obj_set_pos(col, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_top(col, 24, 0);
    lv_obj_set_style_pad_bottom(col, 24, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 16, 0);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);

    // ---- Arbetsrum card ----
    lv_obj_t *card = make_card(col, "Arbetsrum");

    // -- Guldlampan --
    {
        lv_obj_t *row = make_row(card);
        make_label(row, "Guldlampan", &font_sv_16, lv_color_hex(0xA6ADC8));
        sw_guld = make_switch(row);
        lv_obj_add_event_cb(sw_guld, guld_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    add_separator(card);

    // -- Videolampor --
    {
        lv_obj_t *row = make_row(card);
        make_label(row, "Videolampor", &font_sv_16, lv_color_hex(0xA6ADC8));
        sw_video = make_switch(row);
        lv_obj_add_event_cb(sw_video, video_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    {
        lv_obj_t *row = make_row(card);
        make_label(row, "Ljusstyrka", &font_sv_16, lv_color_hex(0x6C7086));
        label_bright = lv_label_create(row);
        lv_label_set_text(label_bright, "--%");
        lv_obj_set_style_text_font(label_bright, &font_sv_16, 0);
        lv_obj_set_style_text_color(label_bright, lv_color_hex(0xCDD6F4), 0);
    }
    slider_bright = make_slider(card, lv_color_hex(0x89B4FA));
    lv_slider_set_range(slider_bright, 0, 255);
    lv_slider_set_value(slider_bright, 128, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_bright, bright_slider_cb, LV_EVENT_RELEASED, NULL);

    {
        lv_obj_t *row = make_row(card);
        make_label(row, "Färgtemp", &font_sv_16, lv_color_hex(0x6C7086));
        label_ct = lv_label_create(row);
        lv_label_set_text(label_ct, "--K");
        lv_obj_set_style_text_font(label_ct, &font_sv_16, 0);
        lv_obj_set_style_text_color(label_ct, lv_color_hex(0xCDD6F4), 0);
    }
    slider_ct = make_slider(card, lv_color_hex(0xFABD2F));
    lv_slider_set_range(slider_ct, 0, 100);
    lv_slider_set_value(slider_ct, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_ct, ct_slider_cb, LV_EVENT_RELEASED, NULL);

    add_separator(card);

    // -- Iris --
    {
        lv_obj_t *row = make_row(card);
        make_label(row, "Iris", &font_sv_16, lv_color_hex(0xA6ADC8));
        sw_iris = make_switch(row);
        lv_obj_add_event_cb(sw_iris, iris_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    {
        lv_obj_t *row = make_row(card);
        make_label(row, "Ljusstyrka", &font_sv_16, lv_color_hex(0x6C7086));
        label_iris_bright = lv_label_create(row);
        lv_label_set_text(label_iris_bright, "--%");
        lv_obj_set_style_text_font(label_iris_bright, &font_sv_16, 0);
        lv_obj_set_style_text_color(label_iris_bright, lv_color_hex(0xCDD6F4), 0);
    }
    slider_iris_bright = make_slider(card, lv_color_hex(0x89B4FA));
    lv_slider_set_range(slider_iris_bright, 0, 255);
    lv_slider_set_value(slider_iris_bright, 128, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_iris_bright, iris_bright_slider_cb, LV_EVENT_RELEASED, NULL);

    add_separator(card);

    // -- Solskydd --
    {
        lv_obj_t *row = make_row(card);
        make_label(row, "Solskydd", &font_sv_16, lv_color_hex(0xA6ADC8));
        label_solskydd = lv_label_create(row);
        lv_label_set_text(label_solskydd, "--%");
        lv_obj_set_style_text_font(label_solskydd, &font_sv_16, 0);
        lv_obj_set_style_text_color(label_solskydd, lv_color_hex(0xCDD6F4), 0);
    }
    slider_solskydd = make_slider(card, lv_color_hex(0xA6E3A1));
    lv_slider_set_range(slider_solskydd, 0, 100);
    lv_slider_set_value(slider_solskydd, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_solskydd, solskydd_slider_cb, LV_EVENT_RELEASED, NULL);

    ESP_LOGI(TAG, "UI created");
}

void ui_update_light_state(const char *entity_id, bool state)
{
    lv_obj_t *sw = NULL;
    if      (strcmp(entity_id, "light.guldlampan")   == 0) sw = sw_guld;
    else if (strcmp(entity_id, "light.videolampor")  == 0) sw = sw_video;
    else if (strcmp(entity_id, "light.iris_golvlampa") == 0) sw = sw_iris;
    if (!sw) return;
    if (state) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else       lv_obj_remove_state(sw, LV_STATE_CHECKED);
}

void ui_update_light_params(const char *entity_id, int brightness, int color_temp_kelvin)
{
    s_updating_from_poll = true;

    if (strcmp(entity_id, "light.videolampor") == 0) {
        if (brightness >= 0 && slider_bright && label_bright) {
            lv_slider_set_value(slider_bright, brightness, LV_ANIM_OFF);
            lv_label_set_text_fmt(label_bright, "%d%%", (brightness * 100) / 255);
        }
        if (color_temp_kelvin > 0 && slider_ct && label_ct) {
            int ct_raw = ((color_temp_kelvin - 2900) * 100) / (7000 - 2900);
            if (ct_raw < 0)   ct_raw = 0;
            if (ct_raw > 100) ct_raw = 100;
            lv_slider_set_value(slider_ct, ct_raw, LV_ANIM_OFF);
            lv_label_set_text_fmt(label_ct, "%dK", color_temp_kelvin);
        }
    } else if (strcmp(entity_id, "light.iris_golvlampa") == 0) {
        if (brightness >= 0 && slider_iris_bright && label_iris_bright) {
            lv_slider_set_value(slider_iris_bright, brightness, LV_ANIM_OFF);
            lv_label_set_text_fmt(label_iris_bright, "%d%%", (brightness * 100) / 255);
        }
    }

    s_updating_from_poll = false;
}

void ui_update_cover_state(const char *entity_id, int position)
{
    if (strcmp(entity_id, "cover.persienn_arbetsrum") != 0) return;
    if (position < 0 || !slider_solskydd || !label_solskydd) return;

    s_updating_from_poll = true;
    lv_slider_set_value(slider_solskydd, position, LV_ANIM_OFF);
    lv_label_set_text_fmt(label_solskydd, "%d%%", position);
    s_updating_from_poll = false;
}

void ui_update_temperature(float temp)
{
    (void)temp;
}
