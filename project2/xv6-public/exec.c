#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir;
  pde_t *oldpgdir;
  struct proc *curproc = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;

  // check memeory limit
  if(curproc->sz_limit) { 
    if(ph.vaddr + ph.memsz > curproc->sz_limit) {
      cprintf("EXCEPTION : memory limit - exec - A\n");
      goto bad;
    }
  }

  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz); // round-up stack size to allocate in memory.

  // check memory limit
  if(curproc->sz_limit) {
    if(sz+(2*PGSIZE) > curproc->sz_limit) {
      cprintf("EXCEPTION : memory limit - exec - B\n");
      goto bad;
    }
  }

  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  pushcli();

  // 1. 지금까지 실행되던 모든 쓰레드를 종료하고 exec를 호출한 thread 하나만 남게 해야합니다.
  struct thread *t;
  struct thread *exec_call_thread = 0;
  for(int i=9; i>=0; i--) {
    t = &(curproc->ttable[i]);
    if(t->state == UNUSED) continue;

    // 1-1. 붙여넣을 thread, 즉 exec를 호출한 thread는 남겨둡니다.
    if(t->tid == curproc->cur_thread) {
      exec_call_thread = t;
      continue;
    }

    kfree(t->kstack);
    t->kstack = 0;
    t->tf = 0;
    t->context = 0;
    t->state = UNUSED;
    t->tid = 0;
    t->chan = 0;    
  }

  t = &(curproc->ttable[0]);

  // 2. main_thread의 위치로 thread를 옮겨주어야 합니다.

  if(exec_call_thread->tid != 0) {
    t->kstack = exec_call_thread->kstack;
    t->tf = exec_call_thread->tf;
    t->context = exec_call_thread->context;
    t->tid = 0;

    exec_call_thread->kstack = 0;
    exec_call_thread->tf = 0;
    exec_call_thread->context = 0;
    exec_call_thread->state = UNUSED;
    exec_call_thread->tid = 0;
  }

  curproc->cur_thread = 0;
  t->state = RUNNABLE;

  // Commit to the user image.
  // 3. proc 구조체 안에 정보롤 저장합니다.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);

  popcli();

  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

int
exec2(char *path, char **argv, int stacksize)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir;
  pde_t *oldpgdir;
  struct proc *curproc = myproc();

  if(stacksize > 100) {
    cprintf("ERROR : stacksize bigger than 100\n");
    return -1;
  }

  if(stacksize < 1) {
    cprintf("ERROR : stacksize smaller than 1\n");
    return -1;
  }

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec2: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.

  if(curproc->sz_limit) {
    if(ph.vaddr + ph.memsz > curproc->sz_limit) {
      cprintf("EXCEPTION : memory limit - exec2 - A\n");
      goto bad;
    }
  }

  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz); // round-up stack size to allocate in memory.

  if(curproc->sz_limit) {
    if(sz+(stacksize+1)*PGSIZE > curproc->sz_limit) {
      cprintf("EXCEPTION : memory limit - exec2 - B\n");
      goto bad;
    }
  }

  if((sz = allocuvm(pgdir, sz, sz + (stacksize+1)*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - (stacksize+1)*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  pushcli();

  // exec를 하면 지금까지 실행되던 모든 쓰레드를 종료하고 하나만 남게 해야합니다.
  struct thread *t;
  struct thread *exec_call_thread = 0;
  for(int i=9; i>=0; i--) {
    t = &(curproc->ttable[i]);
    if(t->state == UNUSED) continue;
    if(t->tid == curproc->cur_thread) {
      exec_call_thread = t;
      continue;
    }

    kfree(t->kstack);
    t->kstack = 0;
    t->tf = 0;
    t->context = 0;
    t->state = UNUSED;
    t->tid = 0;
    t->chan = 0;    
  }

  t = &(curproc->ttable[0]);

  // 2. main_thread의 위치로 thread를 옮겨주어야 합니다.

  if(exec_call_thread->tid != 0) {
    t->kstack = exec_call_thread->kstack;
    t->tf = exec_call_thread->tf;
    t->context = exec_call_thread->context;
    t->tid = 0;

    exec_call_thread->kstack = 0;
    exec_call_thread->tf = 0;
    exec_call_thread->context = 0;
    exec_call_thread->state = UNUSED;
    exec_call_thread->tid = 0;
  }
  
  curproc->cur_thread = 0;
  t->state = RUNNABLE;

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);

  popcli();

  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}