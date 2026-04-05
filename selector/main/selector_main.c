/**
 * Heltec WiFi LoRa 32 V4 — Dual-Boot Selector
 * 
 * Lives in the factory partition. On boot:
 *   - Shows "MeshCore" and "Meshtastic" on OLED
 *   - PRG button (GPIO0) cycles selection
 *   - Auto-boots last used firmware after TIMEOUT_MS
 *   - Hold PRG at power-on to force selector (override auto-boot)
 * 
 * Pin reference (V3/V4 shared):
 *   OLED SDA=17, SCL=18, RST=21
 *   PRG button = GPIO0 (active LOW, internal pullup)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "selector";

// --- Pin definitions (Heltec V3/V4) ---
#define PRG_BUTTON_GPIO     0       // Active LOW, internal pullup
#define OLED_SDA_GPIO       17
#define OLED_SCL_GPIO       18
#define OLED_RST_GPIO       21
#define VEXT_ENABLE_GPIO    36      // Active LOW — enables VEXT rail (OLED power) on V4

// --- Timing ---
#define TIMEOUT_MS          5000    // Auto-boot after 5 seconds
#define DEBOUNCE_MS         50
#define POLL_MS             50

// --- NVS keys ---
#define NVS_NAMESPACE       "selector"
#define NVS_LAST_BOOT_KEY   "last_boot"  // 0 = MeshCore (ota_0), 1 = Meshtastic (ota_1)

// --- SSD1306 I2C OLED (128x64) ---
#define OLED_I2C_ADDR       0x3C
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         400000

// SSD1306 commands
#define OLED_CMD_DISPLAY_OFF        0xAE
#define OLED_CMD_DISPLAY_ON         0xAF
#define OLED_CMD_SET_CONTRAST       0x81
#define OLED_CMD_ENTIRE_DISPLAY_ON  0xA4
#define OLED_CMD_NORMAL_DISPLAY     0xA6
#define OLED_CMD_SET_PAGE_ADDR      0xB0
#define OLED_CMD_SET_COL_LOW        0x00
#define OLED_CMD_SET_COL_HIGH       0x10
#define OLED_CMD_MEMORY_MODE        0x20
#define OLED_CMD_SET_COL_ADDR       0x21
#define OLED_CMD_SET_PAGE_ADDR_MODE 0x22
#define OLED_CMD_SET_DISPLAY_CLOCK  0xD5
#define OLED_CMD_SET_MULTIPLEX      0xA8
#define OLED_CMD_SET_DISPLAY_OFFSET 0xD3
#define OLED_CMD_SET_START_LINE     0x40
#define OLED_CMD_CHARGE_PUMP        0x8D
#define OLED_CMD_SET_SEG_REMAP      0xA1
#define OLED_CMD_SET_COM_SCAN_DEC   0xC8
#define OLED_CMD_SET_COM_PINS       0xDA
#define OLED_CMD_SET_PRECHARGE      0xD9
#define OLED_CMD_SET_VCOM_DETECT    0xDB
#define OLED_CMD_DEACTIVATE_SCROLL  0x2E

// 5x7 font (ASCII 32-127)
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 space
    {0x00,0x00,0x5F,0x00,0x00}, // 33 !
    {0x00,0x07,0x00,0x07,0x00}, // 34 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 35 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 36 $
    {0x23,0x13,0x08,0x64,0x62}, // 37 %
    {0x36,0x49,0x55,0x22,0x50}, // 38 &
    {0x00,0x05,0x03,0x00,0x00}, // 39 '
    {0x00,0x1C,0x22,0x41,0x00}, // 40 (
    {0x00,0x41,0x22,0x1C,0x00}, // 41 )
    {0x14,0x08,0x3E,0x08,0x14}, // 42 *
    {0x08,0x08,0x3E,0x08,0x08}, // 43 +
    {0x00,0x50,0x30,0x00,0x00}, // 44 ,
    {0x08,0x08,0x08,0x08,0x08}, // 45 -
    {0x00,0x60,0x60,0x00,0x00}, // 46 .
    {0x20,0x10,0x08,0x04,0x02}, // 47 /
    {0x3E,0x51,0x49,0x45,0x3E}, // 48 0
    {0x00,0x42,0x7F,0x40,0x00}, // 49 1
    {0x42,0x61,0x51,0x49,0x46}, // 50 2
    {0x21,0x41,0x45,0x4B,0x31}, // 51 3
    {0x18,0x14,0x12,0x7F,0x10}, // 52 4
    {0x27,0x45,0x45,0x45,0x39}, // 53 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 54 6
    {0x01,0x71,0x09,0x05,0x03}, // 55 7
    {0x36,0x49,0x49,0x49,0x36}, // 56 8
    {0x06,0x49,0x49,0x29,0x1E}, // 57 9
    {0x00,0x36,0x36,0x00,0x00}, // 58 :
    {0x00,0x56,0x36,0x00,0x00}, // 59 ;
    {0x08,0x14,0x22,0x41,0x00}, // 60 <
    {0x14,0x14,0x14,0x14,0x14}, // 61 =
    {0x00,0x41,0x22,0x14,0x08}, // 62 >
    {0x02,0x01,0x51,0x09,0x06}, // 63 ?
    {0x32,0x49,0x79,0x41,0x3E}, // 64 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 65 A
    {0x7F,0x49,0x49,0x49,0x36}, // 66 B
    {0x3E,0x41,0x41,0x41,0x22}, // 67 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 68 D
    {0x7F,0x49,0x49,0x49,0x41}, // 69 E
    {0x7F,0x09,0x09,0x09,0x01}, // 70 F
    {0x3E,0x41,0x49,0x49,0x7A}, // 71 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 72 H
    {0x00,0x41,0x7F,0x41,0x00}, // 73 I
    {0x20,0x40,0x41,0x3F,0x01}, // 74 J
    {0x7F,0x08,0x14,0x22,0x41}, // 75 K
    {0x7F,0x40,0x40,0x40,0x40}, // 76 L
    {0x7F,0x02,0x0C,0x02,0x7F}, // 77 M
    {0x7F,0x04,0x08,0x10,0x7F}, // 78 N
    {0x3E,0x41,0x41,0x41,0x3E}, // 79 O
    {0x7F,0x09,0x09,0x09,0x06}, // 80 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 81 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 82 R
    {0x46,0x49,0x49,0x49,0x31}, // 83 S
    {0x01,0x01,0x7F,0x01,0x01}, // 84 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 85 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 86 V
    {0x3F,0x40,0x38,0x40,0x3F}, // 87 W
    {0x63,0x14,0x08,0x14,0x63}, // 88 X
    {0x07,0x08,0x70,0x08,0x07}, // 89 Y
    {0x61,0x51,0x49,0x45,0x43}, // 90 Z
    {0x00,0x7F,0x41,0x41,0x00}, // 91 [
    {0x02,0x04,0x08,0x10,0x20}, // 92 backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 93 ]
    {0x04,0x02,0x01,0x02,0x04}, // 94 ^
    {0x40,0x40,0x40,0x40,0x40}, // 95 _
    {0x00,0x01,0x02,0x04,0x00}, // 96 `
    {0x20,0x54,0x54,0x54,0x78}, // 97 a
    {0x7F,0x48,0x44,0x44,0x38}, // 98 b
    {0x38,0x44,0x44,0x44,0x20}, // 99 c
    {0x38,0x44,0x44,0x48,0x7F}, // 100 d
    {0x38,0x54,0x54,0x54,0x18}, // 101 e
    {0x08,0x7E,0x09,0x01,0x02}, // 102 f
    {0x0C,0x52,0x52,0x52,0x3E}, // 103 g
    {0x7F,0x08,0x04,0x04,0x78}, // 104 h
    {0x00,0x44,0x7D,0x40,0x00}, // 105 i
    {0x20,0x40,0x44,0x3D,0x00}, // 106 j
    {0x7F,0x10,0x28,0x44,0x00}, // 107 k
    {0x00,0x41,0x7F,0x40,0x00}, // 108 l
    {0x7C,0x04,0x18,0x04,0x78}, // 109 m
    {0x7C,0x08,0x04,0x04,0x78}, // 110 n
    {0x38,0x44,0x44,0x44,0x38}, // 111 o
    {0x7C,0x14,0x14,0x14,0x08}, // 112 p
    {0x08,0x14,0x14,0x18,0x7C}, // 113 q
    {0x7C,0x08,0x04,0x04,0x08}, // 114 r
    {0x48,0x54,0x54,0x54,0x20}, // 115 s
    {0x04,0x3F,0x44,0x40,0x20}, // 116 t
    {0x3C,0x40,0x40,0x20,0x7C}, // 117 u
    {0x1C,0x20,0x40,0x20,0x1C}, // 118 v
    {0x3C,0x40,0x30,0x40,0x3C}, // 119 w
    {0x44,0x28,0x10,0x28,0x44}, // 120 x
    {0x0C,0x50,0x50,0x50,0x3C}, // 121 y
    {0x44,0x64,0x54,0x4C,0x44}, // 122 z
    {0x00,0x08,0x36,0x41,0x00}, // 123 {
    {0x00,0x00,0x7F,0x00,0x00}, // 124 |
    {0x00,0x41,0x36,0x08,0x00}, // 125 }
    {0x10,0x08,0x08,0x10,0x08}, // 126 ~
};

static uint8_t oled_buf[128 * 8]; // 128 cols x 8 pages (64 rows)

// --- I2C / OLED helpers ---

static esp_err_t oled_send_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(h, buf, 2, true);
    i2c_master_stop(h);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, h, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(h);
    return ret;
}

static void oled_reset(void) {
    gpio_set_direction(OLED_RST_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(OLED_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(OLED_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void oled_init(void) {
    // Init I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);

    oled_reset();

    // SSD1306 init sequence
    uint8_t init_cmds[] = {
        OLED_CMD_DISPLAY_OFF,
        OLED_CMD_SET_DISPLAY_CLOCK, 0x80,
        OLED_CMD_SET_MULTIPLEX, 0x3F,
        OLED_CMD_SET_DISPLAY_OFFSET, 0x00,
        OLED_CMD_SET_START_LINE | 0x00,
        OLED_CMD_CHARGE_PUMP, 0x14,
        OLED_CMD_MEMORY_MODE, 0x00,
        OLED_CMD_SET_SEG_REMAP,
        OLED_CMD_SET_COM_SCAN_DEC,
        OLED_CMD_SET_COM_PINS, 0x12,
        OLED_CMD_SET_CONTRAST, 0xCF,
        OLED_CMD_SET_PRECHARGE, 0xF1,
        OLED_CMD_SET_VCOM_DETECT, 0x40,
        OLED_CMD_ENTIRE_DISPLAY_ON,
        OLED_CMD_NORMAL_DISPLAY,
        OLED_CMD_DEACTIVATE_SCROLL,
        OLED_CMD_DISPLAY_ON,
    };
    for (int i = 0; i < sizeof(init_cmds); i++) {
        oled_send_cmd(init_cmds[i]);
    }
}

static void oled_flush(void) {
    oled_send_cmd(OLED_CMD_SET_COL_ADDR);
    oled_send_cmd(0x00);
    oled_send_cmd(0x7F);
    oled_send_cmd(OLED_CMD_SET_PAGE_ADDR_MODE);
    oled_send_cmd(0x00);
    oled_send_cmd(0x07);

    // Send all pages
    for (int page = 0; page < 8; page++) {
        uint8_t header[2] = {0x00, (uint8_t)(0xB0 | page)};
        i2c_cmd_handle_t h = i2c_cmd_link_create();
        i2c_master_start(h);
        i2c_master_write_byte(h, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(h, header, 2, true);
        i2c_master_stop(h);
        i2c_master_cmd_begin(I2C_PORT, h, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(h);

        // Send 128 data bytes for this page
        uint8_t data[129];
        data[0] = 0x40; // data mode
        memcpy(&data[1], &oled_buf[page * 128], 128);
        h = i2c_cmd_link_create();
        i2c_master_start(h);
        i2c_master_write_byte(h, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(h, data, 129, true);
        i2c_master_stop(h);
        i2c_master_cmd_begin(I2C_PORT, h, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(h);
    }
}

static void oled_clear(void) {
    memset(oled_buf, 0, sizeof(oled_buf));
}

// Draw a single character at (col, page) — col 0-127, page 0-7
static void oled_draw_char(int col, int page, char c, bool invert) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font5x7[c - 32];
    for (int i = 0; i < 5; i++) {
        int x = col + i;
        if (x >= 128) break;
        uint8_t byte = glyph[i];
        if (invert) byte = ~byte;
        oled_buf[page * 128 + x] = byte;
    }
    // 1px gap
    int gap = col + 5;
    if (gap < 128) oled_buf[page * 128 + gap] = invert ? 0xFF : 0x00;
}

// Draw a string at (col, page)
static void oled_draw_str(int col, int page, const char *s, bool invert) {
    while (*s) {
        oled_draw_char(col, page, *s++, invert);
        col += 6;
        if (col >= 128) break;
    }
}

// Draw a filled rectangle in buffer coords
static void oled_fill_rect(int x, int page, int w, int h_pages, bool fill) {
    for (int p = page; p < page + h_pages && p < 8; p++) {
        for (int i = x; i < x + w && i < 128; i++) {
            oled_buf[p * 128 + i] = fill ? 0xFF : 0x00;
        }
    }
}

// --- NVS helpers ---

static uint8_t nvs_get_last_boot(void) {
    nvs_handle_t h;
    uint8_t val = 0; // default: MeshCore
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_LAST_BOOT_KEY, &val);
        nvs_close(h);
    }
    return val;
}

static void nvs_set_last_boot(uint8_t val) {
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_LAST_BOOT_KEY, val);
        nvs_commit(h);
        nvs_close(h);
    }
}

// --- Screen rendering ---

static void render_screen(int selected, int countdown_sec) {
    oled_clear();

    // Title
    oled_draw_str(10, 0, "  Dual-Boot Selector", false);

    // Divider line (page 1)
    oled_fill_rect(0, 1, 128, 1, true);

    // MeshCore button (left half, pages 2-4)
    bool mc_sel = (selected == 0);
    oled_fill_rect(0, 2, 62, 3, mc_sel);
    oled_draw_str(4, 3, "MeshCore", mc_sel);

    // Divider between buttons
    oled_fill_rect(63, 2, 2, 3, true);

    // Meshtastic button (right half, pages 2-4)
    bool mt_sel = (selected == 1);
    oled_fill_rect(65, 2, 63, 3, mt_sel);
    oled_draw_str(67, 3, "Mesht.", mt_sel);

    // Bottom line
    oled_fill_rect(0, 5, 128, 1, true);

    // Countdown
    char buf[32];
    snprintf(buf, sizeof(buf), "PRG=cycle  Boot in %ds", countdown_sec);
    oled_draw_str(0, 6, buf, false);

    // Version
    oled_draw_str(20, 7, "v1.0 - Heltec V4", false);

    oled_flush();
}

static void render_booting(const char *name) {
    oled_clear();
    oled_fill_rect(0, 0, 128, 8, false);
    oled_draw_str(20, 2, "Booting...", false);
    oled_draw_str(0, 4, name, false);
    oled_flush();
}

// --- OTA boot ---

static void boot_partition(int selected) {
    const char *label = (selected == 0) ? "ota_0" : "ota_1";
    const char *name  = (selected == 0) ? "MeshCore" : "Meshtastic";

    ESP_LOGI(TAG, "Booting %s (%s)", name, label);
    render_booting(name);

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_ANY,
        label
    );

    if (!part) {
        ESP_LOGE(TAG, "Partition '%s' not found!", label);
        oled_clear();
        oled_draw_str(0, 3, "ERROR: partition", false);
        oled_draw_str(0, 4, "not found!", false);
        oled_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        return;
    }

    esp_err_t err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        oled_clear();
        oled_draw_str(0, 3, "ERROR: set boot", false);
        char ebuf[32];
        snprintf(ebuf, sizeof(ebuf), "%s", esp_err_to_name(err));
        oled_draw_str(0, 4, ebuf, false);
        oled_flush();
        vTaskDelay(pdMS_TO_TICKS(3000));
        return;
    }

    nvs_set_last_boot(selected);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

// --- Main ---

void app_main(void) {
    ESP_LOGI(TAG, "Heltec V4 Dual-Boot Selector starting");

    // Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Init PRG button
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << PRG_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    // Enable VEXT power rail (active LOW) — powers OLED on Heltec V4
    gpio_config_t vext_cfg = {
        .pin_bit_mask = (1ULL << VEXT_ENABLE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&vext_cfg);
    gpio_set_level(VEXT_ENABLE_GPIO, 0); // LOW = enable VEXT
    vTaskDelay(pdMS_TO_TICKS(50));       // Let rail stabilize before OLED init

    // Init OLED
    oled_init();
    oled_clear();
    oled_draw_str(10, 3, "Initializing...", false);
    oled_flush();
    vTaskDelay(pdMS_TO_TICKS(300));

    // Load last selection
    int selected = nvs_get_last_boot();
    if (selected > 1) selected = 0;

    // Check if PRG held at boot — if not, check if we should auto-boot
    bool prg_held = (gpio_get_level(PRG_BUTTON_GPIO) == 0);
    ESP_LOGI(TAG, "PRG at boot: %s, last_boot=%d", prg_held ? "HELD" : "released", selected);

    // If PRG not held AND a valid firmware exists, show countdown then auto-boot
    // If PRG held, show selector indefinitely until released + pressed again
    int countdown_ms = TIMEOUT_MS;
    int last_btn_state = 1;
    bool auto_boot = !prg_held;

    render_screen(selected, countdown_ms / 1000);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));

        int btn = gpio_get_level(PRG_BUTTON_GPIO);

        // Detect falling edge (press)
        if (btn == 0 && last_btn_state == 1) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            if (gpio_get_level(PRG_BUTTON_GPIO) == 0) {
                // Button pressed — cycle selection, reset countdown
                selected = 1 - selected;
                auto_boot = true; // re-enable auto-boot after manual press
                countdown_ms = TIMEOUT_MS;
                ESP_LOGI(TAG, "Selected: %s", selected == 0 ? "MeshCore" : "Meshtastic");
            }
        }
        last_btn_state = btn;

        if (auto_boot) {
            countdown_ms -= POLL_MS;
            if (countdown_ms <= 0) {
                boot_partition(selected);
                return;
            }
        }

        render_screen(selected, (countdown_ms + 999) / 1000);
    }
}
