#include "logging.h"

const char logFormats[15][LOG_MAX_LENGTH] =
{
    "Central Dispatcher Starting...\n",
    "Central Dispatcher Awaiting Messages.\n",
    "Central Dispatcher Routing \"%s Event\" to %s Department.\n",

    "%s Department Manager Starting...\n",
    "%s Department Manager Initializing %u Agents.\n",
    "%s Department Manager Awaiting Messages.\n",
    "%s Department Manager Assigning \"%s Event\".\n",

    "Unit %s Initialized.\n",
    "Unit %s Awaiting Instructions.\n",
    "Unit %s Handling \"%s Event\".\n",
    "----Unit %s Finished Handling \"%s Event\".----\n",

    "Event Generator Starting..\n",
    "Event Generator Awaiting User Input.\n",
    "~~Emitting \"%s Event\", Estimated Handling Time: %ums.~~\n",

    "Logger Starting...\n",
};

LoggerBehavior_t loggerBehavior = PRINT_LOG;

void logger_print_timestamp()
{
    char buf[12];
    struct tm* tm_info;

    time_t timer = time(NULL);
    tm_info = localtime(&timer);

    strftime(buf, sizeof(buf), "%H:%M:%S ~ ", tm_info);
    printf("%s", buf);

}

void logger_log_dispatcher_starting(void) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf("%s", logFormats[eLOG_DISPATCHER_WAITING]);
}
void logger_log_dispatcher_waiting(void) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf("%s", logFormats[eLOG_DISPATCHER_WAITING]);
}
void logger_log_dispatcher_routing(char *event_name, const char *department_name) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_DISPATCHER_ROUTING], event_name, department_name);
}
void logger_log_manager_starting(const char *department_name) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_MANAGER_STARTING], department_name);
}
void logger_log_manager_initializing(const char *department_name, uint8_t numAgents) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_MANAGER_INITIALIZING_AGENTS], department_name, numAgents);
}
void logger_log_manager_waiting(const char *department_name) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_MANAGER_WAITING], department_name);
}
void logger_log_manager_routing(const char *department_name, char *event_name) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_MANAGER_ASSIGNING_EVENT], department_name, event_name);
}
void logger_log_unit_waiting(char *unit_name) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_UNIT_AWAITING], unit_name);
}
void logger_log_unit_initialized(char *unit_name) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_UNIT_INITIALIZED], unit_name);
}
void logger_log_unit_handling(char *unit_name, char *event_name) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_UNIT_HANDLING], unit_name, event_name);
}
void logger_log_unit_finished(char *unit_name, char *event_name) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_UNIT_FINISHED], unit_name, event_name);
}
void logger_log_eventgen_starting(void) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf("%s", logFormats[eLOG_GENERATOR_AWAITING]);
}
void logger_log_eventgen_waiting(void) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf("%s", logFormats[eLOG_GENERATOR_AWAITING]);
}
void logger_log_eventgen_emitting(char *event_name, uint32_t event_ms) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf(logFormats[eLOG_GENERATOR_EMITTING], event_name, event_ms);
}
void logger_log_logger_starting(void) 
{
    if (loggerBehavior != PRINT_LOG) return;
    logger_print_timestamp();
    printf("%s", logFormats[eLOG_LOGGER_STARTING]);
}
