#pragma once
#include "types.h"
#include "proc.h"

struct process_info {
    enum procstate state;
    int parent_id;
    uint64 memory;
    int open_files;
    char name[16];
    struct ticks ticks;
    struct file_descr file_descr;
};