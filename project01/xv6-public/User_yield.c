#include "types.h"
#include "stat.h"
#include "user.h"

int 
main(int argc, char *argv[])
{
    yield();
    printf(1, "yield complete\n");
    exit();
};

