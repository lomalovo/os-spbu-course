#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#define DIRSIZ 14

#define SIZE 10

struct Node {
    int data;
    struct Node* next;
};

struct HashSet {
    struct Node** array;
};

// Initialize HashSet
void initializeHashSet(struct HashSet* set) {
    set->array = (struct Node**)malloc(SIZE * sizeof(struct Node*));
    for (int i = 0; i < SIZE; i++) {
        set->array[i] = 0;
    }
}

// Hash function
int hashCode(int key) {
    return key % SIZE;
}

// Add an element to the HashSet
void add(struct HashSet* set, int key) {
    int index = hashCode(key);

    // Create a new node
    struct Node* newNode = (struct Node*)malloc(sizeof(struct Node));
    newNode->data = key;
    newNode->next = 0;

    // If the slot is empty, add the new node
    if (set->array[index] == 0) {
        set->array[index] = newNode;
    } else {
        // Handle collision by chaining
        struct Node* current = set->array[index];
        while (current->next != 0) {
            current = current->next;
        }
        current->next = newNode;
    }
}

// Check if an element is present in the HashSet
int contains(struct HashSet* set, int key) {
    int index = hashCode(key);
    struct Node* current = set->array[index];

    while (current != 0) {
        if (current->data == key) {
            return 1; // Element found
        }
        current = current->next;
    }

    return 0; // Element not found
}

// Print the elements of the HashSet
void printHashSet(struct HashSet* set) {
    for (int i = 0; i < SIZE; i++) {
        printf("Bucket %d:", i);
        struct Node* current = set->array[i];
        while (current != 0) {
            printf(" %d", current->data);
            current = current->next;
        }
        printf("\n");
    }
}

// Free memory allocated for the HashSet
void freeHashSet(struct HashSet* set) {
    for (int i = 0; i < SIZE; i++) {
        struct Node* current = set->array[i];
        while (current != 0) {
            struct Node* next = current->next;
            free(current);
            current = next;
        }
    }
    free(set->array);
}

void du(char *path, struct HashSet *seenSet, uint64 *mem_size) {
    int fd;
    struct stat st;
    struct dirent {
        ushort inum;
        char name[DIRSIZ];
    } de;


    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "Error: cannot open %s\n", path);
        exit(1);
    }

    if (stat(path, &st) < 0) {
        fprintf(2, "Error: cannot stat %s\n", path);
        exit(1);
    }

    //fprintf(1, "%s, %d\n",path, st.type);
    
    
    if (st.type == T_FILE) {
        //fprintf(1, "FILE_FOUND\n");
        if (!contains(seenSet, st.ino)) {
            //fprintf(1, "%d\n", st.size);
            *mem_size = *mem_size + st.size;
            add(seenSet, st.ino);
        }
    } else if (st.type == T_DIR) {
        //fprintf(1, "DIR_FOUND\n");
        //fprintf(1, "2");
        char new_path[255];

        while (read(fd, &de, sizeof(struct dirent)) == sizeof(struct dirent)) {
            //fprintf(1, "3");
            //fprintf(1, "%d\n", de.inum);
            if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;

            int i = 0;
            for (int k = 0; k < strlen(path); ++k) {
                new_path[i] = path[k];
                ++i;
            }
            if (i > 0 && new_path[i - 1] != '/') {
                new_path[i] = '/';
                ++i;
            }
            for (int k = 0; (k < strlen(de.name)) && (k < DIRSIZ); ++k) {
                new_path[i] = de.name[k];
                ++i;
            }
            new_path[i] = 0;
            //fprintf(1, "%s\n", new_path);

            du(new_path, seenSet, mem_size);
        }
    }

    close(fd);
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(2, "Usage: du <directory>\n");
        exit(1);
    }

    uint64 mem_size = 0;
    struct HashSet seenSet;
    initializeHashSet(&seenSet);

    du(argv[1], &seenSet, &mem_size);
    fprintf(1, "Memory used: %d\n", mem_size);
    exit(0);
}
