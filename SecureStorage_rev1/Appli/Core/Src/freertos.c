/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS support stubs — NO_SYS bare-metal mode.
  *
  * FreeRTOS source files are still compiled (they are required to provide
  * vApplicationGetIdleTaskMemory / vApplicationGetTimerTaskMemory symbols
  * expected by port.c and tasks.c).  No tasks are created and the scheduler
  * is never started; these stubs exist only to satisfy the linker.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* USER CODE BEGIN Application */

/**
 * @brief  Stack overflow hook — required when configCHECK_FOR_STACK_OVERFLOW > 0.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    printf("\r\n*** STACK OVERFLOW: task=\"%s\" ***\r\n", pcTaskName);
    BSP_LED_Off(LED_GREEN);
    BSP_LED_Off(LED_YELLOW);
    for (;;)
    {
        BSP_LED_Toggle(LED_RED);
        HAL_Delay(25);
    }
}

/*
 * configSUPPORT_STATIC_ALLOCATION = 1 requires these symbols.
 * They are never called (scheduler is never started), but must be defined.
 */
static StaticTask_t xIdleTaskTCB;
static StackType_t  xIdleTaskStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t  **ppxIdleTaskStackBuffer,
                                   uint32_t      *puxIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = xIdleTaskStack;
    *puxIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

static StaticTask_t xTimerTaskTCB;
static StackType_t  xTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t  **ppxTimerTaskStackBuffer,
                                    uint32_t      *puxTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = xTimerTaskStack;
    *puxTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}

/* BenchmarkTask / HeartbeatTask — not used in NO_SYS mode.
 * All logic runs in bare-metal main(). These stubs keep the linker happy
 * if anything references the symbols. */
void BenchmarkTask(void *argument)
{
    (void)argument;
    vTaskDelete(NULL);
}

void HeartbeatTask(void *argument)
{
    (void)argument;
    vTaskDelete(NULL);
}

/* USER CODE END Application */
