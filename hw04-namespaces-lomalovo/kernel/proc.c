#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct namespace namespaces[MAXNAMESPACES];

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->which_namespace = 0;
  p->is_root = 1;
  p->depth = 0;
  p->all_namespaces[0] = 1;

  namespaces[0].size = 1;
  namespaces[0].root = initproc;
  namespaces[0].next_free_pid = 2;
  namespaces[0].parent_namespace = -1;

  int idx = 1;
  while (idx < 8) {
    p->all_namespaces[idx++] = 0;
  }

  idx = 1;
  while (idx < MAXNAMESPACES) {
    namespaces[idx].size = 0;
    namespaces[idx++].parent_namespace = -1;
  }
  
  
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;

  np->is_root = 0;
  int depth = p->depth;
  np->depth = depth;
  
  int which_namespace = p->which_namespace;
  np->which_namespace = which_namespace;
  
  
  int idx = 0;
  while (idx < 8) {
    if (idx <= depth) {
      np->all_namespaces[idx++] = namespaces[which_namespace].next_free_pid++;
      namespaces[which_namespace].size++;

      which_namespace = namespaces[which_namespace].parent_namespace;
      
    } else {
      np->all_namespaces[idx++] = 0;
    }
    
  }

  release(&np->lock);

  pid = np->all_namespaces[depth];
  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  int ns_id = p->which_namespace;
  struct namespace* cur_namespace = &namespaces[ns_id];
  if (namespaces[ns_id].root == p) {
    namespaces[ns_id].root = 0;
    cur_namespace = &(namespaces[cur_namespace->parent_namespace]);
  }

  while (cur_namespace->root == 0) {
    cur_namespace = &(namespaces[cur_namespace->parent_namespace]); 
  }
  
  int idx = 0;
  while (idx < NPROC) {
    if (proc[idx].parent == p) {
      if (p->is_root) {
        proc[idx].is_root = 1;
      }
      proc[idx].parent = cur_namespace->root;
      wakeup(cur_namespace->root);
    }
    idx++;
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  int ns_id = p->which_namespace;

  while (ns_id > -1) {
    namespaces[ns_id].size--;
    ns_id = namespaces[ns_id].parent_namespace;
  } 

  if (namespaces[p->which_namespace].size == 0) { // освобождаем namespace, если он опустел
    namespaces[p->which_namespace].parent_namespace = -1;
  } else {
    // Give any children to init.
    reparent(p);
  }

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  p->which_namespace = -1;
  int idx = 0;
  while (idx < 8) {
    p->all_namespaces[idx++] = 0;
  }

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

int getppid(void) {
    if (myproc()->is_root) {
      return 0;
    }
    return myproc()->parent->all_namespaces[myproc()->depth];
}


int clone(void) {
    struct proc* p = myproc();
    struct proc* new_p = allocproc();

    if (p->depth == 7 || new_p  == 0) {
        return -1;
    }

    int new_id = 0;

    while(new_id < MAXNAMESPACES && namespaces[new_id].size != 0) {
      new_id++;
    }

    if (new_id == MAXNAMESPACES) {
        return -1;
    }


    if(uvmcopy(p->pagetable, new_p->pagetable, p->sz) < 0) {
        freeproc(new_p);
        release(&new_p->lock);
        return -1;
    }

    new_p->sz = p->sz;
    *(new_p->trapframe) = *(p->trapframe);
    new_p->trapframe->a0 = 0;
    new_p->cwd = idup(p->cwd);
    safestrcpy(new_p->name, p->name, sizeof(p->name));

    for(int i = 0; i < NOFILE; i++) {
        if (p->ofile[i]) {
          new_p->ofile[i] = filedup(p->ofile[i]);
        }
    }

    release(&new_p->lock);

    acquire(&wait_lock);
    new_p->which_namespace = new_id;
    new_p->is_root = 1;
    new_p->parent = p;
    new_p->depth = p->depth + 1;
    namespaces[new_p->which_namespace].parent_namespace = p->which_namespace;
    namespaces[new_p->which_namespace].root = new_p;
    namespaces[new_p->which_namespace].size = 1;
    namespaces[new_p->which_namespace].next_free_pid = 2;

    int pns = p->which_namespace;
    new_p->all_namespaces[new_p->depth] = 1;

    int i = p->depth;
    while (i >= 0) {
        new_p->all_namespaces[i] = namespaces[pns].next_free_pid++;
        namespaces[pns].size++;
        pns = namespaces[pns].parent_namespace;
        --i;
    }

    int pid = new_p->all_namespaces[p->depth];

    release(&wait_lock);

    acquire(&new_p->lock);
    new_p->state = RUNNABLE;
    release(&new_p->lock);

    return pid;
}


int ps_list(int limit, uint64 pids) {

    if (pids >= MAXVA) {
      return -1;
    }

    int all_our_pids[limit];

    extern struct proc proc[NPROC];
    extern struct spinlock wait_lock;
    extern struct spinlock pid_lock;

    acquire(&wait_lock);
    acquire(&pid_lock);

    int count = 0;
    for (int i = 0; i < NPROC; ++i) {

        struct proc* p = &proc[i];
        acquire(&p->lock);

        if (p->state != UNUSED) {
            int ns_flag = 0;
            int pns = p->which_namespace;

            while (pns > -1) {
                if (pns == myproc()->which_namespace) {
                    ns_flag = 1;
                    break;
                }
                pns = namespaces[pns].parent_namespace;
            }

            if (count < limit && ns_flag) {
                all_our_pids[count] = p->pid;
            }
            count++;
        }
        release(&p->lock);
    }
    release(&pid_lock);
    release(&wait_lock);

    if (limit > 0) {
        if (copyout(myproc()->pagetable, pids, (char*)(all_our_pids), limit * sizeof(int)) < 0) {
            return -1;
        }
    }
    
    return count;
}

int ps_info(int pid, uint64 psinfo) {
    if (psinfo >= MAXVA) {
      return -1;
    }

    extern struct proc proc[NPROC];
    extern struct spinlock wait_lock;
    extern struct spinlock pid_lock;

    acquire(&wait_lock);
    acquire(&pid_lock);
    for (int i = 0; i < NPROC; i++) {
        struct proc* p = &proc[i];
        acquire(&p->lock);
        if (p->pid == pid && p->state != UNUSED) {
            struct process_info pr_info;

            pr_info.pid = p->all_namespaces[0];
            strncpy(pr_info.name, p->name, sizeof(pr_info.name));
            pr_info.state = p->state;
            
            if (pid != 1) {
                acquire(&p->parent->lock);
                pr_info.ppid = p->parent->pid;
                release(&p->parent->lock);
                
            } else pr_info.ppid = 0;
            
            pr_info.namespaceid = p->which_namespace;
            pr_info.getpid = p->all_namespaces[p->depth];

            if (p->is_root) {
                pr_info.getppid = 0;
            } else {
                pr_info.getppid = p->parent->all_namespaces[p->depth];
            }

            release(&p->lock);
            release(&pid_lock);
            release(&wait_lock);
            return copyout(myproc()->pagetable, psinfo, (char*)(&pr_info), sizeof(struct process_info));
        }
        release(&p->lock);
    }
    release(&pid_lock);
    release(&wait_lock);

    return 1;
}