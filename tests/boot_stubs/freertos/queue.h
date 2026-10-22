#pragma once
#include "FreeRTOS.h"
typedef struct test_queue *QueueHandle_t;
BaseType_t xQueueReceive(QueueHandle_t queue, void *event, TickType_t wait);
BaseType_t xQueueReset(QueueHandle_t queue);
