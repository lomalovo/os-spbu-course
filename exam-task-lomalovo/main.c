#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

#define MAX_FILES 10000
#define MAX_FILENAME_LEN 200

typedef struct {
    char *filename;
    off_t size;
} FileInfo;

int compare(const void *a, const void *b) {
    FileInfo *fileA = (FileInfo *)a;
    FileInfo *fileB = (FileInfo *)b;
    return (fileA->size - fileB->size);
}

void sum(const char *filename, long *result) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file");
        exit(1);
    }

    char buffer[MAX_FILENAME_LEN];
    long sum = 0;
    while (fgets(buffer, MAX_FILENAME_LEN, file)) {
        sum += atoi(buffer);
    }
    fclose(file);
    *result = sum;
}

void process(FileInfo files[], int n, int p, int parallelism, int pipe_fd) {
    long total_sum = 0;
    int incr=0;
    for (int i = 0; i < n; i++) {
        int a = i / parallelism;
        int b = i % parallelism;
        if ((a % 2 == 0 && b == p) || (a % 2 == 1 && b == parallelism - 1 - p)) {
            if ((i+1) % parallelism == 0) incr = 1 - incr;

            long file_sum = 0;
            sum(files[i].filename, &file_sum);
            total_sum += file_sum;
        }
    }
    write(pipe_fd, &total_sum, sizeof(total_sum));
    close(pipe_fd);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_file> <parallelism>\n", argv[0]);
        exit(1);
    }

    char *input_filename = argv[1];
    int parallelism = atoi(argv[2]);

    if (parallelism < 1 || parallelism > 20) {
        fprintf(stderr, "Parallelism degree must be in [1, 20]\n");
        exit(1);
    }

    FILE *input_file = fopen(input_filename, "r");
    if (!input_file) {
        fprintf(stderr, "Error opening input file");
        exit(1);
    }

    FileInfo files[MAX_FILES];
    int file_count = 0;
    char buffer[MAX_FILENAME_LEN];

    while (fgets(buffer, MAX_FILENAME_LEN, input_file) && file_count < MAX_FILES) {
        buffer[strcspn(buffer, "\n")] = '\0';
        files[file_count].filename = strdup(buffer);

        struct stat st;
        if (stat(buffer, &st) != 0) {
            fprintf(stderr, "Error getting file size");
            exit(1);
        }
        files[file_count].size = st.st_size;
        file_count++;
    }
    fclose(input_file);

    qsort(files, file_count, sizeof(FileInfo), compare);

    int pipe_fds[parallelism][2];
    pid_t pids[parallelism];

    int files_per_process = file_count / parallelism;
    int remaining_files = file_count % parallelism;

    for (int i = 0; i < parallelism; i++) {
        pipe(pipe_fds[i]);
        if ((pids[i] = fork()) == 0) {
            close(pipe_fds[i][0]);
            int start = i * files_per_process + (i < remaining_files ? i : remaining_files);
            int end = start + files_per_process + (i < remaining_files ? 1 : 0);
            process(files, file_count, i, parallelism, pipe_fds[i][1]);
        } else {
            close(pipe_fds[i][1]);
        }
    }

    long total_sum = 0;
    for (int i = 0; i < parallelism; i++) {
        long process_sum;
        read(pipe_fds[i][0], &process_sum, sizeof(process_sum));
        total_sum += process_sum;
        close(pipe_fds[i][0]);
        waitpid(pids[i], NULL, 0);
    }

    printf("sum: %ld\n", total_sum);

    for (int i = 0; i < file_count; i++) {
        free(files[i].filename);
    }

    return 0;
}