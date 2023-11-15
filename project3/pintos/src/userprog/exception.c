#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "syscall.h"

/* project 3 */
#include "vm/page.h"             // support_page
#include "threads/vaddr.h"       // PGSIZE
#include "threads/palloc.h"      // palloc_flags
#include "vm/frame.h"            // frame


/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);
static bool check_addr2stack(void* fault_addr, void* esp);
static bool stack_grow(void* fault_addr);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
    case SEL_UCSEG:
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{  
  // use wrapper function of sys_exit()
  //external_exit(-1);
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

   //if(!not_present) external_exit(-1);

   struct thread* t = thread_current();
   // page_fault가 kernel mode에서 발생했으면 esp = 현재 thread에 저장된 user2kernel_esp
   // page_fault가 user_mode에서 발생했으면 따로 해줄 필요 없이 interrupt frame에서 가져다가 쓰면 된다
   // check_add2stack에서 사용
   uint32_t* esp = t->user2kernel ? t->user2kernel_esp : f->esp;
   if(!is_user_vaddr(fault_addr)) external_exit(-1);

   // 4.2 Suggested Order of implementation @Stanford Manual
   // For now, consider only valid access
   support_page* sp;
   // Find support_page
   sp = support_page_get((uint32_t)fault_addr & ~PGMASK);
   //printf("sp@page_fault: %x\n", sp);
   // 현재 support_page가 set은 되어있지만 physical memory로 load 되어있지 않을 때 (sp->load == false)
   if(sp != NULL && sp->load == false){
      bool success = support_page_load(sp);
      sp->load = success;  // support_page를 load하는 데 성공했는지에 따라서 sp->load를 update
      //if(success) return;
   }
   // 4.3.3. Stack Growth (When fault_addr exceeds f->esp within 32bytes)
   else if(check_addr2stack(fault_addr, esp)){
      bool success = stack_grow(fault_addr);
      //sp->load = success;
   }
   // use wrapper function of sys_exit()
   else {
      external_exit(-1);
      // /* To implement virtual memory, delete the rest of the function
      // body, and replace it with code that brings in the page to
      // which fault_addr refers. */
      // printf ("Page fault at %p: %s error %s page in %s context.\n",
      //          fault_addr,
      //          not_present ? "not present" : "rights violation",
      //          write ? "writing" : "reading",
      //          user ? "user" : "kernel");
      // kill (f);
   }
}

// 4.3.3. Stack Growth @Stanford Pintos
// fault_addr must be in usermem (addr != NULL && is_user_vaddr(addr))
// However, the 80x86 PUSH instruction checks access permissions before it adjusts the stack pointer, so it may cause a page fault 4 bytes below the stack pointer.
//Similarly, the PUSHA instruction pushes 32 bytes at once, so it can fault 32 bytes below the stack pointer.
// 32 byte = 2^5 * 2^3 bits => (1<<8)
// absolute limit on stack size: 8MB
// 8MB = 2^3 * 2^10 * 2^10 * 2^3 => (1 << 26)
static bool check_addr2stack(void* fault_addr, void* esp){
   return check_addr2usermem(fault_addr) && fault_addr >= (esp - (1<<8)) && fault_addr >= (PHYS_BASE - (1<<26));
}

// 4.3.3. Stack Growth @Stanford Pintos
// The first stack page need not be allocated lazily. You can allocate and initialize it with the command line arguments at load time, with no need to wait for it to be faulted in.
// Referred to original version of setup_stack() and original version of load_segment
static bool stack_grow(void* fault_addr){
   //~= setup_stack
   support_page* sp = support_page_init();
   if(sp == NULL) return false;
   // fault_addr의 page_number은 fault_addr & ~PGMASK
   support_page_set(sp, NULL, 0, (uint32_t)fault_addr & ~PGMASK, 0, 0, true);

   //bool success = support_page_load(sp);

   uint8_t *kpage;
   bool success = false;

   //~= setup_stack
   //kpage = palloc_get_page (PAL_USER | PAL_ZERO);
   //printf("stack_grow: %x\n", (uint32_t)fault_addr & ~PGMASK);  // 여긴 아니고
   kpage = palloc_get_page_wf (PAL_USER | PAL_ZERO, (uint32_t)fault_addr & ~PGMASK, sp);
   if(kpage != NULL){
      success = external_install_page(sp->page_num, kpage, sp->write);
      if(!success){ 
        // palloc_free_page (kpage);
        palloc_free_page_wf (kpage);
        return false; 
      }
   }
   return success;
}

bool external_stack_grow(void* addr) {
   return stack_grow(addr);
}
bool external_check_addr2stack(void* addr, void* esp){
   struct thread *t = thread_current();
   esp = t->user2kernel ? t->user2kernel_esp : esp;
   return check_addr2stack(addr, esp);
}