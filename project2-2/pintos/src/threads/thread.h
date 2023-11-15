#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* Project 2-2 For semaphore */
#include "threads/synch.h"


/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* File Descriptor Table */
/* 
Stanford PintOS 3.4.2. Syscall FAQ
Can I set a maximum number of open files per process? 
It is better not to set an arbitrary limit. You may impose a limit of 128 open files per process, if necessary.
*/
#define FD_MAX 128

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

    /* project1 */
    int64_t base_ticks;
    int64_t sleep_ticks;

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
    /*
    Stanford PintOS: Each process has an independent set of file descriptors
    maximum elements(32) is set intuitively.
    => new variable to struct thread -> initialize it in init_thread()
    */
    struct file* fd_table[FD_MAX];
    // status about whether the child process is exit successfully or not
    int success_exit;

    // status about whether the child process is loaded successfully or not
    int success_load;

    // Process wait에서 child process가 제대로 exec되고 나서는 parent의 parent_alive를 semaup해줘야하기때문에
    // Parent에 접근할 수 있어야한다.
    struct thread* parent;  // parent thread
    // Process_wait에서 특정 child_tid가 exit할 때까지 wait해야하기때문에
    // child_tid가 일치하는 child thread를 찾기위해서 children을 탐색해야한다.
    struct list hanging_children;
    // For struct list hanging_children
    struct list_elem hanging_child;

    // See. Semaphore정리 at syscall.c
    // 1. wait: If pid is still alive, waits until it terminates
    struct semaphore child_not_exit;
    // 2. exec: The parent cannot return from the exec until it knows whether the child process successfully loaded its executables
    struct semaphore child_not_load;

    // MKH
    // 3. child가 exit가 될때 sema_up을 하지만 아직 wait부분에서 sema_down을 하지 않아서 생긴 문제 에서 발생.
    // process_wait에서 success_exit을 받아올때 까지 기다려야 할 듯해서 semaphore 하나 더 선언.
    struct semaphore child_not_give_to_succ_ex;

   // used at void sys_exit(int status)
    bool wait_exit;  // exit: success + wait for exit by parent
    bool wait_exit_done;  // exit: success + exit allowed by parent

#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

/* project1 */
void push_thread2sleep_list (void);
//void thread_wakeup (void);
int64_t thread_wakeup(int64_t current_ticks);
void thread_set_base_ticks (int64_t arg_ticks);
void thread_set_sleep_ticks (int64_t arg_ticks);
int get_max_priority(void);
struct list_elem* get_max_list_elem(void);
/* project1 */

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void thread_set_name(const char* name);

#endif /* threads/thread.h */
