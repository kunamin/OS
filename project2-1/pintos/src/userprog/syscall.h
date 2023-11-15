#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

//  unknown type name ‘bool’
#include <stdbool.h>

void syscall_init (void);

bool check_addr2usermem(void* addr);
void access_addr2usermem(void* addr);
int load_file2fd(const char* file);
void external_exit(int status);

#endif /* userprog/syscall.h */
