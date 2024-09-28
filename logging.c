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

void print_timestamp()
{
    char buf[12];
    struct tm* tm_info;

    time_t timer = time(NULL);
    tm_info = localtime(&timer);

    strftime(buf, sizeof(buf), "%H:%M:%S ~ ", tm_info);
    printf("%s", buf);

}
