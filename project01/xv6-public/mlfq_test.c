#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_CHILD 5
#define NUM_LOOP 50000

int me;

int create_child(void)
{
  int pid;
  for(int i =0  ; i<NUM_CHILD; i++){
    if((pid = fork()) == 0){
      me = i;
      sleep(10);
      return 0;
    } // else schedulerLock(2019030991);
  } 
  return 1;
}

void exit_child(int parent) 
{
	if (parent)
		while (wait() != -1);
	exit();
}

int main()
{
  int p;
  p = create_child();
  // schedulerLock(2019030991);
  // schedulerUnlock(2019030991);

	if (!p) {
		int arr[3] = {0, };
		for (int i = 0; i < NUM_LOOP; i++) {
			arr[getLevel()]++;
		}
    // printf(1, "pid %d : L0=%d, L1=%d, L2=%d\n", getpid(), arr[0], arr[1], arr[2]);
	}

	exit_child(p);

  return 0;
}