#include "obd2.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"

static const char *TAG = "obd2";

#define OBD2_RX_QUEUE_DEPTH 16

typedef struct {
    twai_frame_t frame;
    uint8_t buffer[TWAI_FRAME_MAX_LEN];
} obd2_rx_item_t;

static twai_node_handle_t s_twai_node;
static QueueHandle_t s_rx_queue;

static bool IRAM_ATTR obd2_on_rx_done(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx)
{
    (void)edata;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (queue == NULL) {
        return false;
    }

    obd2_rx_item_t item = { 0 };
    item.frame.buffer = item.buffer;
    item.frame.buffer_len = sizeof(item.buffer);

    if (twai_node_receive_from_isr(handle, &item.frame) == ESP_OK) {
        if (xQueueSendFromISR(queue, &item, &higher_priority_task_woken) != pdTRUE) {
            ESP_EARLY_LOGW(TAG, "RX queue full, dropping frame");
        }
    }

    return (higher_priority_task_woken == pdTRUE);
}

static esp_err_t send_obd_request(uint8_t pid)
{
    uint8_t payload[8] = {
        0x02, 0x01, pid, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    twai_frame_t frame = {
        .header = {
            .id = 0x7DF,
            .dlc = twaifd_len2dlc(sizeof(payload)),
            .ide = 0,
            .rtr = 0,
            .fdf = 0,
            .brs = 0,
            .esi = 0,
        },
        .buffer = payload,
        .buffer_len = sizeof(payload),
    };

    ESP_RETURN_ON_ERROR(twai_node_transmit(s_twai_node, &frame, 100), TAG, "twai_node_transmit failed");
    return twai_node_transmit_wait_all_done(s_twai_node, 100);
}

static bool parse_obd_response(const twai_frame_t *msg, uint8_t pid, obd2_vehicle_data_t *out)
{
    if (msg->header.ide || msg->header.rtr) {
        return false;
    }

    if (msg->header.id < 0x7E8 || msg->header.id > 0x7EF) {
        return false;
    }

    if (msg->header.dlc < 4) {
        return false;
    }

    if (msg->buffer[1] != 0x41 || msg->buffer[2] != pid) {
        return false;
    }

    switch (pid) {
    case 0x0D:  // Vehicle speed
        out->speed_kph = msg->buffer[3];
        out->speed_valid = true;
        return true;
    case 0x2F:  // Fuel level input
        out->fuel_percent = (uint8_t)((msg->buffer[3] * 100U) / 255U);
        out->fuel_valid = true;
        return true;
    case 0x0C:  // Engine RPM
        if (msg->header.dlc < 5) {
            return false;
        }
        out->rpm = (uint16_t)((((uint16_t)msg->buffer[3] << 8) | msg->buffer[4]) / 4U);
        out->rpm_valid = true;
        return true;
    default:
        return false;
    }
}

static esp_err_t query_pid(uint8_t pid, obd2_vehicle_data_t *out)
{
    ESP_RETURN_ON_ERROR(send_obd_request(pid), TAG, "failed to send PID 0x%02X", pid);

    const int64_t deadline = esp_timer_get_time() + (int64_t)CONFIG_OBD2_RESPONSE_TIMEOUT_MS * 1000LL;
    while (esp_timer_get_time() < deadline) {
        obd2_rx_item_t item;
        if (xQueueReceive(s_rx_queue, &item, pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }
        if (parse_obd_response(&item.frame, pid, out)) {
            return ESP_OK;
        }
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t obd2_init(void)
{
    s_rx_queue = xQueueCreate(OBD2_RX_QUEUE_DEPTH, sizeof(obd2_rx_item_t));
    ESP_RETURN_ON_FALSE(s_rx_queue != NULL, ESP_ERR_NO_MEM, TAG, "failed to create RX queue");

    twai_onchip_node_config_t node_config = {
        .io_cfg = {
            .tx = CONFIG_OBD2_CAN_TX_GPIO,
            .rx = CONFIG_OBD2_CAN_RX_GPIO,
            .quanta_clk_out = GPIO_NUM_NC,
            .bus_off_indicator = GPIO_NUM_NC,
        },
        .bit_timing = {
            .bitrate = CONFIG_OBD2_CAN_BITRATE,
        },
        .tx_queue_depth = 8,
        .fail_retry_cnt = 3,
    };

    ESP_RETURN_ON_ERROR(twai_new_node_onchip(&node_config, &s_twai_node), TAG, "twai_new_node_onchip failed");

    twai_mask_filter_config_t f_config = {
        .id = 0,
        .mask = 0,
        .is_ext = false,
    };
    ESP_RETURN_ON_ERROR(twai_node_config_mask_filter(s_twai_node, 0, &f_config), TAG, "twai_node_config_mask_filter failed");

    twai_event_callbacks_t callbacks = {
        .on_rx_done = obd2_on_rx_done,
    };
    ESP_RETURN_ON_ERROR(twai_node_register_event_callbacks(s_twai_node, &callbacks, s_rx_queue), TAG,
                        "twai_node_register_event_callbacks failed");
    ESP_RETURN_ON_ERROR(twai_node_enable(s_twai_node), TAG, "twai_node_enable failed");

    ESP_LOGI(TAG, "TWAI started on TX=%d RX=%d", CONFIG_OBD2_CAN_TX_GPIO, CONFIG_OBD2_CAN_RX_GPIO);
    return ESP_OK;
}

esp_err_t obd2_read_vehicle_data(obd2_vehicle_data_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    esp_err_t err = query_pid(0x0D, out);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        return err;
    }

    err = query_pid(0x2F, out);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        return err;
    }

    err = query_pid(0x0C, out);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        return err;
    }

    out->last_update_ms = esp_timer_get_time() / 1000LL;
    return ESP_OK;
}
