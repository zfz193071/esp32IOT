#pragma once
#include "FreeRTOS.h"
using QueueHandle_t = void*;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t wait);
void vQueueDelete(QueueHandle_t queue);
