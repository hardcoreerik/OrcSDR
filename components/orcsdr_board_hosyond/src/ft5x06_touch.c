#include "ft5x06_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define FT5X06_REG_STATUS 0x02

static i2c_master_dev_handle_t s_dev;
static volatile ft5x06_point_t s_pt;

bool ft5x06_get(ft5x06_point_t *out)
{
    /* Seqlock: the writer makes seq odd while it updates x/y, so a copy is only
     * accepted when seq was even and unchanged around it. */
    uint32_t a, b;
    do {
        a = s_pt.seq;
        __sync_synchronize();
        out->x = s_pt.x;
        out->y = s_pt.y;
        out->pressed = s_pt.pressed;
        __sync_synchronize();
        b = s_pt.seq;
    } while ((a & 1) || a != b);
    out->seq = a >> 1;
    return out->pressed;
}

static void ft5x06_task(void *arg)
{
    const char *tag = (const char *)arg;
    int last_event = 3;
    int empty = FT5X06_RELEASE_POLLS;
    int fails = 0;
    for (;;) {
        uint8_t reg = FT5X06_REG_STATUS;
        uint8_t d[5] = {0};
        if (i2c_master_transmit_receive(s_dev, &reg, 1, d, sizeof(d), 20) != ESP_OK) {
            if (++fails == 100) {
                ESP_LOGW(tag, "touch: 100 consecutive I2C read failures");
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        fails = 0;
        const int n = d[0] & 0x0f;
        const int ev = (d[1] >> 6) & 3;
        if (n > 0 && n <= 10 && ev != 3) {
            int x = (FT5X06_X_RES - 1) - (((d[1] & 0x0f) << 8) | d[2]);
            int y = (FT5X06_Y_RES - 1) - (((d[3] & 0x0f) << 8) | d[4]);
            if (x < 0) {
                x = 0;
            }
            if (y < 0) {
                y = 0;
            }
            if (ev == 1) {
                s_pt.pressed = false;
            } else {
                s_pt.seq++; /* odd: update in progress */
                __sync_synchronize();
                s_pt.x = x;
                s_pt.y = y;
                s_pt.pressed = true;
                __sync_synchronize();
                s_pt.seq++; /* even: consistent again */
            }
            if (last_event == 3 && ev != 1) {
                ESP_LOGI(tag, "TOUCH press x=%d y=%d points=%d", x, y, n);
            }
            last_event = ev;
            empty = 0;
        } else if (++empty >= FT5X06_RELEASE_POLLS) {
            s_pt.pressed = false;
            if (last_event != 3) {
                ESP_LOGI(tag, "TOUCH release");
            }
            last_event = 3;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t ft5x06_start(i2c_master_bus_handle_t bus, const char *tag)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT5X06_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }
    return xTaskCreate(ft5x06_task, "ft5x06", 3072, (void *)tag, 5, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
