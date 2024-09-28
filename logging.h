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

extern const char logFormats[15][LOG_MAX_LENGTH];

void print_timestamp();

#endif
