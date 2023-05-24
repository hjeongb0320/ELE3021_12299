#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

extern void forkret(void);
extern void trapret(void);
extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

int thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg)
{
    struct proc *p = myproc();

    pushcli();
    struct thread *t = 0;
    struct thread *main_thread = &(p->ttable[0]);
    struct thread *now_thread;

    for(now_thread = main_thread; now_thread < &(p->ttable[10]); now_thread++) {
        if(now_thread->tid == p->cur_thread) break;
    }
    
    char *sp;

    // 예외처리
    // A. 쓰레드 테이블이 가득찼다면 "쓰레드를 더이상 만들 수 없음" 이라고 출력하고 return -1
    // B. 쓰레드를 골랐는데 main thread가 선택되었을 때 return -1
    // C. 메인 쓰레드가 아닌 쓰레드가 thread_create를 호출 했을 때 return -1

    // 코드 디자인
    // 1. 할당받을 쓰레드를 탐색

    int chk_valid_thread = 0;
    int thread_index = 0;

    for(t = main_thread; t < &(p->ttable[10]); t++) {
        if(t->state == UNUSED) {
            chk_valid_thread = 1;
            break;
        }
        thread_index++;
    }

    // 예외 A
    if(chk_valid_thread == 0) {
        cprintf("EXCEPTION 0 : The maximum number of threads has already been allocated.\n");
        popcli();
        return -1;
    }

    // 예외 B
    if(thread_index == 0) {
        cprintf("EXCEPTION 1 : Main thread terminated.\n");
        popcli();
        return -1;
    }

    // 에외 C
    if(now_thread != main_thread) {
        cprintf("EXCEPTION 2 : Caller is not main thread\n");
        popcli();
        return -1;
    }

    //thread info
    t->state = EMBRYO;
    t->tid = thread_index;
    *thread = t->tid;

    // 2. thread를 할당받음

    // Allocate kernel stack.
    if((t->kstack = kalloc()) == 0){
        p->state = UNUSED;
        t->state = UNUSED;
        return 0;
    }

    sp = t->kstack + KSTACKSIZE;

    // Leave room for trap frame.
    sp -= sizeof *t->tf;
    t->tf = (struct trapframe*)sp;

    // Set up new context to start executing at forkret,
    // which returns to trapret.
    sp -= 4;
    *(uint*)sp = (uint)trapret;

    sp -= sizeof *t->context;
    t->context = (struct context*)sp;
    memset(t->context, 0, sizeof *t->context);
    t->context->eip = (uint)forkret;

    *t->tf = *now_thread->tf;
    t->tf->eax = 0;

    // 3. thread에 함수 정보 저장

    uint sz = p->sz;
    uint spt;
    uint ustack[2];
    pde_t *pgdir = p->pgdir;
    int i;

    uint pool_sz;
    int find = 0;

    // thread_pool에 빈 user stack이 있는지 탐색
    for(i=0; i<10; i++) {
     if(p->thread_pool[i] != 0) {
            pool_sz = p->thread_pool[i];
            p->thread_pool[i] = 0;
            find = 1;
            break;
        }
    }

    // 빈 user stack을 발견하지 못했다면 할당을 해주고 발견했다면 할당하지 않고 재활용함
    if(find == 0) {
        // Allocate two pages at the next page boundary.
        // Make the first inaccessible.  Use the second as the user stack.
        sz = PGROUNDUP(sz); // round-up stack size to allocate in memory.
        t->start = sz;
        if(p->sz_limit) {
            if(sz+(2*PGSIZE) > p->sz_limit) {
            cprintf("EXCEPTION : memory limit - thread_create\n");
            goto bad;
            }
        }

        if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
            goto bad;
        clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
        spt = sz;
    }
    else {
        t->start = pool_sz;
        spt = t->start;
    }

    // 스택 영역에 인자 저장

    ustack[0] = 0xffffffff;  // fake return PC
    ustack[1] = (uint)arg;

    spt = spt - sizeof(ustack);
    
    if(copyout(pgdir, spt, ustack, sizeof(ustack)) < 0) {
        cprintf("spt : %d\n",spt);
        panic("dead");
        goto bad;
    }

    p->sz = sz;
    p->pgdir = pgdir;
    t->tf->eip = (uint)start_routine;
    t->tf->esp = spt;
    switchuvm(p);

    t->state = RUNNABLE;

    popcli();

    return 0;

  bad:
    if(pgdir) freevm(pgdir);
    popcli();
    return -1;
}

void thread_wakeup(void *chan)
{
    struct proc *p = myproc();
    struct thread *main_thread = &(p->ttable[0]);
    
    main_thread->state = RUNNABLE;
}

void thread_exit(void *retval)
{
    struct proc *p = myproc();
    struct thread *main_thread = &(p->ttable[0]);
    struct thread *t;

    acquire(&ptable.lock);

    for(t = main_thread; t < &(p->ttable[10]); t++) {
        if(t->tid == p->cur_thread) break;
    }

    if(t == main_thread) {
        cprintf("Exception 0 : Attempting to exit the main thread");
        return ;
    }

    thread_wakeup(main_thread);

    t->retval = retval;
    t->state = ZOMBIE;
    p->state = RUNNABLE;

    sched();

    cprintf("ZOMBIE thread %d\n",t->tid);
    panic("zombie exit");
}

int thread_join(thread_t thread, void **retval)
{
    struct proc *p = myproc();
    struct thread *join_thread;
    struct thread *main_thread = &(p->ttable[p->cur_thread]);
    int i;

    if(p->cur_thread != 0) {
        cprintf("EXCEPTION 0 : Not mainthread join\n");
        return -1;
    }

    acquire(&ptable.lock);

    for(join_thread = p->ttable; join_thread < &(p->ttable[10]); join_thread++) {
        if(join_thread->tid == thread) break;
    }

    // thread 자원회수
    for(;;) 
    {  
        if(join_thread->state == ZOMBIE) {
    
            kfree(join_thread->kstack);
            join_thread->kstack = 0;
            join_thread->state = UNUSED;
            join_thread->tf = 0;
            join_thread->context = 0;
            join_thread->tid = 0;

            *retval = join_thread->retval;
            join_thread->retval = 0;

            for(i=0; i<10; i++) {
                if(p->thread_pool[i] == 0) {
                    p->thread_pool[i] = join_thread->start;
                }
            }
            switchuvm(p);

            release(&ptable.lock);

            return 0;
        }

        sleep(main_thread, &ptable.lock);
    }
}
