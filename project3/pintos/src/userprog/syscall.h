#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

//  unknown type name ‘bool’
#include <stdbool.h>

/* Project 2-2 For semaphore */
#include "threads/synch.h"
struct semaphore filesys_at_once;
struct semaphore using_at_once;

// project 3
#include "vm/page.h"

void filesys_at_once_init(void);

void syscall_init (void);

bool check_addr2usermem(void* addr);
void access_addr2usermem(void* addr);
int load_file2fd(const char* file);
void external_exit(int status);
void external_close(int fd);

void external_unmap(mapid_t mapid);

void update_dirty_page(uint32_t* pagedir, support_page* sp);
void update_dirty_page_without_sp_free(uint32_t* pagedir, support_page* sp);

#endif /* userprog/syscall.h */
