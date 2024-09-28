#ifndef LOGGING_H
#define LOGGING_H

#define LOG_MAX_LENGTH 64

typedef enum LogFormatId
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
    eLOG_GENERATOR_EMITTING,

    eLOG_LOGGER_STARTING,
} LogFormatId_t;

char logFormats[14][LOG_MAX_LENGTH] =
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
    "Unit %s Finished Handling \"%s Event\".\n",

    "Event Generator Starting..\n",
    "Emitting \"%s Event\", Estimated Handling Time: %ums.\n",

    "Logger Starting...\n",
};

#endif
