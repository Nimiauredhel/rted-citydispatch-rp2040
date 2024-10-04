#ifndef LOGGING_H
#define LOGGING_H

#include "pico/printf.h"
#include "pico/util/datetime.h"
#include "hardware/rtc.h"

#define LOG_MAX_LENGTH 64

typedef const enum LogFormatId
{
    eLOG_DISPATCHER_STARTING,
    eLOG_DISPATCHER_WAITING,
    eLOG_DISPATCHER_ROUTING,

    eLOG_MANAGER_STARTING,
    eLOG_MANAGER_INITIALIZING_AGENTS,
    eLOG_MANAGER_WAITING,
    eLOG_MANAGER_ASSIGNING_EVENT,

    eLOG_UNIT_INITIALIZED,
    eLOG_UNIT_AWAITING,
    eLOG_UNIT_HANDLING,
    eLOG_UNIT_FINISHED,

    eLOG_GENERATOR_STARTING,
    eLOG_GENERATOR_AWAITING,
    eLOG_GENERATOR_EMITTING,

    eLOG_LOGGER_STARTING,
} LogFormatId_t;

typedef enum LoggerBehavior
{
    NONE = 0,
    PRINT_LOG = 1,
    PRINT_STATUS = 2
} LoggerBehavior_t;

extern const char logFormats[15][LOG_MAX_LENGTH];
extern LoggerBehavior_t loggerBehavior;

void logger_print_timestamp();

void logger_log_dispatcher_starting(void);
void logger_log_dispatcher_waiting(void);
void logger_log_dispatcher_routing(char *event_name, const char *department_name);

void logger_log_manager_starting(const char *department_name);
void logger_log_manager_initializing(const char *department_name, uint8_t numAgents);
void logger_log_manager_waiting(const char *department_name);
void logger_log_manager_routing(const char *department_name, char *event_name);

void logger_log_unit_waiting(char *unit_name);
void logger_log_unit_initialized(char *unit_name);
void logger_log_unit_handling(char *unit_name, char *event_name);
void logger_log_unit_finished(char *unit_name, char *event_name);

void logger_log_eventgen_starting(void);
void logger_log_eventgen_waiting(void);
void logger_log_eventgen_emitting(char *event_name, uint32_t event_ms);

void logger_log_logger_starting(void);

#endif
