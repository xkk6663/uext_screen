/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb_frame.h"
#if CONFIG_SOC_JPEG_DECODE_SUPPORTED
#include "driver/jpeg_decode.h"
#endif

#define FRAME_POOL_MAX  8

static QueueHandle_t empty_fb_queue = NULL;
static QueueHandle_t filled_fb_queue = NULL;
static const char *TAG = "usb_frame";

static frame_t *s_pool[FRAME_POOL_MAX];
static int s_pool_cnt = 0;
static volatile uint32_t s_dropped_frames = 0;
static volatile uint32_t s_recover_cnt = 0;

esp_err_t frame_allocate(int nb_of_fb, size_t fb_size)
{
    ESP_RETURN_ON_FALSE(nb_of_fb > 0 && nb_of_fb <= FRAME_POOL_MAX, ESP_ERR_INVALID_ARG, TAG,
                        "frame count %d out of range 1..%d", nb_of_fb, FRAME_POOL_MAX);
    // We will be passing the frame buffers by reference
    empty_fb_queue = xQueueCreate(nb_of_fb, sizeof(frame_t *));
    ESP_RETURN_ON_FALSE(empty_fb_queue, ESP_ERR_NO_MEM, TAG, "Not enough memory for empty_fb_queue %d", nb_of_fb);
    filled_fb_queue = xQueueCreate(nb_of_fb, sizeof(frame_t *));
    ESP_RETURN_ON_FALSE(filled_fb_queue, ESP_ERR_NO_MEM, TAG, "Not enough memory for filled_fb_queue %d", nb_of_fb);
    for (int i = 0; i < nb_of_fb; i++) {
        // Allocate the frame buffer
        frame_t *this_fb = malloc(sizeof(frame_t));
        ESP_RETURN_ON_FALSE(this_fb, ESP_ERR_NO_MEM,  TAG, "Not enough memory for frame buffers %d", fb_size);
#if CONFIG_SOC_JPEG_DECODE_SUPPORTED
        size_t malloc_size = 0;
        jpeg_decode_memory_alloc_cfg_t tx_mem_cfg = {
            .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
        };
        uint8_t *this_data = (uint8_t*)jpeg_alloc_decoder_mem(fb_size, &tx_mem_cfg, &malloc_size);
#else
        uint8_t *this_data = (uint8_t *)heap_caps_aligned_alloc(16, fb_size, MALLOC_CAP_SPIRAM);
#endif
        if (!this_data) {
            free(this_fb);
            ESP_LOGE(TAG, "Not enough memory for frame buffers %d", fb_size);
            return ESP_ERR_NO_MEM;
        }

        // Set members to default
        this_fb->data = this_data;
        this_fb->data_buffer_len = fb_size;
        this_fb->data_len = 0;
        this_fb->in_use = false;
        s_pool[s_pool_cnt++] = this_fb;

        // Add the frame to Queue of empty frames
        const BaseType_t result = xQueueSend(empty_fb_queue, &this_fb, 0);
        assert(pdPASS == result);
    }
    return ESP_OK;
}

void frame_reset(frame_t *frame)
{
    assert(frame);
    frame->data_len = 0;
}

esp_err_t frame_return_empty(frame_t *frame)
{
    ESP_RETURN_ON_FALSE(frame, ESP_ERR_INVALID_ARG, TAG, "null frame");
    frame_reset(frame);
    frame->in_use = false;
    BaseType_t result = xQueueSend(empty_fb_queue, &frame, 0);
    if (result != pdPASS) {
        // Pool cannot take it back (should not happen unless counts are inconsistent).
        // Rebuilding the pool is the only way to avoid a permanent leak.
        s_dropped_frames++;
        frame_pool_recover("return_empty");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t frame_send_filled(frame_t *frame)
{
    ESP_RETURN_ON_FALSE(frame, ESP_ERR_INVALID_ARG, TAG, "null frame");
    frame_reset(frame);
    frame->in_use = false;
    if (xQueueSend(filled_fb_queue, &frame, 0) != pdPASS) {
        // Consumer is behind: hand the buffer straight back to the empty queue instead of
        // dropping it. Previously this path leaked the buffer, and after 6 such events the
        // panel froze forever ("Get frame is null" until reboot).
        s_dropped_frames++;
        if (xQueueSend(empty_fb_queue, &frame, 0) != pdPASS) {
            frame_pool_recover("filled_full");
        }
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void frame_pool_recover(const char *why)
{
    frame_t *f = NULL;
    if (!empty_fb_queue || !filled_fb_queue) {
        return;
    }
    // Frames owned by a producer/consumer keep in_use = true, they are handed back later.
    while (xQueueReceive(empty_fb_queue, &f, 0) == pdPASS) {
        f->in_use = false;
    }
    while (xQueueReceive(filled_fb_queue, &f, 0) == pdPASS) {
        f->in_use = false;
    }
    int requeued = 0;
    for (int i = 0; i < s_pool_cnt; i++) {
        if (s_pool[i]->in_use) {
            continue;
        }
        frame_reset(s_pool[i]);
        s_pool[i]->info.received = 0;
        s_pool[i]->info.total = 0;
        if (xQueueSend(empty_fb_queue, &s_pool[i], 0) == pdPASS) {
            requeued++;
        }
    }
    s_recover_cnt++;
    ESP_LOGW(TAG, "pool recovered (%s): pool=%d requeued=%d dropped=%"PRIu32" recovers=%"PRIu32,
             why ? why : "?", s_pool_cnt, requeued, s_dropped_frames, s_recover_cnt);
}

esp_err_t frame_add_data(frame_t *frame, const uint8_t *data, size_t data_len)
{
    ESP_RETURN_ON_FALSE(frame && data && data_len, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    if (frame->data_buffer_len < frame->data_len + data_len) {
        ESP_LOGD(TAG, "Frame buffer overflow");
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(frame->data + frame->data_len, data, data_len);
    frame->data_len += data_len;
    return ESP_OK;
}

frame_t *frame_get_empty(void)
{
    frame_t *this_fb;
    if (xQueueReceive(empty_fb_queue, &this_fb, 0) == pdPASS) {
        this_fb->in_use = true;
        return this_fb;
    } else {
        return NULL;
    }
}

frame_t *frame_get_filled(void)
{
    frame_t *this_fb;
    if (xQueueReceive(filled_fb_queue, &this_fb, portMAX_DELAY) == pdPASS) {
        return this_fb;
    } else {
        return NULL;
    }
}
