enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct process_info {
    int pid;
    char name[16];
    enum procstate state;
    int ppid;
    int namespaceid;
    int getpid;
    int getppid;
};