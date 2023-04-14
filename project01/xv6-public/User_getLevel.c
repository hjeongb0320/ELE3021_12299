#include "types.h"
#include "stat.h"
#include "user.h"

int 
main(int argc, char *argv[])
{
    int lvl = getLevel();
    printf(1, "Running Process level : %d\n", lvl);
    exit();
};

