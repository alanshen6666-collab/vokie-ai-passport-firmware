// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "bsp_lvgl";

static lv_display_t *s_disp;
static bool s_paused;
static bool s_anim_was_paused;
static int64_t s_paused_at_us;

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    const lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&pc) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init 失败");
        return NULL;
    }

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        // ⚠ C3 无 PSRAM,DMA 只能用内部 RAM(总共约 150KB)。
        // 20 行单缓冲 ≈ 9.6KB;若改成 40 行双缓冲(≈37.5KB)会把 I2S 等外设的
        // DMA 描述符挤到 NO_MEM。刷新略慢但稳。
        .buffer_size   = (uint32_t)BSP_LCD_W * 20,
        .double_buffer = false,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        // 旋转/镜像必须在这里配:esp_lvgl_port 注册显示时会重新下发 MADCTL,
        // 覆盖 bsp_display.c 里 esp_lcd_panel_mirror() 的设置。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes:LVGL 输出小端 RGB565,ST7789 走 SPI 要大端 → 需交换高低字节。
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    s_disp = lvgl_port_add_disp(&dc);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp 失败"); return NULL; }

    ESP_LOGI(TAG, "LVGL 就绪");
    return s_disp;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }

esp_err_t bsp_lvgl_set_paused(bool paused) {
    if (!s_disp) return ESP_ERR_INVALID_STATE;
    if (paused == s_paused) return ESP_OK;
    if (paused) {
        esp_err_t err = lvgl_port_stop();
        // LVGL 9.5 returns 1 ms from lv_timer_handler() when globally disabled.
        // Keep the handler enabled so paused timers yield LV_NO_TIMER_READY
        // and the port task can use its 500 ms maximum wait instead.
        lv_timer_enable(true);
        if (err != ESP_OK) return err;
        lv_timer_pause(lv_display_get_refr_timer(s_disp));
        s_anim_was_paused = lv_timer_get_paused(lv_anim_get_timer());
        lv_timer_pause(lv_anim_get_timer());
        s_paused_at_us = esp_timer_get_time();
    } else {
        esp_err_t err = lvgl_port_resume();
        if (err != ESP_OK) return err;
        lv_tick_inc((uint32_t)((esp_timer_get_time() - s_paused_at_us) / 1000));
        lv_timer_resume(lv_display_get_refr_timer(s_disp));
        if (!s_anim_was_paused) lv_timer_resume(lv_anim_get_timer());
        lv_obj_invalidate(lv_display_get_screen_active(s_disp));
    }
    s_paused = paused;
    return ESP_OK;
}
