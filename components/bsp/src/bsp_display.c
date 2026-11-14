// components/bsp/src/bsp_display.c
// 移植自 trae_card/components/platform/platform_esp32/src/disp_st7789.c
#include "bsp_display.h"
#include "bsp_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_disp";

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;
static bool                      s_bl_ready;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t       s_backlight_lock;
static bool                      s_backlight_lock_held;
#endif

static const gpio_num_t s_panel_hold_pins[] = {
    BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL,
};

static esp_err_t display_set_safe_levels(void) {
    for (size_t i = 0; i < sizeof(s_panel_hold_pins) / sizeof(s_panel_hold_pins[0]); ++i) {
        const gpio_num_t pin = s_panel_hold_pins[i];
        esp_err_t err = gpio_reset_pin(pin);
        if (err == ESP_OK) err = gpio_set_level(pin, pin == BSP_LCD_CS);
        if (err == ESP_OK) err = gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// ST7789P3 厂商专属初始化序列(porch / power / gamma)。
// 这些是【面板厂给的参考例程 TFT_init() 里的值】,不是 ST7789 通用默认值 ——
// 换面板必须找对应厂商要新的一份,照抄这份大概率显示异常。
//
// 以下四条由 esp_lcd 内置驱动完成,故此处不重复:
//   0x11 SLPOUT / 0x3A COLMOD → esp_lcd_panel_init()
//   0x21 INVON                → esp_lcd_panel_invert_color()
//   0x29 DISPON               → esp_lcd_panel_disp_on_off()
//   0x36 MADCTL               → esp_lcd_panel_mirror()(⚠ 别再手动写 0x36,会被它覆盖)
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t  cmd;
    uint8_t  data[16];
    uint8_t  len;
    uint16_t delay_ms;
} st_init_cmd_t;

static const st_init_cmd_t ST7789P3_CMDS[] = {
    {0xB2, {0x05, 0x05, 0x00, 0x33, 0x33}, 5, 0},   // PORCTRL 帧率 porch
    {0xB7, {0x35}, 1, 0},                            // GCTRL 栅极
    {0xBB, {0x21}, 1, 0},                            // VCOMS
    {0xC0, {0x2C}, 1, 0},                            // LCMCTRL
    {0xC2, {0x01}, 1, 0},                            // VDVVRHEN
    {0xC3, {0x0B}, 1, 0},                            // VRHS
    {0xC4, {0x20}, 1, 0},                            // VDVSET
    {0xC6, {0x0F}, 1, 0},                            // FRCTRL2 60Hz 点反转
    {0xD0, {0xA7, 0xA1}, 2, 0},                      // PWCTRL1
    {0xD0, {0xA4, 0xA1}, 2, 0},                      // PWCTRL1(参考例程重发,覆盖上一条)
    {0xD6, {0xA1}, 1, 0},
    {0xE0, {0xD0, 0x04, 0x08, 0x0A, 0x09, 0x05, 0x2D, 0x43,
            0x49, 0x09, 0x16, 0x15, 0x26, 0x2B}, 14, 0},   // PVGAMCTRL 正伽马
    {0xE1, {0xD0, 0x03, 0x09, 0x0A, 0x0A, 0x06, 0x2E, 0x44,
            0x40, 0x3A, 0x15, 0x15, 0x26, 0x2A}, 14, 10},  // NVGAMCTRL 负伽马
};

static void backlight_init(void) {
    if (BSP_LCD_BL < 0) { ESP_LOGW(TAG, "背光引脚未接 MCU,亮度不可调"); return; }
#if CONFIG_PM_ENABLE
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0,
                                      "backlight", &s_backlight_lock));
#endif
    ledc_timer_config_t t = {
        .speed_mode      = BSP_BL_LEDC_MODE,
        .timer_num       = BSP_BL_LEDC_TIMER,
        .duty_resolution = BSP_BL_LEDC_RES,
        .freq_hz         = BSP_BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_USE_XTAL_CLK,
    };
    esp_err_t e = ledc_timer_config(&t);
    if (e != ESP_OK) { ESP_LOGE(TAG, "ledc_timer_config 失败: %s", esp_err_to_name(e)); return; }

    ledc_channel_config_t ch = {
        .gpio_num   = BSP_LCD_BL,
        .speed_mode = BSP_BL_LEDC_MODE,
        .channel    = BSP_BL_LEDC_CHANNEL,
        .timer_sel  = BSP_BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    e = ledc_channel_config(&ch);
    if (e != ESP_OK) { ESP_LOGE(TAG, "ledc_channel_config 失败: %s", esp_err_to_name(e)); return; }

    s_bl_ready = true;
    ESP_LOGI(TAG, "背光 LEDC 就绪 gpio=%d", BSP_LCD_BL);
}

esp_err_t bsp_display_init(void) {
    if (s_panel) return ESP_OK;

    gpio_deep_sleep_hold_dis();
    esp_err_t e = display_set_safe_levels();
    if (e != ESP_OK) return e;
    for (size_t i = 0; i < sizeof(s_panel_hold_pins) / sizeof(s_panel_hold_pins[0]); ++i) {
        e = gpio_hold_dis(s_panel_hold_pins[i]);
        if (e != ESP_OK) return e;
    }
    spi_bus_config_t bus = {
        .mosi_io_num = BSP_LCD_MOSI,
        .sclk_io_num = BSP_LCD_SCLK,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = BSP_LCD_W * 80 * 2,
    };
    e = spi_bus_initialize(BSP_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "SPI 总线初始化失败 (%s) —— 检查 MOSI=GPIO%d / SCLK=GPIO%d 是否冲突",
                 esp_err_to_name(e), BSP_LCD_MOSI, BSP_LCD_SCLK);
        return e;
    }

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BSP_LCD_CS,
        .dc_gpio_num = BSP_LCD_DC,
        .pclk_hz = BSP_LCD_PCLK_HZ,
        .spi_mode = BSP_LCD_SPI_MODE,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .trans_queue_depth = 10,
    };
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST, &io_cfg, &s_io);
    if (e != ESP_OK) { ESP_LOGE(TAG, "panel_io 创建失败: %s", esp_err_to_name(e)); return e; }

    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = BSP_LCD_RST,          // -1 → SWRESET 软复位
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    e = esp_lcd_new_panel_st7789(s_io, &dev, &s_panel);
    if (e != ESP_OK) { ESP_LOGE(TAG, "面板创建失败: %s", esp_err_to_name(e)); return e; }

    esp_lcd_panel_reset(s_panel);   // rst=-1 时走 SWRESET
    esp_lcd_panel_init(s_panel);    // SLPOUT / COLMOD / RAMCTRL

    for (size_t i = 0; i < sizeof(ST7789P3_CMDS) / sizeof(ST7789P3_CMDS[0]); i++) {
        const st_init_cmd_t *c = &ST7789P3_CMDS[i];
        esp_err_t r = esp_lcd_panel_io_tx_param(s_io, c->cmd, c->data, c->len);
        if (r != ESP_OK) ESP_LOGE(TAG, "厂商初始化命令 0x%02X 失败: %s", c->cmd, esp_err_to_name(r));
        if (c->delay_ms) vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
    }

    esp_lcd_panel_invert_color(s_panel, BSP_LCD_INVERT_COLOR);   // 0x21 / 0x20
    esp_lcd_panel_mirror(s_panel, false, false);                 // 0x36 MADCTL:本板不需镜像(XY 双镜像 = 画面 180°)
    esp_lcd_panel_set_gap(s_panel, 0, 0);
    esp_lcd_panel_disp_on_off(s_panel, true);                    // 0x29 DISPON

    backlight_init();
    ESP_LOGI(TAG, "显示就绪 %dx%d", BSP_LCD_W, BSP_LCD_H);
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_display_panel(void) { return s_panel; }

esp_lcd_panel_io_handle_t bsp_display_io(void) { return s_io; }

esp_err_t bsp_display_prepare_deep_sleep(void) {
    if (!s_panel) return ESP_OK;
    bsp_display_backlight(0);
    esp_err_t err = esp_lcd_panel_disp_on_off(s_panel, false);
    if (err == ESP_OK) err = esp_lcd_panel_disp_sleep(s_panel, true);
    if (s_bl_ready) {
        esp_err_t backlight_err = ledc_stop(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL, 0);
        if (err == ESP_OK) err = backlight_err;
    }
    esp_err_t pins_err = display_set_safe_levels();
    if (pins_err != ESP_OK) return pins_err;
    // CS stays high. Return the unused SPI signals to inputs so GPIO8/GPIO9
    // can take their boot-strapping levels when the chip wakes.
    const gpio_config_t spi_inputs = {
        .pin_bit_mask = (1ULL << BSP_LCD_SCLK) | (1ULL << BSP_LCD_MOSI),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    pins_err = gpio_config(&spi_inputs);
    if (pins_err != ESP_OK) return pins_err;
    for (size_t i = 0; i < sizeof(s_panel_hold_pins) / sizeof(s_panel_hold_pins[0]); ++i) {
        pins_err = gpio_hold_en(s_panel_hold_pins[i]);
        if (pins_err != ESP_OK) return pins_err;
    }
    gpio_deep_sleep_hold_en();
    return err;
}

void bsp_display_backlight(uint8_t percent) {
    if (!s_bl_ready) return;
    if (percent > 100) percent = 100;
#if CONFIG_PM_ENABLE
    // Default LEDC output stops in light sleep. Keep lit screens awake to
    // avoid flicker, and release only after driving the backlight fully off.
    if (percent && !s_backlight_lock_held) {
        ESP_ERROR_CHECK(esp_pm_lock_acquire(s_backlight_lock));
        s_backlight_lock_held = true;
    }
#endif
    uint32_t max_duty = (1u << BSP_BL_LEDC_RES) - 1u;
    uint32_t duty = (max_duty * percent) / 100u;
    ESP_ERROR_CHECK(ledc_set_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL));
#if CONFIG_PM_ENABLE
    if (!percent && s_backlight_lock_held) {
        ESP_ERROR_CHECK(esp_pm_lock_release(s_backlight_lock));
        s_backlight_lock_held = false;
    }
#endif
}
