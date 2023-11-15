#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "threads/palloc.h"  // palloc function, enum

#include "vm/page.h"  // support_page

struct semaphore frame_table_lock;

// See frame.txt

// Referred from vaddr.h
// Frame table이 실제 physical memory 부분을 구현하는 건 줄 알고 vaddr.h보고 적어봤는데 
// Manual보고 frame_table의 역할을 알게되니 굳이...?
// #define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

// #define FO_SHIFT        0                               // First offset bit
// #define FO_BITS         12                              // Number of offset bits
// #define FO_SIZE         (1 << FO_BITS)                  // 4KB
// #define FO_MASK         BITMAST(FO_SHIFT, FO_BITS)      // Frame offset bits (0:12)

// #define FN_BITS         20
// #define FN_SIZE         (1 << FN_SIZE)

typedef struct _frame{
    void* page;             // Page
    void* addr;             // page_fault address == addr
    enum palloc_flags flag;
    struct list_elem frame_elem;
    //_frame(void* p, enum palloc_flags f) : page(p), flag(f) {}  // Initialization

    /* Second-chance Algorithm */
    uint32_t *pagedir;  // Need to use function, pagedir_is_access
    support_page* sp;   // Need to store support_page => Just use support_page_get
}frame;

struct list frame_table;
struct list_elem* iterator;

// Referred from palloc.h and palloc.c
void frame_table_init(void);                        // Init frame_table

frame* frame_init(void);                            // Init frame
void frame_set(frame*, void*, enum palloc_flags, uint32_t*, void*, support_page*);   // Set frame
void frame_free(frame *);                           // Free frame
frame* frame_get(void*);                            // Find frame from frame_table
void* frame_get_addr2page(void* addr);
void* palloc_get_page_wf(enum palloc_flags, void*, support_page*);        // palloc_wrapper for frame
void palloc_free_page_wf(void*);


/* Second-chance Algorithm */
frame* find_frame_victim(void);
void swap(void);

/*victim 찾을때도 겹치면 안될것 같아서*/
struct semaphore find_victim;
struct semaphore palloc_at_once;

#endif /* vm/frame.h */
