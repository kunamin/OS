#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void command2argv(const char *command, void** esp);
void argv2stack(void** esp, char** argv, int argc);

bool external_install_page(void *, void *, bool);

#endif /* userprog/process.h */