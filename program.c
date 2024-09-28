// C libs
#include <stdlib.h>
#include <stdint.h>
// RP-2040 libs
#include "pico/stdlib.h"
#include "pico/printf.h"
#include "pico/time.h"
#include "pico/stdio_usb.h"
#include "hardware/gpio.h"
#include "hardware/rtc.h"
#include "hardware/regs/rosc.h"
#include "hardware/regs/addressmap.h"
// FreeRTOS libs
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"
// Application headers
#include "logging.h"

// *** Definitions ***
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
typedef enum DepartmentCode
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
typedef struct CityDepartmentAgentState
{
    bool busy;
    char name[16];
    CityEvent_t currentEvent;
} CityDepartmentAgentState_t;
typedef struct CityDepartment
{
    DepartmentCode_t code;
    BaseType_t status;
    QueueHandle_t jobQueue;
    uint8_t agentCount;
    CityDepartmentAgentState_t *agentStates;
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

// *** Global Constants ***
const char departmentNames[NUM_DEPARTMENTS][10] = {"Medical\0", "Police\0", "Fire\0", "Covid-19\0"};
const uint8_t departmentAgentCounts[NUM_DEPARTMENTS] = {4, 3, 2, 4};
// Events will be generated, randomly or otherwise,
// from this pool of event templates
const CityEventTemplate_t eventTemplates[8] =
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

// *** Global Variables ***
TaskHandle_t eventGeneratorHandle;
uint32_t gpioBounceTable[4] = {0};

// *** Function Declarations ***
void InitializeHardware(void);
CityData_t* InitializeCityData(void);
void InitializeCityTasks(CityData_t *cityData);
void InitializeHelperTasks(CityData_t *cityData);
uint32_t RandomNumber(void);
void onGpioRise(uint gpio, uint32_t events);
// *** Task Declarations ***
void CentralDispatcherTask(void *param);
void DepartmentManagerTask(void *param);
void DepartmentAgentTask(void *param);
void LoggerTask(void *param);
void EventGeneratorTask(void *param);

// *** Function Definitions ***
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
    rtc_init();
    stdio_init_all();
    gpio_init(2);
    gpio_init(28);
    gpio_set_dir(2, false);
    gpio_set_dir(28, true);
    gpio_put(28, false);
    gpio_set_irq_enabled_with_callback(2, GPIO_IRQ_EDGE_RISE, true, &onGpioRise);
}

CityData_t* InitializeCityData(void)
{
    CityData_t *cityData = pvPortMalloc(sizeof(CityData_t));
    cityData->incomingQueue = xQueueCreate(INCOMING_QUEUE_LENGTH, sizeof(CityEvent_t));

    for (int i = 0; i < NUM_DEPARTMENTS; i++)
    {
        cityData->departments[i].code = i;
        cityData->departments[i].jobQueue = xQueueCreate(DEPARTMENT_QUEUE_LENGTH, sizeof(CityEvent_t));
        cityData->departments[i].agentCount = departmentAgentCounts[i];
        cityData->departments[i].agentStates = pvPortMalloc(sizeof(CityDepartmentAgentState_t)
                * departmentAgentCounts[i]);
                
        for (int j = 0; j < departmentAgentCounts[i]; j++)
        {
            cityData->departments[i].agentStates[j].busy = false;
            sprintf(cityData->departments[i].agentStates[j].name, "%s-%u", departmentNames[i], j+1);
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
            DepartmentManagerTask,
            departmentNames[cityData->departments[i].code], TASK_STACK_SIZE,
            &(cityData->departments[i]), DEPARTMENT_DISPATCHER_PRIORITY, NULL);
    }
}

void GenerateRandomEvent(void)
{

}

void InitializeHelperTasks(CityData_t *cityData)
{
    xTaskCreate( LoggerTask, "Logger", TASK_STACK_SIZE,
            NULL, LOGGER_PRIORITY, NULL);
    xTaskCreate( EventGeneratorTask, "EventGenerator", TASK_STACK_SIZE,
            &(cityData->incomingQueue), EVENT_GENERATOR_PRIORITY, &eventGeneratorHandle);
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

void onGpioRise(uint gpio, uint32_t events)
{
    static uint32_t cooldown = 300;
    uint32_t currentMs = to_ms_since_boot(get_absolute_time());
    if (currentMs - gpioBounceTable[gpio] < cooldown) return;
    gpioBounceTable[gpio] = currentMs;
    xTaskResumeFromISR(eventGeneratorHandle);
}

// *** Task Definitions ***
void CentralDispatcherTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    CityData_t *cityData = (CityData_t *)param;
    CityEvent_t handledEvent;

    print_timestamp();
    printf(logFormats[eLOG_DISPATCHER_WAITING]);

    for(;;)
    {
        print_timestamp();
        printf(logFormats[eLOG_DISPATCHER_WAITING]);

        if (xQueueReceive(cityData->incomingQueue, &(handledEvent), portMAX_DELAY))
        {
            print_timestamp();
            printf(logFormats[eLOG_DISPATCHER_ROUTING], handledEvent.description, departmentNames[handledEvent.code]);

            xQueueSend(cityData->departments[handledEvent.code].jobQueue, &(handledEvent), portMAX_DELAY);
        }
    }
}

void DepartmentManagerTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    CityDepartment_t *departmentData = (CityDepartment_t *)param;
    CityEvent_t *handledEvent = pvPortMalloc(sizeof(CityEvent_t));

    print_timestamp();
    printf(logFormats[eLOG_DISPATCHER_ROUTING], departmentNames[departmentData->code]);

    for (int i = 0; i < departmentData->agentCount; i++)
    {
        xTaskCreate(DepartmentAgentTask, handledEvent->description, TASK_STACK_SIZE,
        &(departmentData->agentStates[i]), DEPARTMENT_HANDLER_PRIORITY, NULL);
    }

    for(;;)
    {
        print_timestamp();
        printf(logFormats[eLOG_MANAGER_WAITING], departmentNames[departmentData->code]);

        if (xQueueReceive(departmentData->jobQueue, handledEvent, portMAX_DELAY))
        {
            print_timestamp();
            printf(logFormats[eLOG_MANAGER_ASSIGNING_EVENT], departmentNames[departmentData->code], handledEvent->description);

            bool assigned = false;

            do
            {
                for (int i = 0; i < departmentData->agentCount; i++)
                {
                    if (!(departmentData->agentStates[i].busy))
                    {
                        departmentData->agentStates[i].currentEvent = *handledEvent;
                        departmentData->agentStates[i].busy = true;
                        assigned = true;
                        break;
                    }
                }
            } while(!assigned);
        }
    }
}
void DepartmentAgentTask(void *param)
{
    CityDepartmentAgentState_t *agentState = (CityDepartmentAgentState_t *)param;

    print_timestamp();
    printf(logFormats[eLOG_UNIT_INITIALIZED], agentState->name);

    for(;;)
    {
        print_timestamp();
        printf(logFormats[eLOG_UNIT_AWAITING], agentState->name);

        while(!agentState->busy)
        {
            vTaskDelay(1);
        }

        print_timestamp();
        printf(logFormats[eLOG_UNIT_HANDLING], agentState->name, agentState->currentEvent.description);

        vTaskDelay(agentState->currentEvent.ticks);
        agentState->busy = false;

        print_timestamp();
        printf(logFormats[eLOG_UNIT_FINISHED], agentState->name, agentState->currentEvent.description);
    }
}
void LoggerTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);

    print_timestamp();
    printf(logFormats[eLOG_LOGGER_STARTING]);

    for(;;)
    {
        vTaskDelay(LOGGER_SLEEP);
    }
}
void EventGeneratorTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);

    print_timestamp();
    printf(logFormats[eLOG_GENERATOR_STARTING]);

    QueueHandle_t *incomingQueue = (QueueHandle_t *)param;
    uint32_t nextSleep;
    uint8_t nextEventTemplate;
    CityEvent_t *nextEvent = pvPortMalloc(sizeof(CityEvent_t));

    for(;;)
    {
        print_timestamp();
        printf(logFormats[eLOG_GENERATOR_AWAITING]);

        vTaskSuspend(NULL);

        nextEventTemplate = RandomNumber()%8;
        nextEvent->code = eventTemplates[nextEventTemplate].code;
        nextEvent->description = eventTemplates[nextEventTemplate].description;
        nextEvent->ticks = eventTemplates[nextEventTemplate].minTicks
            + (RandomNumber()%(eventTemplates[nextEventTemplate].maxTicks-eventTemplates[nextEventTemplate].minTicks));
        gpio_put(28, true);

        print_timestamp();
        printf(logFormats[eLOG_GENERATOR_EMITTING],

                nextEvent->description, pdTICKS_TO_MS(nextEvent->ticks));
        xQueueSend(*incomingQueue, nextEvent, portMAX_DELAY);
        gpio_put(28, false);

        //nextSleep = EVENT_GENERATOR_SLEEP_MIN
        //    + (RandomNumber()%(EVENT_GENERATOR_SLEEP_MAX-EVENT_GENERATOR_SLEEP_MIN));
        //vTaskDelay(nextSleep);
    }
}
