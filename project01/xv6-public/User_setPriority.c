#include "types.h"
#include "stat.h"
#include "user.h"

int 
main(int argc, char *argv[])
{
    if(argc <= 2) {
        exit();
    }
    
    int ipt_pid = atoi(argv[1]);
    int ipt_priority = atoi(argv[2]);

    setPriority(ipt_pid,ipt_priority);
    printf(1, "setPriority Complete.\n");

    exit();
};