// C libs
#include <stdlib.h>
#include <stdint.h>
// RP-2040 libs
#include "pico/stdlib.h"
#include "pico/printf.h"
#include "pico/time.h"
#include "pico/stdio_usb.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
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

#define LOGGER_PRIORITY (50)
#define CENTRAL_DISPATCHER_PRIORITY (100)
#define DEPARTMENT_DISPATCHER_PRIORITY (150)
#define DEPARTMENT_HANDLER_PRIORITY (200)
#define EVENT_GENERATOR_PRIORITY (250)

#define INITIAL_SLEEP (pdMS_TO_TICKS(1500))
#define EVENT_GENERATOR_SLEEP_MAX (pdMS_TO_TICKS(6000))
#define EVENT_GENERATOR_SLEEP_MIN (pdMS_TO_TICKS(2000))
#define LOGGER_SLEEP (pdMS_TO_TICKS(1000))

#define PIN_EVENT_GEN 2
#define PIN_PRINT_LOG 4
#define PIN_PRINT_ENABLE 6
#define PIN_PRINT_STATUS 8
#define PIN_EVENT_READY 28

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
//
// a "debouncing" cooldown for the event generation
// and its related gpio functions
const uint32_t buttonCooldownMs = 200;

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

// handle of the event generation task,
// only exposed here to allow it
// to be suspended/resumed via ISR
TaskHandle_t eventGeneratorHandle;

// tracking time since last gpio HIGH
// to enforce cooldown & debouncing
uint32_t gpioBounceTable[4] = {0};

// a counter of pending/ongoing events
// for user feedback (in this case, LED brightness)
uint8_t eventBacklog = 0;

// *** Function Declarations ***
void InitializeHardware(void);
CityData_t* InitializeCityData(void);
void InitializeCityTasks(CityData_t *cityData);
void InitializeHelperTasks(CityData_t *cityData);
void PrintStatus(CityData_t *cityData);
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

    gpio_init(PIN_EVENT_GEN);
    gpio_init(PIN_PRINT_ENABLE);
    gpio_init(PIN_PRINT_LOG);
    gpio_init(PIN_PRINT_STATUS);
    gpio_init(PIN_EVENT_READY);

    gpio_set_dir(PIN_EVENT_GEN, GPIO_IN);
    gpio_set_dir(PIN_EVENT_GEN, GPIO_IN);
    gpio_set_dir(PIN_EVENT_GEN, GPIO_IN);

    gpio_set_dir(PIN_EVENT_READY, GPIO_OUT);
    gpio_set_dir(PIN_PRINT_ENABLE, GPIO_OUT);

    gpio_put(PIN_EVENT_READY, false);

    gpio_put(PIN_PRINT_ENABLE, true);

    gpio_set_irq_enabled_with_callback(PIN_EVENT_GEN, GPIO_IRQ_EDGE_RISE, true, &onGpioRise);
    gpio_set_irq_enabled_with_callback(PIN_PRINT_LOG, GPIO_IRQ_EDGE_RISE, true, &onGpioRise);
    gpio_set_irq_enabled_with_callback(PIN_PRINT_STATUS, GPIO_IRQ_EDGE_RISE, true, &onGpioRise);

    gpio_set_function(27, GPIO_FUNC_PWM);
    pwm_set_enabled(5, true);
    pwm_set_wrap(5, 1024);
    pwm_set_both_levels(5, 0, 0);
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
            cityData, LOGGER_PRIORITY, NULL);
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

// ISR when a gpio input is set HIGH
void onGpioRise(uint gpio, uint32_t events)
{
    uint32_t currentMs = to_ms_since_boot(get_absolute_time());
    if (currentMs - gpioBounceTable[gpio] < buttonCooldownMs) return;
    gpioBounceTable[gpio] = currentMs;

    switch (gpio)
    {
        case PIN_EVENT_GEN:
            xTaskResumeFromISR(eventGeneratorHandle);
            break;
        case PIN_PRINT_LOG:
            loggerBehavior = PRINT_LOG;
            break;
        case PIN_PRINT_STATUS:
            loggerBehavior = PRINT_STATUS;
            break;
    }
}

void PrintStatus(CityData_t *cityData)
{
    printf("\n~~~~ CITY STATUS ~~~~\n\n~~ Unhandled Events: %u ~~\n\n",
            eventBacklog);

    for (int i = 0; i < NUM_DEPARTMENTS; i++)
    {
        printf("~ %s Department ~\n", departmentNames[i]);

        for (int j = 0; j < cityData->departments[i].agentCount; j++)
        {
            printf("~~ Unit %s Status: %s\n", 
                    cityData->departments[i].agentStates[j].name,
                    cityData->departments[i].agentStates[j].busy
                    ? "Busy" : "Free");
        }

        printf("\n");
    }

    printf("~~~~~~~~~~~~~~~~~~~~~\n");
}

// *** Task Definitions ***

// the central dispatcher reads events from the incoming events queue,
// and forwards them to the appropriate department queue
void CentralDispatcherTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    CityData_t *cityData = (CityData_t *)param;
    CityEvent_t handledEvent;

    logger_log_dispatcher_waiting();

    for(;;)
    {
        logger_log_dispatcher_waiting();

        if (xQueueReceive(cityData->incomingQueue, &(handledEvent), portMAX_DELAY))
        {
            logger_log_dispatcher_routing(handledEvent.description, departmentNames[handledEvent.code]);

            xQueueSend(cityData->departments[handledEvent.code].jobQueue, &(handledEvent), portMAX_DELAY);
            eventBacklog++;
        }
    }
}

// the department manager reads events from the department job queue,
// and forwards them to a free agent if available. if no agent
// is available, the manager waits until one is freed up.
void DepartmentManagerTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    CityDepartment_t *departmentData = (CityDepartment_t *)param;
    CityEvent_t *handledEvent = pvPortMalloc(sizeof(CityEvent_t));

    logger_log_manager_routing(departmentNames[handledEvent->code], handledEvent->description);

    for (int i = 0; i < departmentData->agentCount; i++)
    {
        xTaskCreate(DepartmentAgentTask, handledEvent->description, TASK_STACK_SIZE,
        &(departmentData->agentStates[i]), DEPARTMENT_HANDLER_PRIORITY, NULL);
    }

    for(;;)
    {
        logger_log_manager_waiting(departmentNames[departmentData->code]);

        if (xQueueReceive(departmentData->jobQueue, handledEvent, portMAX_DELAY))
        {
            logger_log_manager_routing(departmentNames[departmentData->code], handledEvent->description);
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

                vTaskDelay(10);

            } while(!assigned);
        }
    }
}

// the department agent waits to be assigned a task by its manager.
// it then waits for (task) milliseconds before reporting the task complete.
void DepartmentAgentTask(void *param)
{
    CityDepartmentAgentState_t *agentState = (CityDepartmentAgentState_t *)param;

    logger_log_unit_initialized(agentState->name);

    for(;;)
    {
        logger_log_unit_waiting(agentState->name);

        while(!agentState->busy)
        {
            vTaskDelay(1);
        }

        logger_log_unit_handling(agentState->name, agentState->currentEvent.description);
        vTaskDelay(agentState->currentEvent.ticks);
        agentState->busy = false;

        logger_log_unit_finished(agentState->name, agentState->currentEvent.description);
        eventBacklog--;
    }
}

// the logger doesn't do a lot yet,
// but it's generally responsible
// for logging and user feedback
void LoggerTask(void *param)
{
    //TODO: actually implement the logger in a meaningful way
    CityData_t *cityData = (CityData_t *)param;

    loggerBehavior = PRINT_LOG;
    vTaskDelay(INITIAL_SLEEP);

    logger_log_logger_starting();

    for(;;)
    {
        vTaskDelay(LOGGER_SLEEP);
        uint16_t level = 256/(1+(eventBacklog*4));
        pwm_set_both_levels(5, level, level);

        if (loggerBehavior == PRINT_STATUS)
        {
            PrintStatus(cityData);
        }
    }
}

// the event generator creates a new event
// randomly from the preset event templates,
// and adds it to the incoming event queue
void EventGeneratorTask(void *param)
{
    vTaskDelay(INITIAL_SLEEP);
    logger_log_eventgen_starting();

    QueueHandle_t *incomingQueue = (QueueHandle_t *)param;
    uint32_t nextSleep;
    uint8_t nextEventTemplate;
    CityEvent_t *nextEvent = pvPortMalloc(sizeof(CityEvent_t));

    for(;;)
    {
        logger_log_eventgen_waiting();

        gpio_put(28, true);
        vTaskSuspend(NULL);
        gpio_put(28, false);

        nextEventTemplate = RandomNumber()%8;
        nextEvent->code = eventTemplates[nextEventTemplate].code;
        nextEvent->description = eventTemplates[nextEventTemplate].description;
        nextEvent->ticks = eventTemplates[nextEventTemplate].minTicks
            + (RandomNumber()%(eventTemplates[nextEventTemplate].maxTicks-eventTemplates[nextEventTemplate].minTicks));

        logger_log_eventgen_emitting( nextEvent->description, pdTICKS_TO_MS(nextEvent->ticks));
        xQueueSend(*incomingQueue, nextEvent, portMAX_DELAY);

        //nextSleep = EVENT_GENERATOR_SLEEP_MIN
        //    + (RandomNumber()%(EVENT_GENERATOR_SLEEP_MAX-EVENT_GENERATOR_SLEEP_MIN));
        //vTaskDelay(nextSleep);
        vTaskDelay(pdMS_TO_TICKS(buttonCooldownMs));
    }
}
