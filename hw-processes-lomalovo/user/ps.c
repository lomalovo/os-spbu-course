#include "kernel/types.h"
#include "user/user.h"
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

void print_about_process(struct process_info* proc_info) {
    printf("name: %s\n", proc_info->name);
	printf("state: %s\n", ToString(proc_info->state));
	printf("parent id: %d\n", proc_info->parent_id);
    printf("memory: %d\n", proc_info->memory);
    printf("open files: %d\n", proc_info->open_files);
    printf("ticks started: %d\n", proc_info->ticks.ticks_started);
    printf("ticks cpu: %d\n", proc_info->ticks.ticks_cpu);
    printf("ticks user: %d\n", proc_info->ticks.ticks_user);
    printf("ticks kernel: %d\n", proc_info->ticks.ticks_kernel);
    printf("ticks ready: %d\n", proc_info->ticks.ticks_ready);
    printf("context switches: %d\n", proc_info->ticks.context_switches);

    printf("read from fd: %d\n", proc_info->file_descr.read_fd);
    printf("write to fd: %d\n", proc_info->file_descr.write_fd);
    printf("pages: %d\n", proc_info->file_descr.pages);

	printf("\n");
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

int main(int argc, char* argv[]){
    if (argc != 2) {
        printf("Usage: ps <command>\n");
        exit(1);
    }
    if (!strcmp(argv[1], "count")) {
        int count = ps_list(0, (int*)0);
        if (count < 0) {
            printf("Error: ps count - something wrong\n");
            exit(1);
        }
        printf("Amount of processes: %d\n", count);
    }
    else if (!strcmp(argv[1], "pids")) {
        struct pids_info ans = over_limit();
        printf("Total processes: %d\n", ans.count);
        for (int i = 0; i < ans.count; i++) {
            printf("%d\n", ans.pids[i]);
        }
        free(ans.pids);
    }
    else if(!strcmp(argv[1], "list")) {
        struct pids_info ans = over_limit();

		for (int i = 0; i < ans.count; ++i) {
			struct process_info proc_info;
            if (ps_info(ans.pids[i], &proc_info) == -2) {
				printf("Error: ps list - something wrong\n");
                exit(1);
			} else if (ps_info(ans.pids[i], &proc_info) == 0)
            {   
                print_about_process(&proc_info);
            }
            
		}
    }
    return 0;
}