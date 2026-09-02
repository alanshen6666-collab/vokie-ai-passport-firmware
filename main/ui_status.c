#include "ui_status.h"

#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "vokie_symbol_asset.h"
#include <stdio.h>
#include <string.h>

#define UI_BG 0x0B1018
#define UI_PANEL 0x151D28
#define UI_WHITE 0xF6F8FC
#define UI_INK 0x202A38
#define UI_MUTED 0x7F8B9E
#define UI_BLUE 0x67D8FF
#define UI_GREEN 0x78E0A4
#define UI_ORANGE 0xFFB454
#define UI_RED 0xFF7180
#define UI_CONTENT_X 0
#define UI_CONTENT_W BSP_LCD_W
#define UI_HEADER_X 30
#define UI_HEADER_W 180
#define UI_HEADER_Y 20
#define UI_LOGO_SIZE 64
#define UI_LOGO_X ((BSP_LCD_W - UI_LOGO_SIZE) / 2)
#define UI_LOGO_Y 70
#define UI_STATE_Y 178
#define UI_DETAIL_Y 211
#define UI_BRIGHTNESS_FULL 65
#define UI_BRIGHTNESS_PROCESSING 38
#define UI_BRIGHTNESS_DIM 18
#define UI_HINT_TIMEOUT_US (3LL * 1000LL * 1000LL)
#define UI_HINT_FADE_IN_MS 160
#define UI_HINT_FADE_OUT_MS 220
#define UI_DIM_TIMEOUT_US (3LL * 1000LL * 1000LL)
#define UI_OFF_TIMEOUT_US (20LL * 1000LL * 1000LL)

static lv_obj_t *s_screen;
static lv_obj_t *s_state;
static lv_obj_t *s_detail;
static lv_obj_t *s_dot;
static lv_obj_t *s_button_hints;
static volatile int64_t s_last_touch_us;
static volatile bool s_active;
static volatile bool s_dirty;
static volatile bool s_hints_requested;
static volatile int64_t s_hints_deadline_us;
static uint8_t s_backlight_level;
static bool s_hints_visible;
static ui_status_state_t s_state_value;
static char s_message[96];
static TaskHandle_t s_task;

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color,
                     int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                       uint32_t color, int width)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    if (width > 0) {
        lv_obj_set_width(obj, width);
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    }
    return obj;
}

static void set_backlight(uint8_t percent)
{
    if (percent == s_backlight_level) return;
    s_backlight_level = percent;
    bsp_display_backlight(percent);
}

static uint8_t active_brightness(void)
{
    return s_state_value == UI_STATUS_PROCESSING ? UI_BRIGHTNESS_PROCESSING
                                                  : UI_BRIGHTNESS_FULL;
}

static const char *state_name(ui_status_state_t state)
{
    switch (state) {
    case UI_STATUS_RECORDING: return "LISTENING";
    case UI_STATUS_PROCESSING: return "THINKING";
    case UI_STATUS_SUCCESS: return "SENT";
    case UI_STATUS_ERROR: return "ERROR";
    case UI_STATUS_DISCONNECTED: return "OFFLINE";
    case UI_STATUS_STARTING: return "STARTING";
    default: return "READY";
    }
}

static uint32_t state_color(ui_status_state_t state)
{
    switch (state) {
    case UI_STATUS_RECORDING: return UI_ORANGE;
    case UI_STATUS_PROCESSING: return UI_BLUE;
    case UI_STATUS_SUCCESS: return UI_GREEN;
    case UI_STATUS_ERROR: return UI_RED;
    case UI_STATUS_DISCONNECTED: return UI_MUTED;
    default: return UI_BLUE;
    }
}

static const lv_image_dsc_t s_vokie_symbol = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_NATIVE,
        .w = UI_LOGO_SIZE,
        .h = UI_LOGO_SIZE,
        .stride = UI_LOGO_SIZE * 2,
    },
    .data_size = sizeof(s_vokie_symbol_data),
    .data = s_vokie_symbol_data,
};

static void draw_vokie(void)
{
    // This is a 64x64 RGB565 raster of the official vokie-symbol.svg path.
    // It is placed directly on the screen: no tile, border, or distortion.
    lv_obj_t *mark = lv_image_create(s_screen);
    lv_image_set_src(mark, &s_vokie_symbol);
    lv_obj_set_pos(mark, UI_LOGO_X, UI_LOGO_Y);
}

static void draw_button_rail(void)
{
    // The hints float above the centered layout and never push it sideways.
    // They are hidden at rest and appear briefly after any physical-key action.
    const int rail_w = 34;
    const int x = BSP_LCD_W - 12 - rail_w;
    const int ys[3] = {46, 148, 250};
    const uint32_t colors[3] = {UI_ORANGE, UI_BLUE, UI_GREEN};
    const char *actions[3] = {"VOICE", "SEND", "UNDO"};
    s_button_hints = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_button_hints);
    lv_obj_set_pos(s_button_hints, 0, 0);
    lv_obj_set_size(s_button_hints, BSP_LCD_W, BSP_LCD_H);
    lv_obj_remove_flag(s_button_hints, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 3; ++i) {
        lv_obj_t *card = box(s_button_hints, x, ys[i], rail_w, 22, UI_PANEL, 7);
        lv_obj_t *text = label(card, actions[i], &lv_font_montserrat_10,
                               colors[i], rail_w);
        lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_add_flag(s_button_hints, LV_OBJ_FLAG_HIDDEN);
}

static void set_button_hints_visible(bool visible)
{
    if (!s_button_hints || visible == s_hints_visible) return;
    s_hints_visible = visible;
    lv_anim_delete(s_button_hints, NULL);
    if (visible) {
        lv_obj_remove_flag(s_button_hints, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_button_hints);
        lv_obj_set_style_opa(s_button_hints, LV_OPA_TRANSP, 0);
        lv_obj_fade_in(s_button_hints, UI_HINT_FADE_IN_MS, 0);
    } else {
        lv_obj_fade_out(s_button_hints, UI_HINT_FADE_OUT_MS, 0);
    }
}

static void render_state(void)
{
    if (!s_screen || !bsp_lvgl_lock(500)) return;
    const uint32_t color = state_color(s_state_value);
    lv_label_set_text(s_state, state_name(s_state_value));
    lv_obj_set_style_text_color(s_state, lv_color_hex(color), 0);
    lv_label_set_text(s_detail, s_message[0] ? s_message : "Vokie is ready");
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(color), 0);
    set_button_hints_visible(s_hints_requested || s_active);
    lv_obj_set_style_opa(s_screen, LV_OPA_COVER, 0);
    set_backlight(active_brightness());
    bsp_lvgl_unlock();
}

static void status_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_dirty) {
            s_dirty = false;
            render_state();
        }
        const int64_t now_us = esp_timer_get_time();
        if (s_screen && !s_active && s_hints_requested &&
            now_us >= s_hints_deadline_us && bsp_lvgl_lock(100)) {
            s_hints_requested = false;
            set_button_hints_visible(false);
            bsp_lvgl_unlock();
        }
        if (s_screen && s_active) {
            set_backlight(active_brightness());
        } else if (s_screen && now_us - s_last_touch_us >= UI_OFF_TIMEOUT_US) {
            set_backlight(0);
        } else if (s_screen && now_us - s_last_touch_us >= UI_DIM_TIMEOUT_US) {
            set_backlight(UI_BRIGHTNESS_DIM);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void ui_status_init(void)
{
    if (s_screen || !bsp_lvgl_lock(1000)) return;
    s_last_touch_us = esp_timer_get_time();
    s_state_value = UI_STATUS_STARTING;
    s_message[0] = 0;
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    // The main composition stays centered when the hints are hidden. The
    // header uses its own narrow column so the status dot does not pull the
    // title off-center.
    lv_obj_t *caption = label(s_screen, "AI VOICE PASSPORT", &lv_font_montserrat_14,
                              UI_MUTED, UI_HEADER_W);
    lv_obj_set_pos(caption, UI_HEADER_X, UI_HEADER_Y);
    s_dot = box(s_screen, UI_HEADER_X + UI_HEADER_W - 7, UI_HEADER_Y + 5,
                7, 7, UI_BLUE, 4);
    draw_vokie();
    draw_button_rail();

    s_state = label(s_screen, "", &lv_font_montserrat_20, UI_BLUE, UI_CONTENT_W);
    lv_obj_set_pos(s_state, UI_CONTENT_X, UI_STATE_Y);
    s_detail = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_detail, lv_color_hex(UI_MUTED), 0);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_detail, UI_CONTENT_W);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_detail, UI_CONTENT_X, UI_DETAIL_Y);

    lv_screen_load(s_screen);
    bsp_lvgl_unlock();
    render_state();
    xTaskCreate(status_task, "ui_status", 2048, NULL, 2, &s_task);
}

void ui_status_touch(void)
{
    const int64_t now_us = esp_timer_get_time();
    s_last_touch_us = now_us;
    s_hints_deadline_us = now_us + UI_HINT_TIMEOUT_US;
    s_hints_requested = true;
    s_dirty = true;
    if (s_screen) set_backlight(active_brightness());
}

void ui_status_set_state(ui_status_state_t state, const char *message)
{
    s_state_value = state;
    s_active = state == UI_STATUS_RECORDING || state == UI_STATUS_PROCESSING;
    if (message) {
        strncpy(s_message, message, sizeof(s_message) - 1);
        s_message[sizeof(s_message) - 1] = 0;
    } else {
        s_message[0] = 0;
    }
    // State changes refresh the display and idle timer, but do not open the
    // button hints on their own. Only a physical-key interaction should make
    // the controls appear when the screen is otherwise at rest.
    s_last_touch_us = esp_timer_get_time();
    s_dirty = true;
    if (s_screen) set_backlight(active_brightness());
}
