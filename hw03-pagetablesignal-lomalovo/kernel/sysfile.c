//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "write_call_info.h"
#include "syscall.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}


int
sys_ps_pt0(void)
{
  int pid;
  uint64 table;
  argint(0, &pid);
  argaddr(1, &table);

  uint64* ptr_table = (uint64*)table;

  extern struct proc proc[NPROC];
  extern struct spinlock wait_lock;
  extern struct spinlock pid_lock;

  struct proc *p;

  acquire(&wait_lock);
  acquire(&pid_lock);
  for (p = proc; p < &proc[NPROC]; ++p) {
    acquire(&p->lock);
    if (p->state == UNUSED) {
      release(&p->lock);
      continue;
    }
    if (p->pid == pid) {
      release(&p->lock);
      release(&pid_lock);
      release(&wait_lock);
      if (copyout(myproc()->pagetable, (uint64)(ptr_table), (char*)p->pagetable, PGSIZE)) {
        return -1;
      }
      return 0;
    }
    release(&p->lock);
  }
  release(&pid_lock);
  release(&wait_lock);
  return -1;
}

int sys_ps_pt_1(void) {
    int pid;
  uint64 addr;
  uint64 table;
  argint(0, &pid);
  argaddr(1, &addr);
  argaddr(2, &table);

  if (addr > MAXVA) {
    return -1;
  }

  uint64* ptr_table = (uint64*)table;

  extern struct proc proc[NPROC];
  extern struct spinlock wait_lock;
  extern struct spinlock pid_lock;

  struct proc *p;

  acquire(&wait_lock);
  acquire(&pid_lock);
  for (p = proc; p < &proc[NPROC]; ++p) {
    acquire(&p->lock);
    if (p->state == UNUSED) {
      release(&p->lock);
      continue;
    }
    if (p->pid == pid) {
      release(&p->lock);
      release(&pid_lock);
      release(&wait_lock);
      uint64* elem = &p->pagetable[PX(2, (uint64*)addr)];
      pagetable_t pt = (pagetable_t)PTE2PA(*elem);
      if (copyout(myproc()->pagetable, (uint64)(ptr_table), (char*)pt, PGSIZE)) {
        return -1;
      }
      return 0;
    }
    release(&p->lock);
  }
  release(&pid_lock);
  release(&wait_lock);
  return -1;
}

int
sys_ps_pt_2(void)
{
  int pid;
  uint64 addr;
  uint64 table;
  argint(0, &pid);
  argaddr(1, &addr);
  argaddr(2, &table);

  if (addr > MAXVA) {
    return -1;
  }

  uint64* ptr_table = (uint64*)table;

  extern struct proc proc[NPROC];
  extern struct spinlock wait_lock;
  extern struct spinlock pid_lock;

  struct proc *p;

  acquire(&wait_lock);
  acquire(&pid_lock);
  for (p = proc; p < &proc[NPROC]; ++p) {
    acquire(&p->lock);
    if (p->state == UNUSED) {
      release(&p->lock);
      continue;
    }
    if (p->pid == pid) {
      release(&p->lock);
      release(&pid_lock);
      release(&wait_lock);
      uint64* elem_tmp = &p->pagetable[PX(2, (uint64*)addr)];
      pagetable_t pt_tmp = (pagetable_t)PTE2PA(*elem_tmp);
      uint64* elem = &pt_tmp[PX(1, (uint64*)addr)];
      pagetable_t pt = (pagetable_t)PTE2PA(*elem);
      if (copyout(myproc()->pagetable, (uint64)(ptr_table), (char*)pt, PGSIZE)) {
        return -1;
      }
      return 0;
    }
    release(&p->lock);
  }
  release(&pid_lock);
  release(&wait_lock);
  return -1;
}

int is_address_valid(struct proc *p, char *addr, int size) {
    pte_t *pte;
    uint64 pa;
    for (int i = 0; i < size; i += PGSIZE) {
        if ((pte = walk(p->pagetable, (uint64)addr + i, 0)) == 0)
            return 0; 
        if ((*pte & PTE_V) == 0)
            return 0;
        pa = PTE2PA(*pte);
        if (pa == 0)
            return 0; 
    }
    return 1;
}

int
sys_ps_copy(void)
{
  int pid;
  uint64 addr;
  int size;
  uint64 data;
  argint(0, &pid);
  argaddr(1, &addr);
  argint(2, &size);
  argaddr(3, &data);

  if (addr > MAXVA) {
    return -1;
  }

  extern struct proc proc[NPROC];

  struct proc *p;
  extern struct spinlock wait_lock;
  extern struct spinlock pid_lock;

  acquire(&wait_lock);
  acquire(&pid_lock);
  for (p = proc; p < &proc[NPROC]; ++p) {
    acquire(&p->lock);
    if (p->pid == pid) {
      if (p->state != UNUSED && p->state != ZOMBIE) {
        if (!is_address_valid(p, (char*)addr, size)) {
          release(&p->lock);
          release(&pid_lock);
          release(&wait_lock);
          return -1;
        }
        release(&p->lock);
        release(&pid_lock);
        release(&wait_lock);
        char buf[size];
        memmove(buf, (char*)walkaddr(p->pagetable, addr), size);
        if (copyout(myproc()->pagetable, data, buf, size)) {
          return -1;
        }
      } else {
        release(&p->lock);
        release(&pid_lock);
        release(&wait_lock);
        return -1;
      }
      release(&p->lock);
      release(&pid_lock);
      release(&p->lock);
      return 0;
    }
  }
  release(&pid_lock);
  release(&wait_lock);
  return -1;
}

int
sys_ps_sleep_on_write(void)
{
  int pid;
  argint(0, &pid);

  
  uint64 addr;
  argaddr(1, &addr);

  if (addr > MAXVA) {
    return -1;
  }

  struct write_call_info *info = (struct write_call_info*) addr;

  extern struct proc proc[NPROC];
  extern struct spinlock wait_lock;
  extern struct spinlock pid_lock;

  struct proc *p;

  acquire(&wait_lock);
  acquire(&pid_lock);
  for (p = proc; p < &proc[NPROC]; ++p) {
    acquire(&p->lock);
    if (p->pid == pid) {
      if (p->state != SLEEPING || p->last_syscall != SYS_write) {
        release(&p->lock);
        release(&pid_lock);
        release(&wait_lock);
        return 1; //ok, but nothing happens 
      }

      uint64 addr0, addr1, addr2;
      addr2 = p->trapframe->a1;
      addr0 = PGROUNDDOWN(addr2);
      addr1 = walkaddr(myproc()->pagetable, addr0);
      char* addr3 = (char *)(addr1 + (addr2 - addr0));

      info->fd = p->syscall_args[0];
      info->addr = addr3;
      info->n = p->syscall_args[2];

      if(copyout(myproc()->pagetable, (uint64)(info->buffer), addr3, info->n) < 0) {
        release(&p->lock);
        release(&pid_lock);
        release(&wait_lock);
        return -1;
      }
      release(&p->lock);
      release(&pid_lock);
      release(&wait_lock);
      return 0;
    }
    release(&p->lock);
  }
  release(&pid_lock);
  release(&wait_lock);
  return -1;
}