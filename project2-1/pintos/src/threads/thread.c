#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* project1 */
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* project1 */
  list_init(&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  // initialized fd_table to 0 by memset
  memset(initial_thread->fd_table, 0, sizeof(struct file*) * 32);
  // check for valid initialization of fd_table
  //hex_dump(initial_thread->fd_table, initial_thread->fd_table, sizeof(struct file*) * 32, true);
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */

/* project1 */
void push_thread2sleep_list(void) {
  list_push_back (&sleep_list, &thread_current() -> elem);
}

// void thread_wakeup(void){
//   struct list_elem *le = list_begin(&sleep_list);
//   while(le != list_end(&sleep_list)) {
//       struct thread *th = list_entry(le, struct thread, elem);
//       //2023-03-19-15:20 Kyeonghoon
//       //else 밑에 있는것을 여기에 옮겨보았다.
//       th -> sleep_ticks--;
//       /*
//       timer_interrupt에서 tick++된 상태니까
//       thread_wakeup에서는 sleeplist에서는 tick == 0인 thread를 꺠우는 게 아니라
//       tick == 1인 thread를 깨워야함 (이미 tick은 올라갔으니까 ㅇㅇ)
//       */
//       if(th -> sleep_ticks == 0) {
//           //printf("thread %d is ")
//           le = list_remove(le);
//           thread_unblock(th);
//       }
//       else {
//           //th -> sleep_ticks--;
//           le = list_next(le);
//       }
//   }
// }

// 23-03-27 HJ
int64_t thread_wakeup(int64_t current_ticks){
  // Define next_nearest_ticks
  int64_t next_nearest_ticks = INT64_MAX;
  struct list_elem *le = list_begin(&sleep_list);
  while(le != list_end(&sleep_list)){
      struct thread *th = list_entry(le, struct thread, elem);
      int64_t th_ticks = th->base_ticks + th->sleep_ticks;
      //printf("th_ticks: %lld\n", th_ticks);
      //printf("current_ticks: %lld\n", current_ticks); 
      if(th_ticks == current_ticks) {
        //printf("Done@thread_wakeup\n");
        le = list_remove(le);
        thread_unblock(th);
      }
      else {
        // Find next_nearest_ticks to update nearest_ticks in timer.c by passing to return value
        if(th_ticks < next_nearest_ticks){
          //printf("th_ticks: %lld\n", th_ticks);
          //printf("next_nearest_ticks: %lld\n", next_nearest_ticks); 
          next_nearest_ticks = th_ticks;
        }
        le = list_next(le);
      }
  }
  // Return next_nearest_ticks
  //printf("next_nearest_ticks: %lld\n", next_nearest_ticks); 
  return next_nearest_ticks;
}

// 23-03-27 HJ
void thread_set_base_ticks (int64_t arg_ticks){
  thread_current()->base_ticks = arg_ticks;
}

// 23-03-19 HJ
void thread_set_sleep_ticks (int64_t arg_ticks){
  thread_current()->sleep_ticks = arg_ticks;
}

// 23-03-22 HJ
// Priority Scheduling에서는 Ready List에 있는 Thread을
// Priority에 맞게 깨워야한다
// Ready list에 있는 thread 중 get_max_priority
int get_max_priority(void){
  struct list_elem* curr = NULL;      // current
  struct list_elem* next = NULL;      // next
  int priority_max = 0;               // Lowest priority

  // Loop
  for(curr = list_begin(&ready_list); curr != list_end(&ready_list); curr = next){
    struct thread *th = list_entry(curr, struct thread, elem);
    next = list_next(curr);
    int priority_th = th->priority;
    //printf("priority_th: %d\n", priority_th);
    if(priority_max < priority_th) priority_max = priority_th;
  }
  
  return priority_max;
}

// get_max_priority를 통해 priority_max를 구하고
// priority_max == priority_th인 list_elem를 return (get_max_list_elem)

// ready_list를 체크해서 priority schedulling을 해야하는 경우
// 1. ready_list에서 thread를 가져올 때 next_thread_to_run
// 2. thread가 create되었을 때 thread_create
// 3. thread의 priority가 update되었을 때 thread_set_priority
struct list_elem* get_max_list_elem(void){
  struct list_elem* curr = NULL;       // current
  struct list_elem* next = NULL;       // next
  struct list_elem* ret = NULL;        // Return

  int priority_max = get_max_priority();
  //ASSERT(0 <= priority_max && priority_max <= 63);
  
  //Doesn't work
  //enum intr_level print_level;
  //print_level = intr_disable ();
  //printf("priority_max@get_max_list_elem: %d\n", priority_max);
  //intr_set_level (print_level);
  
  // Loop
  for(curr = list_begin(&ready_list); curr != list_end(&ready_list); curr = next){
    struct thread *th = list_entry(curr, struct thread, elem);
    int priority_th = th->priority;
    //printf("priority_th: %d\n", priority_th);
    if(priority_max == priority_th) {
      ret = curr;
      break;
    }
    next = list_next(curr);
  }

  // Doesn't work
  //printf("priority_max@get_max_priority: %d\n", priority_max);
  return ret;
}

/* project1 */

tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);


  // 23-03-24 HJ
  // int priority_max = get_max_priority();
  // thread->priority = priority인 thread가 create
  // 해당 값과 현재 ready_list에 있는 priority_max와 비교하여
  // priority < priority_max 일 때는 current thread를 yield해줘야함

  // 23-03-24 HJ
  // 그 thread는 이미 thread_unblock을 통해 ready_list로 들어갔고
  // 지금은 current thread의 priority와 방금 생성된 thread가 포함된 ready_list의 priority를 비교한다음에
  // thread_yield를 하면 된다

  //printf("new_priority@thread_create: %d\n", t->priority);
  //printf("current_priority@thread_create: %d\n", thread_current()->priority);
  //printf("priority_max@thread_create: %d\n", priority_max);
  
  //if(thread_current()->priority < priority_max){
  //  thread_yield();
  //}

  /* 23-03-27 Kyeonghoon
    thread_current()->priority는 t가 만들어 지기전 priority가 가장 높은 thread였다.
    그래서 t-> priority와 thread_current()->priority를 비교해서 t-> priority가 더 크면
    yield 하고 같거나 더 작으면 yield안해도 된다.
  */
  
  if(thread_current()->priority < t -> priority){
    thread_yield();
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
  int priority_max = get_max_priority();
  // Current thread의 priority가 new_priority로 변경됨
  // 변경된 값과 현재 ready_list에 있는 priority_max와 비교하여
  // new_priority < priority_max 일 때는 current thread를 yield해줘야함

  //printf("new_priority@thread_set_priority: %d\n", new_priority);
  //printf("priority_max@thread_set_priority: %d\n", priority_max);
  
  if(new_priority < priority_max){
    thread_yield();
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else {
    struct list_elem* le = get_max_list_elem();
    list_remove(le);
    return list_entry(le, struct thread, elem);
  }
  // else {
  //   //enum intr_level old_level;
  //   //old_level = intr_disable ();
  //   struct list_elem* curr;
  //   struct list_elem* next;
  //   struct list_elem* ret;
  //   int priority_max = 0;
  //   for(curr = list_begin(&ready_list); curr != list_end(&ready_list); curr = next){
  //     struct thread* th = list_entry(curr, struct thread, elem);
  //     next = list_next(curr);
  //     if(priority_max < th->priority) priority_max = th->priority;
  //   }
  //   for(curr = list_begin(&ready_list); curr != list_end(&ready_list); curr = next){
  //     struct thread* th = list_entry(curr, struct thread, elem);
  //     next = list_next(curr);
  //     if(priority_max == th->priority) {
  //         ret = curr;
  //     }
  //   }
  //   //intr_set_level (old_level);
  //   list_remove(ret);
  //   return list_entry(ret, struct thread, elem);
  // }
  //else
    //return list_entry (list_pop_front (&ready_list), struct thread, elem);

  // 23-03-22 HJ
  // else {
  //     // return get_max_thread();
  //     // Need to remove this thread from the ready_list
  //     // Need to get list_elem
  //     struct list_elem* le = get_max_list_elem();
  //     struct thread *th = list_entry(le, struct thread, elem);
  //     printf("th->status: %dn\n", th->status);
  //     printf("th->priority: %d\n", th->priority);
  //     list_remove(le);
  //     return th;
  // }

  // 23-03-24 HJ
  //else {
    //struct list_elem* le = get_max_list_elem();
    //struct thread *th = list_entry(le, struct thread, elem);
    //printf("th->status: %dn\n", th->status);
    //printf("th->priority: %d\n", th->priority);
    //list_remove(le);
    //return list_entry(le, struct thread, elem);
  //}
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
