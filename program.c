// C libs
#include <stdlib.h>
#include <stdint.h>
// RP-2040 libs
#include "pico/stdlib.h"
#include "pico/printf.h"
#include "pico/time.h"
#include "pico/stdio_usb.h"
#include "hardware/gpio.h"
#include "hardware/regs/rosc.h"
#include "hardware/regs/addressmap.h"
// FreeRTOS libs
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"

#define NUM_DEPARTMENTS (4)

#define TASK_STACK_SIZE (configMINIMAL_STACK_SIZE)

#define EVENT_GENERATOR_PRIORITY (50)
#define LOGGER_PRIORITY (100)
#define CENTRAL_DISPATCHER_PRIORITY (150)
#define DEPARTMENT_DISPATCHER_PRIORITY (200)
#define DEPARTMENT_HANDLER_PRIORITY (250)

#define INITIAL_SLEEP (1500)
#define EVENT_GENERATOR_SLEEP_MAX (4000)
#define EVENT_GENERATOR_SLEEP_MIN (1000)
#define LOGGER_SLEEP (1000)

// *** Types ***
typedef enum DepartmentCode_t
{
    POLICE = 0,
    FIRE = 1,
    MEDICAL = 2,
    COVID = 3
} DepartmentCode_t;
typedef struct CityDepartment
{
    DepartmentCode_t code;
    BaseType_t status;
    QueueHandle_t jobQueue;
    uint8_t freeHandlers;
} CityDepartment_t;
typedef struct CityData
{
    BaseType_t dispatcherStatus;
    QueueHandle_t incomingQueue;
    CityDepartment_t departments[NUM_DEPARTMENTS];
} CityData_t;
typedef struct CityEvent
{
    TickType_t ticks;
    DepartmentCode_t code;
    char *description;
} CityEvent_t;
typedef struct CityEventTemplate
{
    TickType_t maxTicks;
    TickType_t minTicks;
    DepartmentCode_t code;
    char *description;
} CityEventTemplate_t;

// *** Global Constants
const char departmentNames[NUM_DEPARTMENTS][10] = {"Police\0", "Fire\0", "Medical\0", "Covid-19\0"};
// Events will be generated, randomly or otherwise,
// from this pool of event templates
CityEventTemplate_t eventTemplates[8] =
{
    {2000, 500, POLICE, "Minor Police Event"},
    {5000, 2500, POLICE, "Major Police Event"},
    {2000, 500, FIRE, "Minor Fire Event"},
    {5000, 2500, FIRE, "Major Fire Event"},
    {2000, 500, MEDICAL, "Minor Medical Event"},
    {5000, 2500, MEDICAL, "Major Medical Event"},
    {2024, 2019, COVID, "Covid-19 Isolated Event"},
    {10000, 5000, COVID, "Covid-19 Outbreak Event"},
};

// *** Function Declarations ***
CityData_t* InitializeCityData(void);
void InitializeCityTasks(CityData_t *cityData);
void CentralDispatcherTask(void *param);
void DepartmentDispatcherTask(void *param);
void DepartmentHandlerTask(void *param);
void LoggerTask(void *param);
void EventGeneratorTask(void *param);
uint32_t RandomNumber(void);

// *** Function Implementations ***
int main(void)
{
    // hardware specific initialization
    stdio_init_all();
    gpio_init(28);
    gpio_set_dir(28, true);
    gpio_put(28, false);
    sleep_ms(1000);

    // application data initialization
    CityData_t *cityData = InitializeCityData();

    // initial tasks creation
    InitializeCityTasks(cityData);

    xTaskCreate( LoggerTask, "Logger", TASK_STACK_SIZE,
            NULL, LOGGER_PRIORITY, NULL);
    xTaskCreate( EventGeneratorTask, "EventGenerator", TASK_STACK_SIZE,
            &(cityData->incomingQueue), EVENT_GENERATOR_PRIORITY, NULL);

    // begin execution
    vTaskStartScheduler();

    for(;;)
    {
        // should never get here
    }
}

CityData_t* InitializeCityData(void)
{
    static const uint8_t departmentHandlerCounts[NUM_DEPARTMENTS] = {4, 4, 4, 4};
    static const uint16_t departmentQueueLengths[NUM_DEPARTMENTS] = {200, 200, 200, 200};

    CityData_t *cityData = pvPortMalloc(sizeof(CityData_t));
    cityData->incomingQueue = xQueueCreate(200, sizeof(CityEvent_t));

    for (int i = 0; i < NUM_DEPARTMENTS; i++)
    {
        cityData->departments[i].code = i;
        cityData->departments[i].jobQueue = xQueueCreate(departmentQueueLengths[i], sizeof(CityEvent_t));
        cityData->departments[i].freeHandlers = departmentHandlerCounts[i];
    }

    return cityData;
}

void InitializeCityTasks(CityData_t *cityData)
{
    cityData->dispatcherStatus = xTaskCreate(
            CentralDispatcherTask,
            "CentralDispatcher", TASK_STACK_SIZE,
            cityData, CENTRAL_DISPATCHER_PRIORITY, NULL);

    for (int i = 0; i < NUM_DEPARTMENTS; i++)
    {
            cityData->departments[i].status = xTaskCreate(
            DepartmentDispatcherTask,
            departmentNames[cityData->departments[i].code], TASK_STACK_SIZE,
            &(cityData->departments[i]), DEPARTMENT_DISPATCHER_PRIORITY, NULL);
    }
}

void CentralDispatcherTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    CityData_t *cityData = (CityData_t *)param;
    CityEvent_t handledEvent;
    printf("Central Dispatcher Starting...\n");

    for(;;)
    {
        printf("Central Dispatcher Waiting...\n");
        if (xQueueReceive(cityData->incomingQueue, &(handledEvent), portMAX_DELAY))
        {
            printf("Central Dispatcher Routing Event \"%s\" to %s Department.\n", handledEvent.description, departmentNames[handledEvent.code]);
            xQueueSend(cityData->departments[handledEvent.code].jobQueue, &(handledEvent), portMAX_DELAY);
        }
    }
}
void DepartmentDispatcherTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    CityDepartment_t *departmentData = (CityDepartment_t *)param;
    CityEvent_t *handledEvent = pvPortMalloc(sizeof(CityEvent_t));
    bool hasEvent;
    printf("%s Department Dispatcher Starting...\n", departmentNames[departmentData->code]);

    for(;;)
    {
        printf("%s Department Dispatcher Waiting...\n", departmentNames[departmentData->code]);

        if (xQueueReceive(departmentData->jobQueue, handledEvent, portMAX_DELAY))
        {
            printf("%s Department Dispatcher Assigning Event \"%s\"\n", departmentNames[departmentData->code], handledEvent->description);
            CityEvent_t *passedEvent = pvPortMalloc(sizeof(CityEvent_t));
            *passedEvent = *handledEvent;
            xTaskCreate(DepartmentHandlerTask, handledEvent->description, TASK_STACK_SIZE,
            passedEvent, DEPARTMENT_HANDLER_PRIORITY, NULL);
        }
    }
}
void DepartmentHandlerTask(void *param)
{
    // handle event here
    CityEvent_t *event = (CityEvent_t *)param;
    printf("%s Department Handler Started Handling Event \"%s\"\n", departmentNames[event->code], event->description);
    vTaskDelay(event->ticks);
    printf("%s Department Handler Finished Handling Event \"%s\"\n", departmentNames[event->code], event->description);
    // this is a temporary task,
    // so it must delete itself when finished
    vPortFree(event);
    vTaskDelete(NULL);
}
void LoggerTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    printf("Logger Starting...\n");

    for(;;)
    {
        vTaskDelay(LOGGER_SLEEP);
    }
}
void EventGeneratorTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    printf("Event Generator Starting...\n");

    QueueHandle_t *incomingQueue = (QueueHandle_t *)param;
    uint32_t nextSleep;
    uint8_t nextEventTemplate;
    CityEvent_t *nextEvent = pvPortMalloc(sizeof(CityEvent_t));

    for(;;)
    {
        nextEventTemplate = RandomNumber()%8;
        nextEvent->code = eventTemplates[nextEventTemplate].code;
        nextEvent->description = eventTemplates[nextEventTemplate].description;
        nextEvent->ticks = eventTemplates[nextEventTemplate].minTicks
            + (RandomNumber()%(eventTemplates[nextEventTemplate].maxTicks-eventTemplates[nextEventTemplate].minTicks));
        gpio_put(28, true);
        printf("\nEmitting Event: %s\nEstimated handling time: %u ticks\n\n",
                nextEvent->description, nextEvent->ticks);
        xQueueSend(*incomingQueue, nextEvent, portMAX_DELAY);
        gpio_put(28, false);

        nextSleep = EVENT_GENERATOR_SLEEP_MIN
            + (RandomNumber()%(EVENT_GENERATOR_SLEEP_MAX-EVENT_GENERATOR_SLEEP_MIN));
        vTaskDelay(nextSleep);
    }
}
uint32_t RandomNumber(void)
{
    int k = 0;
    int random=0;
    volatile uint32_t *rnd_reg=(uint32_t *)(ROSC_BASE + ROSC_RANDOMBIT_OFFSET);
    
    for(k=0;k<32;k++)
    {
        random = random << 1;
        random=random + (0x00000001 & (*rnd_reg));
    }

    return random;
}
