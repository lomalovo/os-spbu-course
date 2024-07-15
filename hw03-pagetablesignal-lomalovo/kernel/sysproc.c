#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_sigsetmask(void) {
    int mask; argint(0, &mask);
    struct proc *p = myproc();
    int old_mask = p->signal_mask;
    p->signal_mask = mask;
    return old_mask;
}

int sys_siggetmask(void) {
    return myproc()->signal_mask;
}

int sys_signal(void) {
  int sig; argint(0, &sig);
  uint64 handler_addr; argaddr(1, &handler_addr);
  void (*handler)(int) = (void (*)(int))handler_addr;

  if(sig == SIGKILL)
    return -1;  

  if(handler != (void (*)(int))SIG_IGN && handler != (void (*)(int))SIG_DFL)
    return -1;  

  if(sig == SIGPIPE)
    myproc()->sig_handlers[SIGPIPE] = handler;

  return 0;
}

