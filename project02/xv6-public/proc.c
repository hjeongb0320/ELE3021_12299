#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  struct thread *main_thread;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      main_thread = &(p->ttable[0]);
      goto found;
    }

  release(&ptable.lock);
  return 0;

found:
  //process(main thread) info
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->cur_thread = 0;
  p->sz_limit = 0;

  //thread info
  main_thread->state = EMBRYO;
  main_thread->tid = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((main_thread->kstack = kalloc()) == 0){
    p->state = UNUSED;
    main_thread->state = UNUSED;
    return 0;
  }
  sp = main_thread->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *main_thread->tf;
  main_thread->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *main_thread->context;
  main_thread->context = (struct context*)sp;
  memset(main_thread->context, 0, sizeof *main_thread->context);
  main_thread->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  struct thread *main_thread;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  main_thread = &(p->ttable[0]);
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(main_thread->tf, 0, sizeof(*main_thread->tf));
  main_thread->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  main_thread->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  main_thread->tf->es = main_thread->tf->ds;
  main_thread->tf->ss = main_thread->tf->ds;
  main_thread->tf->eflags = FL_IF;
  main_thread->tf->esp = PGSIZE;
  main_thread->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  main_thread->state = RUNNABLE;

  release(&ptable.lock);
}


// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);

  sz = curproc->sz;

  if(curproc->sz_limit) {
    if(sz+n >curproc->sz_limit) {
      cprintf("EXCEPTION : memory limit - sbrk\n");
      return -1;
    }
  }

  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  
  release(&ptable.lock);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct thread *main_thread;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  main_thread = &(np->ttable[0]);

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    kfree(main_thread->kstack);
    np->kstack = 0;
    main_thread->kstack = 0;
    np->state = UNUSED;
    main_thread->state = UNUSED;
    return -1;
  }

  acquire(&ptable.lock);

  np->sz = curproc->sz;
  np->sz_limit = curproc->sz_limit;
  np->parent = curproc;
  *main_thread->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  main_thread->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);


  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  np->state = RUNNABLE;
  main_thread->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;

      if(p->state == ZOMBIE) {
        pid = p->pid;
        struct thread *t;
        for(int i=9; i>=0; i--) {
          t = &(p->ttable[i]);
          if(t->state == UNUSED) continue;

          kfree(t->kstack);
          t->tid = 0;
          t->chan = 0;
          t->tf = 0;
          t->context = 0;
          t->kstack = 0;
          t->state = UNUSED;    
        }

        freevm(p->pgdir);
        p->pid = 0;
        p->kstack = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;

        for(int i=0; i<9; i++) p->thread_pool[i] = 0;
        
        release(&ptable.lock);
        return pid;
      }
    }
  

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  struct thread *t;
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE){
        continue;
      }

        int i = 0;
        for(t = &(p->ttable[0]); t < &(p->ttable[10]); t++){ // find last_sched thread arr index
          if(t->tid == p->cur_thread)
            break;
          i++;
        }

        int idx, thread_num;
        for(idx = 1; idx <= 10; idx++) { // find which thread have to be scheduling
          thread_num = i + idx;
          if(( t = (&(p->ttable[thread_num%10])) )->state == RUNNABLE){
          
            copy_thread(p,t);
            // Switch to chosen process.  It is the process's job
            // to release ptable.lock and then reacquire it
            // before jumping back to us.
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;

            swtch(&(c->scheduler), p->context);
            switchkvm();

            if(p->state != ZOMBIE)
              copy_process(p,&(p->ttable[p->cur_thread]));
            else break;

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;          
          }
        }
    }
    release(&ptable.lock);

  }
}

void 
copy_thread(struct proc* running_p, struct thread* running_thread)
{
  pushcli();

  running_p->kstack = running_thread->kstack;
  running_p->tf = running_thread->tf;
  running_p->context = running_thread->context;

  running_p->cur_thread = running_thread->tid;
  popcli();
}

void 
copy_process(struct proc* running_p, struct thread* running_thread)
{
  pushcli();
  running_thread->kstack = running_p->kstack;
  running_thread->tf = running_p->tf;
  running_thread->context = running_p->context;
  popcli();
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  struct thread *t;

  for(t = p->ttable; t < &(p->ttable[10]); t++) {
    if(t->tid == p->cur_thread) break;
  }
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  t->chan = chan;
  t->state = SLEEPING;
  p->state = RUNNABLE;

  sched();

  // Tidy up.
  t->chan = 0;
  //t->state = RUNNABLE;
  

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  struct thread *main_thread;
  struct thread *t;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    main_thread = &(p->ttable[0]);
    for(t = main_thread; t< &(p->ttable[10]); t++) {
      if(t->state == SLEEPING && t->chan == chan)
        t->state = RUNNABLE; 
      }
  }
}


// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;
  struct thread *thread;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      thread = &(p->ttable[0]);
      // Wake process from sleep if necessary.
      for(thread = &(p->ttable[0]); thread < &(p->ttable[10]); thread++){
        if(thread->state == SLEEPING)
          thread->state = RUNNABLE;
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

int
setmemorylimit(int pid, int limit)
{
  struct proc *p;
  int pid_chk = 0;

  acquire(&ptable.lock);

  // 1. process 찾기

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->pid == pid) {
      pid_chk = 1;
      break;
    }
  }
  release(&ptable.lock);

  // 2. 예외처리

  if(pid_chk == 0) {
    cprintf("EXCEPTION 0 : non-exit pid error\n");
    return -1;
  }

  if(limit != 0) {
    if(limit < 0) {
      cprintf("EXCEPTION 1 : negative limit\n");
      return -1;
    }

    if((p->sz)/4096 > limit) {
      cprintf("EXCEPTION 2 : sz already bigger than limit\n");
      return -1;
    }
  }

  // 3. limit 설정하기

  p->sz_limit = limit;

  return 0;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  int pnum = 0;
  struct proc *p;
  struct thread *t;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
      
    pnum++;
    cprintf("\n-------------\n");
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d. pid : %d\n", pnum, p->pid);
    cprintf("state : %s | name : %s\n", state, p->name);
    cprintf("current thread : %d\n", p->cur_thread);
    cprintf("stack pages : %d | memory size : %d | memory limit %d\n",(p->sz)/4096, p->sz, p->sz_limit);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n\n");

    cprintf("threads\n");

    for(t = p->ttable; t < &(p->ttable[10]); t++) {
      if(t->state == UNUSED) continue;
      if(t->state >= 0 && t->state < NELEM(states) && states[t->state])
        state = states[t->state];
      else
        state = "???";
      cprintf("%d %s", t->tid, state);
      if(p->state == SLEEPING){
        getcallerpcs((uint*)t->context->ebp+2, pc);
        for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
      }
      cprintf("\n-------------\n");
      cprintf("\n\n");
    }
  }
}