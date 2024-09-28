// C libs
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
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
// Application headers
#include "logging.h"

#define NUM_DEPARTMENTS (4)

#define TASK_STACK_SIZE (configMINIMAL_STACK_SIZE)
#define INCOMING_QUEUE_LENGTH (256)
#define DEPARTMENT_QUEUE_LENGTH (256)

#define EVENT_GENERATOR_PRIORITY (50)
#define LOGGER_PRIORITY (100)
#define CENTRAL_DISPATCHER_PRIORITY (150)
#define DEPARTMENT_DISPATCHER_PRIORITY (200)
#define DEPARTMENT_HANDLER_PRIORITY (250)

#define INITIAL_SLEEP (pdMS_TO_TICKS(1500))
#define EVENT_GENERATOR_SLEEP_MAX (pdMS_TO_TICKS(6000))
#define EVENT_GENERATOR_SLEEP_MIN (pdMS_TO_TICKS(2000))
#define LOGGER_SLEEP (pdMS_TO_TICKS(1000))

// *** Types ***
typedef enum DepartmentCode_t
{
    MEDICAL = 0,
    POLICE = 1,
    FIRE = 2,
    COVID = 3
} DepartmentCode_t;
typedef struct CityEvent
{
    TickType_t ticks;
    DepartmentCode_t code;
    char *description;
} CityEvent_t;
typedef struct CityDepartmentHandlerState
{
    bool busy;
    char name[16];
    CityEvent_t currentEvent;
} CityDepartmentHandlerState_t;
typedef struct CityDepartment
{
    DepartmentCode_t code;
    BaseType_t status;
    QueueHandle_t jobQueue;
    uint8_t handlerCount;
    CityDepartmentHandlerState_t *handlerStates;
} CityDepartment_t;
typedef struct CityData
{
    BaseType_t dispatcherStatus;
    QueueHandle_t incomingQueue;
    CityDepartment_t departments[NUM_DEPARTMENTS];
} CityData_t;
typedef struct CityEventTemplate
{
    TickType_t minTicks;
    TickType_t maxTicks;
    DepartmentCode_t code;
    char *description;
} CityEventTemplate_t;

// *** Global Constants
const char departmentNames[NUM_DEPARTMENTS][10] = {"Medical\0", "Police\0", "Fire\0", "Covid-19\0"};
// Events will be generated, randomly or otherwise,
// from this pool of event templates
CityEventTemplate_t eventTemplates[8] =
{
    {pdMS_TO_TICKS(2000),  pdMS_TO_TICKS(5000),  MEDICAL, "Minor Medical"},
    {pdMS_TO_TICKS(6000),  pdMS_TO_TICKS(12000), MEDICAL, "Major Medical"},
    {pdMS_TO_TICKS(2000),  pdMS_TO_TICKS(4000),  POLICE,  "Minor Criminal"},
    {pdMS_TO_TICKS(5000),  pdMS_TO_TICKS(10000), POLICE,  "Major Criminal"},
    {pdMS_TO_TICKS(1000),  pdMS_TO_TICKS(4000),  FIRE,    "Minor Fire"},
    {pdMS_TO_TICKS(6000),  pdMS_TO_TICKS(16000), FIRE,    "Major Fire"},
    {pdMS_TO_TICKS(4000),  pdMS_TO_TICKS(6000),  COVID,   "Covid-19 Isolated"},
    {pdMS_TO_TICKS(10000), pdMS_TO_TICKS(10000), COVID,   "Covid-19 Outbreak"},
};

// *** Function Declarations ***
void InitializeHardware(void);
CityData_t* InitializeCityData(void);
void InitializeCityTasks(CityData_t *cityData);
void InitializeHelperTasks(CityData_t *cityData);
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
    InitializeHardware();
    sleep_ms(1000);

    // application data initialization
    CityData_t *cityData = InitializeCityData();

    // initial tasks creation
    InitializeCityTasks(cityData);
    InitializeHelperTasks(cityData);

    // begin execution
    vTaskStartScheduler();

    for(;;)
    {
        // should never get here
    }
}

void InitializeHardware(void)
{
    stdio_init_all();
    gpio_init(28);
    gpio_set_dir(28, true);
    gpio_put(28, false);
}

CityData_t* InitializeCityData(void)
{
    static const uint8_t departmentInitialResources[NUM_DEPARTMENTS] = {4, 3, 2, 4};

    CityData_t *cityData = pvPortMalloc(sizeof(CityData_t));
    cityData->incomingQueue = xQueueCreate(INCOMING_QUEUE_LENGTH, sizeof(CityEvent_t));

    for (int i = 0; i < NUM_DEPARTMENTS; i++)
    {
        cityData->departments[i].code = i;
        cityData->departments[i].jobQueue = xQueueCreate(DEPARTMENT_QUEUE_LENGTH, sizeof(CityEvent_t));
        cityData->departments[i].handlerCount = departmentInitialResources[i];
        cityData->departments[i].handlerStates = pvPortMalloc(sizeof(CityDepartmentHandlerState_t)
                * departmentInitialResources[i]);
                
        for (int j = 0; j < departmentInitialResources[i]; j++)
        {
            cityData->departments[i].handlerStates[j].busy = false;
            sprintf(cityData->departments[i].handlerStates[j].name, "%s-%u", departmentNames[i], j+1);
        }
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

void InitializeHelperTasks(CityData_t *cityData)
{
    xTaskCreate( LoggerTask, "Logger", TASK_STACK_SIZE,
            NULL, LOGGER_PRIORITY, NULL);
    xTaskCreate( EventGeneratorTask, "EventGenerator", TASK_STACK_SIZE,
            &(cityData->incomingQueue), EVENT_GENERATOR_PRIORITY, NULL);
}

void CentralDispatcherTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    CityData_t *cityData = (CityData_t *)param;
    CityEvent_t handledEvent;
    printf(logFormats[eLOG_DISPATCHER_WAITING]);

    for(;;)
    {
        printf(logFormats[eLOG_DISPATCHER_WAITING]);
        if (xQueueReceive(cityData->incomingQueue, &(handledEvent), portMAX_DELAY))
        {
            printf(logFormats[eLOG_DISPATCHER_ROUTING], handledEvent.description, departmentNames[handledEvent.code]);
            xQueueSend(cityData->departments[handledEvent.code].jobQueue, &(handledEvent), portMAX_DELAY);
        }
    }
}
void DepartmentDispatcherTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    CityDepartment_t *departmentData = (CityDepartment_t *)param;
    CityEvent_t *handledEvent = pvPortMalloc(sizeof(CityEvent_t));
    printf(logFormats[eLOG_DISPATCHER_ROUTING], departmentNames[departmentData->code]);

    for (int i = 0; i < departmentData->handlerCount; i++)
    {
        xTaskCreate(DepartmentHandlerTask, handledEvent->description, TASK_STACK_SIZE,
        &(departmentData->handlerStates[i]), DEPARTMENT_HANDLER_PRIORITY, NULL);
    }

    for(;;)
    {
        printf(logFormats[eLOG_MANAGER_WAITING], departmentNames[departmentData->code]);

        if (xQueueReceive(departmentData->jobQueue, handledEvent, portMAX_DELAY))
        {
            printf(logFormats[eLOG_MANAGER_ASSIGNING_EVENT], departmentNames[departmentData->code], handledEvent->description);
            bool assigned = false;

            do
            {
                for (int i = 0; i < departmentData->handlerCount; i++)
                {
                    if (!(departmentData->handlerStates[i].busy))
                    {
                        departmentData->handlerStates[i].currentEvent = *handledEvent;
                        departmentData->handlerStates[i].busy = true;
                        assigned = true;
                        break;
                    }
                }
            } while(!assigned);
        }
    }
}
void DepartmentHandlerTask(void *param)
{
    // handle event here
    CityDepartmentHandlerState_t *handlerState = (CityDepartmentHandlerState_t *)param;
    printf(logFormats[eLOG_UNIT_INITIALIZED], handlerState->name);

    for(;;)
    {
        printf(logFormats[eLOG_UNIT_AWAITING], handlerState->name);

        while(!handlerState->busy)
        {
            vTaskDelay(1);
        }

        printf(logFormats[eLOG_UNIT_HANDLING], handlerState->name, handlerState->currentEvent.description);
        vTaskDelay(handlerState->currentEvent.ticks);
        handlerState->busy = false;
        printf(logFormats[eLOG_UNIT_FINISHED], handlerState->name, handlerState->currentEvent.description);
    }
}
void LoggerTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    printf(logFormats[eLOG_LOGGER_STARTING]);

    for(;;)
    {
        vTaskDelay(LOGGER_SLEEP);
    }
}
void EventGeneratorTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP*2);
    printf(logFormats[eLOG_GENERATOR_STARTING]);

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
        printf(logFormats[eLOG_GENERATOR_EMITTING],
                nextEvent->description, pdTICKS_TO_MS(nextEvent->ticks));
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
