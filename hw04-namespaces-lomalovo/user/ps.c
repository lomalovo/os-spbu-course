#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/riscv.h"
#include "kernel/syscall.h"
#include "kernel/param.h"

struct pids_info {
    int count;
    int* pids;
};

inline const char* ToString(enum procstate state)
{
    switch (state)
    {
		case USED: return "USED";
		case SLEEPING: return "SLEEPING";
		case RUNNABLE: return "RUNNABLE";
		case RUNNING: return "RUNNING";
		case ZOMBIE: return "ZOMBIE";
        default: return "UNKNOWN";
    }
}

void print_process(struct process_info* proc_info) {
    printf("pid: %d\n", proc_info->pid);
    printf("name: %s\n", proc_info->name);
    printf("state: %s\n", ToString(proc_info->state));
    printf("ppid: %d\n", proc_info->ppid);
    printf("namespace id: %d\n", proc_info->namespaceid);
    printf("getpid(): %d\n", proc_info->getpid);
    printf("getppid(): %d\n\n", proc_info->getppid);
}

struct pids_info over_limit() {
    int limit = DEFNUMPROC;
    int* pids = malloc(limit * sizeof(int));

    if (pids == 0) {
        printf("Error: ps pids - problem with allocation of memory\n");
        exit(1);
    }

    int count = ps_list(limit, pids);
    if (count < 0) {
        printf("Error: ps count - something wrong\n");
        exit(1);
    }

    while (count > limit) {
        limit = 2 * count < NPROC ? 2 * count : NPROC;
        free(pids);
        pids = malloc(limit * sizeof(int));

        if (pids == 0) {
            printf("Error: ps pids - problem with allocation of memory\n");
            exit(1);
        }

        count = ps_list(limit, pids);
        if (count < 0) {
            printf("Error: ps count - something wrong\n");
            exit(1);
        }
    }
    

    struct pids_info ans;
    ans.count = count;
    ans.pids = pids;
    return ans;
}


void main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(2, "Error: not enough arguments.\n");
        exit(1);
    }

	if (!strcmp(argv[1], "list")) {
		struct pids_info ans = over_limit();
		for (int i = 0; i < ans.count; ++i) {
			struct process_info psinfo;
            int code = ps_info(ans.pids[i], &psinfo);
            if (code < 0) {
                free(ans.pids);
				fprintf(2, "Error: ps info\n");
                exit(1);
			} else if (code == 0) {
    			print_process(&psinfo);
            }
		}

        free(ans.pids);
        exit(0);
	} else if (!strcmp(argv[1], "fork")) {
		int pid = fork();
        if (pid) {
            sleep(5);
		    printf("parent pid: %d, parent ppid: %d\n", getpid(), getppid());
        } else {
            printf("child pid: %d, child ppid: %d\n", getpid(), getppid());
        }
	} else if (!strcmp(argv[1], "clone")) {
		int pid = clone();
        if (pid) {
            sleep(5);
		    fprintf(1, "parent pid: %d, parent ppid: %d\n", getpid(), getppid());
        } else {
            fprintf(1, "child pid: %d, child ppid: %d\n", getpid(), getppid());
        }
        exit(0);
	} else {
        fprintf(2, "Error: ps unknown arg\n");
        exit(1);
    }
}
