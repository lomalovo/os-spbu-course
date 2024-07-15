#include "types.h"

struct write_call_info {
    int fd;         
    char *addr;    
    int n;          
    char *buffer;   
};