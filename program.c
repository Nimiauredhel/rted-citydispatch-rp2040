// C libs
#include <stdlib.h>
#include <stdint.h>
// RP-2040 libs
#include "pico/stdio_usb.h"
#include "pico/printf.h"
#include "pico/stdlib.h"
// FreeRTOS libs
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"

void CentralDispatcherTask(void *param);
void DepartmentDispatcherTask(void *param);
void DepartmentHandlerTask(void *param);
void LoggerTask(void *param);
void EventGeneratorTask(void *param);
void ExampleTask1(void *param);
void ExampleTask2(void *param);

int main(void)
{
    stdio_init_all();

    TaskHandle_t gExampleTask1 = NULL;
    TaskHandle_t gExampleTask2 = NULL;

    uint32_t status1 = xTaskCreate(
            ExampleTask1,
            "Example Task 1",
            1024,
            NULL,
            tskIDLE_PRIORITY,
            &gExampleTask1);
    uint32_t status2 = xTaskCreate(
            ExampleTask2,
            "Example Task 2",
            1024,
            NULL,
            tskIDLE_PRIORITY,
            &gExampleTask2);

    vTaskStartScheduler();

    for(;;)
    {
        // should never get here
    }
}

void CentralDispatcherTask(void *param)
{
    for(;;)
    {
    }
}
void DepartmentDispatcherTask(void *param)
{
    for(;;)
    {
    }
}
void DepartmentHandlerTask(void *param)
{
    // handle event here

    vTaskDelete(NULL);
}
void LoggerTask(void *param)
{
    for(;;)
    {
    }
}
void EventGeneratorTask(void *param)
{
    for(;;)
    {
    }
}

void ExampleTask1(void *param)
{
    for(;;)
    {
        printf("Hello from example task 1!\n");
        vTaskDelay(1000);
    }
}

void ExampleTask2(void *param)
{
    for(;;)
    {
        printf("Hello from example task 2!\n");
        vTaskDelay(2000);
    }
}
