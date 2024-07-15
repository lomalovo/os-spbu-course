#include "kernel/types.h"
#include "user/user.h"

#define BUFFER_SIZE 256

void head(int fd, int n, int size) {
    char *buf = (char *)malloc(BUFFER_SIZE);
    int bytesRead = 0;
    int totalBytesRead = 0;
    int totalLinesRead = 0;
    
    while ((bytesRead = read(fd, buf, BUFFER_SIZE)) > 0) {
        for (int i = 0; i < bytesRead; ++i) {

            if (size == 1) {
                totalBytesRead++;
            } else if (size == 0 && buf[i] == '\n') {
                
                totalLinesRead++;
                
            }

            fprintf(1, "%c", buf[i]);

            if ((size == 1 && totalBytesRead >= n) ||
                (size == 0 && totalLinesRead >= n)) {
                
                close(fd);
                free(buf);
                return;
            }
        }
    }
    free(buf);
}

int is_flag(char* arg, char* flag) {
    if (strlen(arg) <= strlen(flag)) {
        return -1;
    }
    for (int i = 0; i < strlen(flag); i++) {
        if (arg[i] != flag[i]) {
            return -1;
        }
    }
    return (int)atoi(arg + strlen(flag));
}

int main(int argc, char *argv[]) {
    int fd = 0; // Default to stdin
    int n = 10; // Default to 10 lines
    int size = 0; // 0 for lines, 1 for bytes
    int data = 0;

    if (argc > 3) {
        fprintf(2, "Usage: head [--lines=N | --bytes=N] [file]\n");
        exit(1);
    }

    int option_flag = 0;
    int file_flag = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-') {
            if (option_flag) {
                fprintf(2, "Error: Only one option possible\n");
                exit(1);
            }

            if (file_flag) {
                fprintf(2, "Usage: head [--lines=N | --bytes=N] [file]\n");
                exit(1);
            }

            if ((data = is_flag(argv[i], "--lines=")) > 0) {
                size = 0;
                n = data;
            } else if ((data = is_flag(argv[i], "--bytes=")) > 0) {
                size = 1;
                n = data;
            } else {
                fprintf(2, "Error: Unknown option %s\n", argv[i]);
                exit(1);
            }
            option_flag = 1;

        } else {
            if ((fd = open(argv[i], 0)) < 0) {
                fprintf(2, "Error: Cannot open file %s\n", argv[i]);
                exit(1);
            }

            file_flag = 1;
        }
    }
    head(fd, n, size);
    close(fd);
    exit(0);
}
