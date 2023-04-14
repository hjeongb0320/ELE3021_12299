#include "types.h"
#include "stat.h"
#include "user.h"

int 
main(int argc, char *argv[])
{
    if(argc <= 1) {
        exit();
    }
    
    int password = atoi(argv[1]);

    schedulerLock(password);

    printf(1, "SchedulerLock finish\n");

    exit();
};