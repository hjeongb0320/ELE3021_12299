#include "types.h"
#include "stat.h"
#include "user.h"

int 
main(int argc, char *argv[])
{
    if(argc <= 1) {
        exit();
    }

    if(strcmp(argv[1], "2019030991_12299_Hong") != 0) {
        exit();
    }

    char *buf = "2019030991_12299_Hong";
    int ret_val;
    ret_val = myfunction(buf);
    printf(1, "Return value : 0x%x\n", ret_val);
    exit();
};