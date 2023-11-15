#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

//  unknown type name ‘bool’
#include <stdbool.h>

/* Project 2-2 For semaphore */
#include "threads/synch.h"
struct semaphore filesys_at_once;
void filesys_at_once_init(void);

void syscall_init (void);

bool check_addr2usermem(void* addr);
void access_addr2usermem(void* addr);
int load_file2fd(const char* file);
void external_exit(int status);
void external_close(int fd);

#endif /* userprog/syscall.h */
