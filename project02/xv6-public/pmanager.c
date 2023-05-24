#include "types.h"
#include "stat.h"
#include "user.h"

int
getcmd(char *buf, int nbuf)
{
  printf(2, "(PMG) : ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int parse(char *buf, char **args)
{
  int argc = 0;
  while (*buf != '\0') {
    // Skip leading whitespace
    while (*buf == ' ' || *buf == '\t' || *buf == '\n')
      *buf++ = '\0';

    // If reached the end of the buffer, break
    if (*buf == '\0')
      break;

    // Save the current argument
    args[argc++] = buf;

    // Find the end of the current argument
    while (*buf != '\0' && *buf != ' ' && *buf != '\t' && *buf != '\n')
      buf++;
  }

  // Null-terminate the argument list
  args[argc] = 0;

  return argc;
}

int
main(int argc, char *argv[])
{
    static char buf[100];
    printf(1, "Process manager start\n");

    while(getcmd(buf, sizeof(buf)) >= 0){

        char *args[10];  // Maximum 10 arguments
        parse(buf, args);

        if (args[0] == 0) continue;

        if (strcmp(args[0], "list") == 0) {
            printf(1, "Running the list command\n");
            procdump();
        
        }
        else if (strcmp(args[0], "kill") == 0) {
            if(args[1] != 0) {
                printf(1, "Running the kill command\n");

                uint pid = atoi(args[1]);

                if(kill(pid) == 0) {
                    printf(1,"SUCCESS : pid %d killed\n",pid);
                }
                else printf(1,"ERROR : pid %d \n",pid);
    
                }
            }
        else if (strcmp(args[0], "execute") == 0) {
            if (args[1] != 0 && args[2] != 0) {
            printf(1, "Running the execute command with path: %s and stacksize: %s\n", args[1], args[2]);
      
            char* path = args[1];
            char *val[2] = {path,0};
            uint stacksize = atoi(args[2]);

            int pid = fork();

            if(pid == 0) {
                if(exec2(path,val,stacksize) == -1) printf(1,"ERROR : exec2 fail\n");
                exit();
                }
            }
        }
        else if (strcmp(args[0], "memlim") == 0) {

            if (args[1] != 0 && args[2] != 0) {
                printf(1, "Running the memlim command with pid: %s and limit: %s\n", args[1], args[2]);
                int pid = atoi(args[1]);
                int limit = atoi(args[2]);
                if(setmemorylimit(pid,limit) == 0) printf(1, "SUCCESS : set memory limit");
                else printf(1, "ERROR : set memory limit");
            }
        }
        else if (strcmp(args[0], "exit") == 0) {
            printf(1, "Exiting the process manager\n");
            exit();
        }
        else {
            printf(1, "Invalid command: %s\n", args[0]);
        }
    }
}