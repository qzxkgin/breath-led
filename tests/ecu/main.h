#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

typedef struct { void *Instance; } UART_HandleTypeDef;
typedef struct { void *Instance; } TIM_HandleTypeDef;

extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim2;

#define USART1 ((void *)0)
#define TIM_CHANNEL_1 1u
#define TIM_CHANNEL_2 2u
#define TIM_CHANNEL_3 3u

void record(uint32_t channel, uint32_t value);
#define __HAL_TIM_SET_COMPARE(h, ch, val) record(ch, val)

void HAL_UART_Receive_IT(UART_HandleTypeDef *huart, uint8_t *data, uint16_t size);
void HAL_TIM_PWM_Start(TIM_HandleTypeDef *htim, uint32_t channel);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);

#endif
