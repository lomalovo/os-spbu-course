#include "kernel/types.h"
#include "user/user.h"
#include "kernel/riscv.h"
#include "kernel/write_call_info.h"

void print_pt_elem(uint64 elem, int ind) {
    const char *flags[] = {"READ", "WRITE", "EXECUTE", "USER MODE"};
    uint64 masks[] = {PTE_R, PTE_W, PTE_X, PTE_U};
    int num_flags = sizeof(flags) / sizeof(flags[0]);

    if (elem & PTE_V) {
        printf("%d %x %x", ind, elem, PTE2PA(elem));
        for (int i = 0; i < num_flags; i++) {
            if (elem & masks[i]) {
                printf(", %s", flags[i]);
            }
        }
        printf("\n");
    }
}


void print_memory_dump(const void *buffer, int size) {
    const unsigned char *buf = (const unsigned char *)buffer;
    for (int i = 0; i < size; i += 16) {
        char str[17];
        printf("%d: ", i);

        for (int j = 0; j < 16; j++) {
            if (i + j < size) {
                unsigned char b = buf[i + j];
                str[j] = (b >= 32 && b < 127) ? b : '.';
                printf("%02x ", b);
            } else {
                printf("   ");
                str[j] = ' ';
            }
        }
        str[16] = 0;
        printf(" |%s|\n", str);
    }
}

void
main(int argc, char *argv[]) {
	if (argc < 3 || argc > 6) {
		printf("ps: incorrect number of arguments!\n");
		exit(1);
	}
	if (!strcmp(argv[1], "pt")) {
		int pid = atoi(argv[3]);
		uint64* table = malloc(PGSIZE);
		if (!strcmp(argv[2], "0")) {
			if (ps_pt0(pid, table) < 0) {
				printf("ps pt: something went wrong!\n");
				free(table);
				exit(1);
			}
		} else if (!strcmp(argv[2], "1")) {
			uint64 addr = atoi(argv[4]);
			if (ps_pt_1(pid, (void*)addr, table) < 0) {
				printf("ps pt: something went wrong!\n");
				free(table);
				exit(1);
			}
		} else if (!strcmp(argv[2], "2")) {
			uint64 addr = atoi(argv[4]);
			if (ps_pt_2(pid, (void*)addr, table) < 0) {
				printf("ps pt: something went wrong!\n");
				free(table);
				exit(1);
			}
		} else {
			printf("ps pt: incorrect level!\n");
			free(table);
			exit(1);
		}
		int level = atoi(argv[2]);
		for (int i = 0; i < PGSIZE / sizeof(uint64); ++i) {
			uint64 elem = table[i];
			if (PTE_V & elem) {
				print_pt_elem(elem, i);
			} else if (((argc == 5 && level == 0) || (argc == 6 && level > 0)) && !strcmp(argv[argc - 1], "-v")) {
				printf("%d %x INVALID\n", i, elem);
			} else if ((argc == 5 && level == 0) || (argc == 6 && level > 0)) {
				printf("ps pt: incorrect last argument!\n");
				free(table);
				exit(1);
			}
		}
		free(table);
		exit(0);
	} else if (!strcmp(argv[1], "dump")) {
		if (argc != 5) {
			printf("ps dump: incorrect number of arguments!\n");
			exit(1);
		}

		int pid = atoi(argv[2]);
		uint64 addr = atoi(argv[3]);
		int size = atoi(argv[4]);

		char* data = malloc(size);
		if (ps_copy(pid, (void*)addr, size, (char*)data) < 0) {
			printf("ps copy: something went wrong!\n");
			free(data);
			exit(1);
		}
		for (int i = 0; i < size; ++i) {
			printf("%x\n", data[i]);
		}
		free(data);
		exit(0);
	} else if (!strcmp(argv[1], "sleep")) {
		int pid = atoi(argv[2]);
		struct write_call_info info;
		info.buffer = malloc(512);

		int sleep = ps_sleep_on_write(pid, (void*)&info);
		if (sleep == 0) {
			printf("fd: %d, addr: %d, n: %d\n", info.fd, info.addr, info.n);
			print_memory_dump(info.buffer, info.n);
		} else if (sleep == 1) {
			printf("Proccess not sleeping or sleep not after write\n");
		} else if (sleep < 0){
			printf("ps sleep: something went wrong!\n");
			free(info.buffer);
			exit(1);
		}
		free(info.buffer);
		exit(0);
	} else {
		printf("ps: incorrect arguments!\n");
		exit(1);
	}
}
