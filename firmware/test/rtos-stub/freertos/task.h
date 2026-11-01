#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void*;
BaseType_t xTaskCreate(void (*task)(void*), const char* name, std::uint32_t stack,
                       void* context, UBaseType_t priority, TaskHandle_t* handle);
void vTaskDelete(TaskHandle_t task);
