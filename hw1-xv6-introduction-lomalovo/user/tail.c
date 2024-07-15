#include "kernel/types.h"
#include "user/user.h"

#define MAX_LINE_SIZE 256

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

void tail_bytes(int fd, int n){
    char *buf = (char *)malloc(n);
    int bytesRead = 0;
    int bytesReadLastTime = 0;
    while ((bytesRead = read(fd, buf, n)) > 0) {
        bytesReadLastTime = bytesRead;
    }
    for (int i = 0; i < n; i++){
        fprintf(1, "%c", buf[(bytesReadLastTime + i)%n]);
    }
    free(buf);
    return;
}

void tail_lines(int fd, int n){
    int buffSize = n*MAX_LINE_SIZE;
    char *buf = (char *)malloc(buffSize);
    int bytesRead = 0;
    int bytesReadLastTime = 0;
    int linesCount = 0;
    int indexOfNLines = 0;
    while ((bytesRead = read(fd, buf, buffSize)) > 0) {
        bytesReadLastTime = bytesRead;
    }
    for (int i = 0; i < buffSize; i++){
        if (buf[(bytesReadLastTime + buffSize - i)%buffSize] == '\n'){
            linesCount++;
        }
        if (linesCount == n){
            indexOfNLines = (bytesReadLastTime + buffSize - i + 1)%buffSize;
            break;
        }
    }
    int lenOfNLines = (bytesReadLastTime - indexOfNLines + buffSize)%buffSize;

    for (int i = 0; i < lenOfNLines + 1; i++) {
        fprintf(1, "%c", buf[(indexOfNLines + i)%buffSize]);
    }
    
    
    free(buf);
    return;
}

int main(int argc, char *argv[]) {
    int fd = 0;
    int n = 10; // Default to 10 lines
    int size = 0; // 0 for lines, 1 for bytes
    int data = 0;

    if (argc > 3) {
        fprintf(2, "Usage: more [--lines=N | --bytes=N] [file]\n");
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
    if (fd == 0) {
        fprintf(2, "Error: No file\n");
        exit(1);
    }
    if (size == 1){
        tail_bytes(fd, n);
    } else {
        tail_lines(fd, n);
    }
    close(fd);
    return 0;
}

