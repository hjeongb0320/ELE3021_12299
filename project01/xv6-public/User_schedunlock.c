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

    schedulerUnlock(password);

    printf(1, "SchedulerUnLock finish\n");

    exit();
};