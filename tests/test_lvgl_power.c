#include "bsp_display.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include <assert.h>
#include <stdio.h>

lv_display_t *bsp_lvgl_init(void);
static lv_display_t display;
static lv_obj_t screen;
static lv_timer_t refresh, animation;
static bool tick_running = true, handler_enabled = true, fail_stop, fail_resume;
static unsigned stops, resumes, invalidations;
static uint32_t ticks;
static int64_t now;
void *bsp_display_panel(void) { return &display; }
void *bsp_display_io(void) { return &display; }
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *c) { (void)c; return ESP_OK; }
lv_display_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *c) { (void)c; return &display; }
bool lvgl_port_lock(int t) { (void)t; return true; }
void lvgl_port_unlock(void) {}
esp_err_t lvgl_port_stop(void) {
    handler_enabled=false;
    if (fail_stop) return ESP_FAIL;
    assert(tick_running); tick_running=false; ++stops; return ESP_OK;
}
esp_err_t lvgl_port_resume(void) {
    handler_enabled=true;
    if (fail_resume) return ESP_FAIL;
    assert(!tick_running); tick_running=true; ++resumes; return ESP_OK;
}
void lv_timer_enable(bool v) { handler_enabled=v; }
void lv_timer_pause(lv_timer_t *t) { t->paused=true; }
void lv_timer_resume(lv_timer_t *t) { t->paused=false; }
bool lv_timer_get_paused(lv_timer_t *t) { return t->paused; }
lv_timer_t *lv_anim_get_timer(void) { return &animation; }
lv_timer_t *lv_display_get_refr_timer(lv_display_t *d) { assert(d==&display); return &refresh; }
void lv_tick_inc(uint32_t t) { ticks+=t; }
lv_obj_t *lv_display_get_screen_active(lv_display_t *d) { (void)d; return &screen; }
void lv_obj_invalidate(lv_obj_t *o) { assert(o==&screen); ++invalidations; }
int64_t esp_timer_get_time(void) { return now; }

int main(void) {
    assert(bsp_lvgl_set_paused(true) == ESP_ERR_INVALID_STATE);
    assert(bsp_lvgl_init() == &display);
    fail_stop=true;
    assert(bsp_lvgl_set_paused(true) == ESP_FAIL);
    assert(handler_enabled && tick_running && !refresh.paused);
    fail_stop=false;
    for (unsigned i=0; i<20; ++i) {
        animation.paused=(i%2 == 0);
        bool was_paused=animation.paused;
        assert(bsp_lvgl_set_paused(true) == ESP_OK);
        assert(refresh.paused && animation.paused && !tick_running);
        assert(handler_enabled); // Disabling this makes LVGL 9.5 return 1 ms.
        assert(bsp_lvgl_set_paused(true) == ESP_OK && stops == i+1);
        now+=30000000;
        fail_resume=true;
        assert(bsp_lvgl_set_paused(false) == ESP_FAIL);
        assert(!tick_running && refresh.paused);
        fail_resume=false;
        assert(bsp_lvgl_set_paused(false) == ESP_OK);
        assert(tick_running && !refresh.paused && handler_enabled);
        assert(animation.paused == was_paused);
        assert(ticks == (i+1)*30000 && invalidations == i+1);
        assert(bsp_lvgl_set_paused(false) == ESP_OK && resumes == i+1);
    }
    puts("LVGL power lifecycle tests: PASS");
}
