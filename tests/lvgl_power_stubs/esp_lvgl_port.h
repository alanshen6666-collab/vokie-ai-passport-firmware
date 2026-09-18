#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { bool paused; } lv_timer_t;
typedef struct { int unused; } lv_display_t;
typedef struct { int unused; } lv_obj_t;
typedef struct { int unused; } lvgl_port_cfg_t;
#define ESP_LVGL_PORT_INIT_CONFIG() {0}
typedef struct {
    void *panel_handle, *io_handle;
    uint32_t buffer_size;
    bool double_buffer;
    int hres, vres;
    struct { bool swap_xy, mirror_x, mirror_y; } rotation;
    struct { bool buff_dma, swap_bytes; } flags;
} lvgl_port_display_cfg_t;
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *);
lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *);
bool lvgl_port_lock(int);
void lvgl_port_unlock(void);
esp_err_t lvgl_port_stop(void);
esp_err_t lvgl_port_resume(void);
void lv_timer_enable(bool);
void lv_timer_pause(lv_timer_t *);
void lv_timer_resume(lv_timer_t *);
bool lv_timer_get_paused(lv_timer_t *);
lv_timer_t *lv_anim_get_timer(void);
lv_timer_t *lv_display_get_refr_timer(lv_display_t *);
void lv_tick_inc(uint32_t);
lv_obj_t *lv_display_get_screen_active(lv_display_t *);
void lv_obj_invalidate(lv_obj_t *);
