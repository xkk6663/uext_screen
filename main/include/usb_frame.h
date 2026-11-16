/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t received;
    uint32_t total;
} frame_info_t;

typedef struct {
    size_t data_buffer_len;                   /**< Max data length supported by this frame buffer*/
    size_t data_len;                          /**< Data length of currently store frame */
    uint8_t *data;                            /**< Frame data */
    frame_info_t info;
    volatile bool in_use;                     /**< Owned by rx task or draw task, not sitting in any queue */
} frame_t;

esp_err_t frame_allocate(int nb_of_fb, size_t fb_size);

void frame_reset(frame_t *frame);

esp_err_t frame_return_empty(frame_t *frame);

esp_err_t frame_send_filled(frame_t *frame);

esp_err_t frame_add_data(frame_t *frame, const uint8_t *data, size_t data_len);

frame_t *frame_get_empty(void);

frame_t *frame_get_filled(void);

/**
 * @brief Rebuild the frame pool: drain both queues and hand every idle buffer back to the empty queue.
 *
 * Guarantees that a stalled pipe cannot permanently starve the pool, which used to freeze
 * the panel until a reboot. Buffers still owned by a task keep in_use = true and are skipped.
 *
 * @param why short reason string, printed in the log
 */
void frame_pool_recover(const char *why);

#ifdef __cplusplus
}
#endif
