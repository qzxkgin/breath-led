#include <stdint.h>

#include "main.h"

#define PACKET_SIZE 6
#define HEADER_BYTE 0xA5

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim2;

static uint8_t rx_byte;
static uint8_t rx_buf[PACKET_SIZE];
static uint8_t rx_idx;
static uint8_t led_rgb[3];

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    uint16_t i;
    uint8_t bit;
    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x8000u)
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void led_apply(void) {
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, (uint32_t)led_rgb[0] * 1000u / 255u);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, (uint32_t)led_rgb[1] * 1000u / 255u);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, (uint32_t)led_rgb[2] * 1000u / 255u);
}

static void resync(void) {
    do {
        uint8_t i;
        for (i = 1u; i < rx_idx; i++)
            rx_buf[i - 1u] = rx_buf[i];
        rx_idx--;
    } while (rx_idx > 0u && rx_buf[0] != HEADER_BYTE);
}

void breath_ecu_on_byte(uint8_t byte) {
    if (rx_idx == 0u && byte != HEADER_BYTE)
        return;
    rx_buf[rx_idx++] = byte;
    if (rx_idx < PACKET_SIZE)
        return;
    if (crc16_ccitt(rx_buf, PACKET_SIZE - 2) != (uint16_t)(rx_buf[4] | ((uint16_t)rx_buf[5] << 8))) {
        resync();
        return;
    }
    rx_idx = 0;
    led_rgb[0] = rx_buf[1];
    led_rgb[1] = rx_buf[2];
    led_rgb[2] = rx_buf[3];
    led_apply();
}

void breath_ecu_init(void) {
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    led_apply();
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        breath_ecu_on_byte(rx_byte);
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}
