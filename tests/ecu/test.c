#include <stdint.h>
#include <stdio.h>

#include "main.h"

UART_HandleTypeDef huart1;
TIM_HandleTypeDef htim2;

static uint32_t pwm[4];
static int fails = 0;

void record(uint32_t channel, uint32_t value) { pwm[channel] = value; }
void HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *data, uint16_t size) {
    (void)huart;
    (void)data;
    (void)size;
}
void HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t channel) {
    (void)htim;
    (void)channel;
}

void breath_ecu_on_byte(uint8_t byte);

static void feed(const uint8_t *data, int len) {
    for (int i = 0; i < len; i++)
        breath_ecu_on_byte(data[i]);
}

static void expect_rgb(const char *name, int r, int g, int b) {
    const uint32_t want_r = (uint32_t)r * 1000u / 255u;
    const uint32_t want_g = (uint32_t)g * 1000u / 255u;
    const uint32_t want_b = (uint32_t)b * 1000u / 255u;
    if (pwm[1] == want_r && pwm[2] == want_g && pwm[3] == want_b) {
        printf("ok   %-26s pwm=(%3u,%3u,%3u)\n", name, pwm[1], pwm[2], pwm[3]);
    } else {
        printf("FAIL %-26s pwm=(%3u,%3u,%3u) want=(%3u,%3u,%3u)\n", name, pwm[1], pwm[2], pwm[3],
               want_r, want_g, want_b);
        fails++;
    }
}

int main(void) {
    const uint8_t red[] = {0xA5, 0xFF, 0x00, 0x00, 0x90, 0x1D};
    const uint8_t green[] = {0xA5, 0x00, 0xFF, 0x00, 0x0C, 0xD1};
    const uint8_t blue[] = {0xA5, 0x00, 0x00, 0xFF, 0x03, 0xCC};
    const uint8_t white[] = {0xA5, 0xFF, 0xFF, 0xFF, 0x9F, 0x00};
    const uint8_t off[] = {0xA5, 0x00, 0x00, 0x00, 0xF3, 0xD2};
    const uint8_t bad_crc[] = {0xA5, 0x12, 0x34, 0x56, 0x00, 0x00};
    const uint8_t junk[] = {0x00, 0x13, 0x7F};
    const uint8_t false_header_then_red[] = {0xA5, 0xA5, 0xFF, 0x00, 0x00, 0x90, 0x1D};

    feed(red, (int)sizeof(red));
    expect_rgb("red", 255, 0, 0);
    feed(green, (int)sizeof(green));
    expect_rgb("green", 0, 255, 0);
    feed(blue, (int)sizeof(blue));
    expect_rgb("blue", 0, 0, 255);
    feed(white, (int)sizeof(white));
    expect_rgb("white", 255, 255, 255);
    feed(off, (int)sizeof(off));
    expect_rgb("off", 0, 0, 0);

    feed(junk, (int)sizeof(junk));
    feed(red, (int)sizeof(red));
    expect_rgb("junk then red", 255, 0, 0);

    feed(bad_crc, (int)sizeof(bad_crc));
    expect_rgb("bad crc keeps old color", 255, 0, 0);
    feed(green, (int)sizeof(green));
    expect_rgb("green after bad crc", 0, 255, 0);

    feed(false_header_then_red, (int)sizeof(false_header_then_red));
    expect_rgb("false header then red", 255, 0, 0);

    if (fails == 0)
        printf("ALL PASS\n");
    else
        printf("%d FAILED\n", fails);
    return fails != 0;
}
