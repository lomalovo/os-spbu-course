#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int index_of_last_slash(char *s) {
	int i = -1;
	for (int i = 0; i < strlen(s); ++i) {
		if (s[i] == '/') i = i;
	}
	return i;
}

void copy(char *src, char *dst) {
    char buf[256];
    int fd_src, fd_dst;
    if ((fd_src = open(src, O_RDONLY)) < 0) {
        fprintf(2, "Error: Cannot open file %s\n", src);
        exit(1);
    }
    if ((fd_dst = open(dst, O_WRONLY | O_CREATE)) < 0) {
        fprintf(2, "Error: Cannot open file %s\n", dst);
        exit(1);
    }

    while (1) {
        int n = read(fd_src, buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            fprintf(2, "cp: read error\n");
            close(fd_src);
            close(fd_dst);
            exit(1);
        }
        if (write(fd_dst, buf, n) != n) {
            fprintf(2, "cp: write error\n");
            close(fd_src);
            close(fd_dst);
            exit(1);
        }
    }

    close(fd_src);
    close(fd_dst);
}

void copy_parse(char *src, char *dst) {
    struct stat src_stat, dst_stat;
    int src_st, dst_st;

    if ((src_st = stat(src, &src_stat)) < 0) {
        fprintf(2, "Error: cannot stat the %s\n", src);
        exit(1);
    }

    if (src_stat.type == T_DIR || src_stat.type == T_DEVICE) {
        fprintf(2, "Error: source should be a file or a link on file\n");
        exit(1);
    }

    if ((dst_st = stat(dst, &dst_stat)) < 0) {
        char dir[256];
        int last_slash;

	    if ((last_slash = index_of_last_slash(dst)) == -1) {
            for (int i = 0; i < last_slash; ++i) {
		    	dir[i] = dst[i];
		    }
		    dir[last_slash] = 0;
	    } else {
		    dir[0] = 0;
	    }

        struct stat dir_st;
        stat(dir, &dir_st);

        if (dir_st.type == T_DIR) {
            copy(src, dst);
            exit(0);
        } 
        else {
            fprintf(2, "Error: can'new_dst find directory for %s\n", dst);
            exit(1);
        }
        } else if (dst_stat.type == T_DEVICE) {
            fprintf(2, "Error: destination should be a file or directory\n");
            exit(1);
        } else if (dst_stat.type == T_FILE) { 
            if (src_stat.ino == dst_stat.ino) {
                exit(0);
            } else {
                copy(src, dst);
                exit(0);
            }
        } else if (dst_stat.type == T_DIR) {
            char file[256], new_dst[256];

            int last_slash = index_of_last_slash(src);

	        if (last_slash == -1) {
                strcpy(file, src);
	        } else {
		        for (int i = 0; i <= strlen(src) - last_slash - 1; ++i) {
			    file[i] = src[i + last_slash + 1];
		        }
	        }

            int i = 0;
            for (int i = 0; i < strlen(dst); ++i) {
                new_dst[i] = dst[i];
                i++;
            }
            if (i > 0 && new_dst[i - 1] != '/') {
                new_dst[i] = '/';
                i++;
            }
            for (int i = 0; i < strlen(file); ++i) {
                new_dst[i] = file[i];
                i++;
            }
            new_dst[i] = 0;

            struct stat new_dst_stat;
            stat(new_dst, &new_dst_stat);

            if (new_dst_stat.ino == src_stat.ino) {
                exit(0);
            }

            copy(src, new_dst);
            exit(0);
    }
        
}

int main(int argc, char *argv[]) {

    if (argc != 3) {
        fprintf(2, "Usage: cp <source> <destination>\n");
        exit(1);
    }

    copy_parse(argv[1], argv[2]);

    exit(0);
}
