// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define PAGES 512 * 32
#define NODES (2 * PAGES - 1)
#define DEPTH 15

extern char end[];

enum node_state {
    NOTEXIST = 0,
    USEDNODE = 1,
    INNERNODE = 2,
    FREENODE = 3
};

struct node {
    enum node_state state;
    int id;
    int size;
    int lvl;  
    struct node* left_child;
    struct node* right_child;
    struct node* prev;
    struct node* next;
    struct node* parent;
    struct node* neighbour;
    char* memory;
};

struct {
    struct spinlock lock;
    struct node nodes[NODES];
    struct node* lists[DEPTH];
    int sizes[DEPTH];
} buddy_metadata;


void add_free_node(struct node* n) {
    buddy_metadata.sizes[n->lvl]++;
    n->next = buddy_metadata.lists[n->lvl];
    buddy_metadata.lists[n->lvl] = n;
    n->prev = 0;
    if (n->next != 0) {
        n->next->prev = n;
    }
}

void print_cur_info() {
    int sizes[10], free = 0;
    int i = 0;
    while (i < 10) {
        sizes[i] = buddy_metadata.sizes[i];
        free += (buddy_metadata.sizes[i] << i);
        i++;
    }
    while (i < DEPTH) {
        sizes[9] += (buddy_metadata.sizes[i] << (i - 9));
        free += (buddy_metadata.sizes[i] << i);
        i++;
    }
    printf("used = %d, free = %d, sizes: ", PAGES - free, free);
    for (int i = 0; i < 9; ++i) {
        printf("%d, ", sizes[i]);
    }
    printf("%d\n", sizes[9]);
}

void buddy_init() {
    initlock(&buddy_metadata.lock, "buddy_mem");
    int idx = 0;
    while (idx < DEPTH) {
        buddy_metadata.lists[idx] = 0;
        buddy_metadata.sizes[idx] = 0;
        idx++;
    }

    struct node* current_node = &buddy_metadata.nodes[0];
    current_node->state = FREENODE;
    current_node->id = 0;
    current_node->size = PAGES;
    current_node->lvl = DEPTH - 1;
    current_node->left_child = &buddy_metadata.nodes[1];
    current_node->right_child = &buddy_metadata.nodes[2];
    current_node->prev = 0;
    current_node->next = 0;
    current_node->parent = current_node;
    current_node->neighbour = current_node;
    current_node->memory = (char*)PGROUNDUP((uint64)end);
    add_free_node(&buddy_metadata.nodes[0]);

    int id = 1;
    while (id < NODES) {
        current_node = &buddy_metadata.nodes[id];
        struct node* parent = &buddy_metadata.nodes[(id - 1) / 2];
        current_node->state = NOTEXIST; 
        current_node->id = id;
        current_node->lvl = parent->lvl - 1;
        current_node->size = parent->size / 2;
        
        if (id < NODES / 2) {
            current_node->left_child = &buddy_metadata.nodes[2 * id + 1];
            current_node->right_child = &buddy_metadata.nodes[2 * id + 2];
        } else {
            current_node->left_child = 0;
            current_node->right_child = 0;
        }
        
        current_node->prev = 0;
        current_node->next = 0;

        current_node->parent = parent;
        if (id % 2 == 1) {
            current_node->neighbour = &buddy_metadata.nodes[id + 1];
        } else {
            current_node->neighbour = &buddy_metadata.nodes[id - 1];
        }
        
        if (id % 2 == 1) {
            current_node->memory = parent->memory;
        } else {
            current_node->memory = parent->memory + current_node->size * PGSIZE;
        }

        id++;
    }
}


void buddy_free(void *pa) {
    if (pa == 0 || ((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP) { 
        panic("buddy_free");
    }

    acquire(&buddy_metadata.lock);

    struct node* current_node = &buddy_metadata.nodes[0];
    while (current_node->state == INNERNODE) {
        if (current_node->right_child->memory > (char*)pa) {
            current_node = current_node->left_child;
        } else {
            current_node = current_node->right_child;
        }
    }
    
    if (current_node->state != USEDNODE || current_node->memory != (char*)pa) {
        panic("buddy_free");
    }

    if (current_node->id == 0) {
        add_free_node(current_node);
        current_node->state = FREENODE;
        release(&buddy_metadata.lock);
        return;
    }
    
    while (current_node->id && current_node->neighbour->state == 3) {
        current_node->state = NOTEXIST;
        current_node->neighbour->state = NOTEXIST;

        if (current_node->neighbour->prev) {
            current_node->neighbour->prev->next = current_node->neighbour->next;
        } else {
            buddy_metadata.lists[current_node->neighbour->lvl] = current_node->neighbour->next;
        }

        if (current_node->neighbour->next)
            current_node->neighbour->next->prev = current_node->neighbour->prev;

        buddy_metadata.sizes[current_node->neighbour->lvl]--;
        current_node->neighbour->prev = 0;
        current_node->neighbour->next = 0;
        current_node = current_node->parent;
    }

    add_free_node(current_node);
    current_node->state = FREENODE;
    release(&buddy_metadata.lock);
}

int is_deg_2(int n) {
    if (n <= 0 || n > 512) {
        return -1;
    }

    int pow2 = 1, lvl = 0;
    while (pow2 < n) {
        pow2 *= 2;
        lvl++;
    }
    
    if (n != pow2) {
        return -1;
    } else {
        return lvl;
    } 
}

void* buddy_alloc(int n) {
    int lvl = is_deg_2(n);
    if (lvl == -1) {
        return 0;
    }

    acquire(&buddy_metadata.lock);

    int split_lvl = -1;
    int idx = lvl;
    while (idx < DEPTH) {
        if (buddy_metadata.sizes[idx] > 0) {
            split_lvl = idx;
            break;
        }
        idx++;
    }

    if (split_lvl == -1) {
        printf("There are no free node for alloc\n");
        release(&buddy_metadata.lock);
        return 0;
    }

    struct node* current_node = buddy_metadata.lists[split_lvl];

    if (current_node->prev) {
        current_node->prev->next = current_node->next;
    } else {
        buddy_metadata.lists[current_node->lvl] = current_node->next;
    }

    if (current_node->next)
        current_node->next->prev = current_node->prev;
    buddy_metadata.sizes[current_node->lvl]--;

    current_node->prev = 0;
    current_node->next = 0;
    while (current_node->lvl > lvl) {
        current_node->state = INNERNODE;
        add_free_node(current_node->right_child);
        current_node->right_child->state = FREENODE;
        current_node = current_node->left_child;
    }
    current_node->state = USEDNODE; 
    
    release(&buddy_metadata.lock);

    return current_node->memory;
}
