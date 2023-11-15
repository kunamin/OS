#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

// Additional header
#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static void sys_halt(void);
static void sys_exit(int status);
static bool sys_create(const char* file, unsigned initial_size);
static int sys_write(int fd, const void* buffer, unsigned size);
static void sys_close(int fd);
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //printf ("system call!\n");
  //thread_exit();

  /* Stannford PintOS
  Thus, when the system call handler syscall_handler() gets control, the system call number is in the 32-bit word at the caller's stack pointer
  , the first argument is in the 32-bit word at the next higher address, and so on. 
  The caller's stack pointer is accessible to syscall_handler() as the esp member of the struct intr_frame passed to it. (struct intr_frame is on the kernel stack.)
  */

  /*
  Information about enum SYS_xxxx is in /lib/syscall-nr.h 
  Each syscall arguments are stored on the stack
  */

  /*
  ------------------------------------------------------------
  Comment about esp
  */

  // esp를 자주쓸 거 같아서 그냥 변수로 선언

  // 자꾸 sysnum == 9가 되길래 debugging해봤는데
  // 첫번째는 sysnum == 9이고 그 다음이 test하고자하는 sysnum이 들어가서 돌아감
  // 첫번째는 printf랑 관련된 syscall인거 같아서 fd == 1이 되어야하는데 fd = *(esp + 1)을 하면 0xd가 들어가서 error가 일어남

  // => 첫 syscall의 sysnum == 9(sys_write)인 이유는 "(TEST_NAME) begin"를 command_line에 print해야하기 때문이고
  // command line에 print하기 때문에 fd == 1 (putbuf())이어야한다.

  /*
    halt test 기준으로
    Executing 'halt':
    bfffff40                                      09 00 00 00 |            ....|
    bfffff50  0d 00 00 00 01 00 00 00-0d 00 00 00 bd 81 04 08 |................|
    bfffff60  01 00 00 00 c0 bb 04 08-0d 00 00 00 b4 ff ff bf |................|
    bfffff70  00 00 00 00 00 00 00 00-c8 a6 04 08 e0 a6 04 08 |................|
    bfffff80  00 00 00 00 00 00 00 00-00 00 00 00 00 00 00 00 |................|
    bfffff90  00 00 00 00 00 00 00 00-c0 ff ff bf e3 81 04 08 |................|
    bfffffa0  00 00 00 00 00 00 00 00-00 00 00 00 c7 80 04 08 |................|
    fd = *(esp+1) == 1이 될 수 있는 곳이 esp = 0xbfffff50랑 esp = 0xbfffff5c인데

    esp = 0xbfffff50 했을때
    @syscall_handler-sysnum: 9
    1: esp+4: bfffff54
    2: esp+8: bfffff58
    3: esp+12: bfffff5c
    argument1@sys_write: 1
    argument3@sys_write: 134513085
    Page fault at 0xd: not present error reading page in kernel context.

    esp = 0xbfffff5c 했을 때
    @syscall_handler-sysnum: 9
    1: esp+4: bfffff60
    2: esp+8: bfffff64
    3: esp+12: bfffff68
    argument1@sys_write: 1
    argument3@sys_write: 13
    (halt) begin
    로 제대로 돌아감 (정확하게 왜 memory가 이런지는 과제 끝나고 더 공부해봐야할듯)
    =>
    argument 개수마다 arg0의 위치가 어디인지 하나씩 디버깅하면서 arg 개수에 따른 arg0의 위치가
    1(exit, open, close): esp + 1
    2(create): esp + 4, esp + 5
    3(write): esp + 4 + 1, esp + 4 + 2, esp 4 + 3 이어야 syscall이 잘 돌아가서 그렇다는 건 알아냈고

    => 
    조교님이 올려주신 PintOS project 2-1 Manual에서 arg 개수에 따른 arg0의 상대적인 위치를 알려주주셔서 해결했음
    (정확하게 왜 그런지는 과제 기한 끝나고 조교님께 여쭤볼 예정)
     For # of arguments is
      • 1: argument is located in esp+1
      • 2: arguments are located in esp+4 (arg0), esp+5 (arg1)
      • 3: arguments are located in esp+5 (arg0), esp+6 (arg1), esp+7 (arg2)
 */


  uint32_t* esp = f->esp;
  //hex_dump(esp, esp, 100, true);
  int sysnum;
  access_addr2usermem(esp);
  sysnum = *esp;  // void* f->esp
  //printf("@syscall_handler-sysnum: %d\n", sysnum);
  //esp+=1;
  //printf("esp: %p\n", esp);
  
  // sysnum이 int로 *esp해서 구해지고 그 value를 syscall_nr.h에 있는 enumerate와 비교해서 sysnym에 맞는 syscall를 호출
  // if elseif else 하기엔 syscall_nr.h에 enum이 20개나 있으니까 그냥 case로 구현
  // syscall 부분에서 어떤 걸 해야하는지는 Stanford PintOS Manual을 참고
  switch(sysnum) {
    case SYS_HALT:                   /* (0) Halt the operating system. */
      sys_halt();
      break;
    case SYS_EXIT:                   /* (1) Terminate this process. */
      // hex_dump(esp, esp, 100, true);
      // printf("1st: %x\n", esp + 1);

      // check if the address of argument is valid or not
      access_addr2usermem(esp + 1);

      // Call sys_exit
      sys_exit(*(esp + 1));
      break;
    case SYS_CREATE:                 /* (2) Create a file. */
      // static bool sys_create(const char* file, unsigned initial_size)
      // hex_dump(esp, esp, 100, true);
      // printf("1st: %x\n", esp + 4);
      // printf("2nd: %x\n", esp + 4 + 1);

      // check if the address of arguments is valid or not
      // esp += 4;  // 디버깅으로 알아낸 argument 위치
      access_addr2usermem(esp + 4);
      access_addr2usermem(esp + 4 + 1);

      // Call sys_create
      // Store the return value @f->eax
      f->eax = sys_create(*(esp + 4), *(esp + 4 + 1));
      break;
    case SYS_OPEN:                   /* (1) Open a file. */
      // hex_dump(esp, esp, 100, true);
      // printf("1st: %x\n", esp + 1);

      // check if the address of argument is valid or not
      access_addr2usermem(esp + 1);

      // Call load_file2fd
      f->eax = load_file2fd(*(esp + 1));
      break;
    case SYS_WRITE:                  /* (3) Write to a file. */
      // static int sys_write(int fd, const void* buffer, unsigned size)
      //printf("%d\n", f->eax);
      // hex_dump(esp, esp, 40, true);
      // printf("1st: %x\n", esp + 4 + 1);
      // printf("2st: %x\n", esp + 4 + 2);
      // printf("3st: %x\n", esp + 4 + 3);

      // check if the address of arguments is valid or not
      // esp += 4;  // 디버깅으로 알아낸 argument 위치
      access_addr2usermem(esp + 4 + 1);
      access_addr2usermem(esp + 4 + 2);
      access_addr2usermem(esp + 4 + 3);
      //printf("1: esp+4+1: %x\n", esp + 4);
      //printf("2: esp+4+2: %x\n", esp + 4 + 1);
      //printf("3: esp+4+3: %x\n", esp + 4 + 2);
      
      // Call sys_write
      // Store the return value @f->eax
      f->eax = sys_write(*(esp + 4 + 1), *(esp + 4 + 2), *(esp + 4 + 3));
      //printf("%d\n", f->eax);
      break;
    // case SYS_SEEK:                   /* Change position in a file. */
    //   break;
    // case SYS_TELL:                   /* Report current position in a file. */
    //   break;
    case SYS_CLOSE:                  /* Close a file. */
      // hex_dump(esp, esp, 40, true);
      // printf("1st: %x\n", esp + 1);

      // check if the address of argument is valid or not
      access_addr2usermem(esp + 1);

      // Call sys_close
      sys_close(*(esp + 1));
      break;
  }
}

bool check_addr2usermem(void* addr){
  // Stanford PintOS: "look at pagedir.c and threads/vaddr.h"
  // is_user_vaddr is check whether vaddr < PHYS_BASE;
  return addr != NULL && is_user_vaddr(addr);
}

// check_addr2usermem()의 return value가 false가 되었을 때 terminates(sys_exit(-1))할 수 있어야함
// 이걸 syscall_handler 안에서 check_addr2usermem 쓸 때마다 if~~ sys_exit하기엔 너무 자주쓸 거 같아서
// 함수로 wrap
void access_addr2usermem(void* addr){
  if(!check_addr2usermem(addr)) sys_exit(-1);
}

// 인자로 fild을 받아서 current thread의 fd_table에 load하는 함수 
// static으로 선언하지 않고 전역으로 선언함
int load_file2fd(const char* file){
  //printf("argument1@load_file2fd: %s\n", file);
  //check_addr2usermem(file);

  // argument로 들어온 건 const char* file (file_name)이니까 check_addr2usermem
  // 여기서는 return -1 해야하니까 access_addr2usermem 말고 if + check_addr2usermem
  // open-null test
  if(!check_addr2usermem(file)) return -1;
  
  struct file* open_fd = filesys_open(file);
  
  // open-empty test
  if(open_fd == NULL) return -1;
  
  /*
  open_fd를 했으니 그 file을 thread->fd_table에 load해야함 (load file2fd)
  Stanford PintOS
  fd_idx == 0, 1 are reserved for STDIN_FILENO and STDOUT_FILENO, respectively
  근데 file descriptor wikipedia에는 2까지 STDERR_FILENO으로 reserved
  */
  int fd_idx;
  struct thread* t = thread_current();
  for(fd_idx = 3; fd_idx < FD_MAX; fd_idx++){
    if(t->fd_table[fd_idx] == NULL){
      t->fd_table[fd_idx] = open_fd;
      break;
    }
  }
  // return fd_idx if valid fd_idx
  // return -1(err) if invalid fd_idx
  return fd_idx;
  //return (fd_idx ? 2 < fd_idx && fd_idx < FD_MAX : -1); => for문 시작과 끝이 3 ~ FD_MAX -1 이라서 이 조건문을 필요 X
}

// Implement wrapper function of sys_exit()
// sys_exit를 static function으로 선언했는데 page_fault 부분에서 써야해서 wrapper function 방법 이용
void external_exit(int status){
  sys_exit(status);
}

static void sys_halt(void){
  // Stanford PintOS: "shutdown_power_off"
  shutdown_power_off();
}

void sys_exit(int status){
  // exit에서 status를 인자로 넘겨주는 이유가 있었음
  // test랑 output difference를 보니 "program_name: exit(status)"을 출력해야함
  struct thread* t = thread_current();
  printf("%s: exit(%d)\n", t->name, status);  // struct thread char name[16];
  thread_exit();
}

/*
create-bad-ptr test
이 test에서는 *file이 이상한 value가 들어가서 filesys_create함수에서 page fault가 발생
argument1@sys_create: 20101234
argument2@sys_create: 
이 경우에 page fault가 발생하고 Kernel panic이 발생한다
tests/userprog/create-bad-ptr.ck를 보면 page fault가 발생하지 않고 exit(-1)이 되는 상황인데
Stanford PintOS에서 exception.c and exception.h 3.1.1. Source Files를 봤을 때
"Some, but not all, solutions to project 2 requires modifying page_fault() in this file"이라고 적힌 걸 보니
page_fault()를 수정해서 (지금은 page_fault() 처리하는 코드를 구현하지 않았으니)
page_fault() 다 실행되기 전에 sys_exit(-1)를 call하면 page_fault()->Kernel Panic이 page_fault()->sys_call(-1)로 flow하지 않을까
*/

static bool sys_create(const char* file, unsigned initial_size){
  // filesys 파일 찾아보다가 filesys_create 함수 발견
  // file.c =>
  //printf("argument1@sys_create: %p\n", file);
  //printf("argument2@sys_create: %d\n", initial_size);
  // create-null test
  access_addr2usermem(file);
  // TEST create-null
  //if(file == NULL) sys_exit(-1);  // create-null test // access_addr2usermem이 check해줌
  //printf("file: %s\n", file);
  bool success = filesys_create(file, initial_size);
  return success;
}

// args_single에서 sys_write debugging해보면 sys_write (size=13, buffer=0x1, fd=13) 이렇게 넘어오는데
// Stanford pintOS를 보면 each process has an independent set of file descriptors라고 적혀있는걸 보니
// file_descriptor 부분 배우고 구현해야할듯

// 저기서 말하는 set of file descriptors 부분이 file descriptor table 인듯
static int sys_write(int fd, const void* buffer, unsigned size){
  //printf("argument1@sys_write: %d\n", fd);
  //printf("argument2@sys_write: %s\n", buffer);
  //printf("argument3@sys_write: %d\n", size);
  
  // invalid fd
  if (fd < 0 || fd >= FD_MAX) sys_exit(-1);
  
  // Can't write this file descriptor (fd == 0: STDIN_FILENO and fd == 2: STDERR_FILENO)
  else if(fd == 0 || fd == 2) return -1;
  
  // STDOUT_FILENO == fd => print at command line
  else if(fd == 1){
    /*
    Stanford PintOS
    fd == 0: putbuf
    */
    putbuf(buffer, size);
    return size;
  }
  // 2 < fd && fd < FD_MAX
  else {
    int written_size;
    struct thread* t = thread_current();
    // Write할 file이 현재 current thread의 file descriptor table[fd]에 있어야하는데 없으면 실패
    if(t->fd_table[fd] == NULL) return -1;
    //off_t file_write (struct file *file, const void *buffer, off_t size) 
    written_size = file_write(t->fd_table[fd], buffer, size);
    return written_size;
  }
}

/*
(close-twice) begin
(close-twice) open "sample.txt"
(close-twice) close "sample.txt"
(close-twice) close "sample.txt" again
*/
static void sys_close(int fd){
  // close-bad-fd TEST
  if(fd < 0 || fd >= FD_MAX) sys_exit(-1);

  struct thread* t = thread_current();
  // close-null TEST
  if(t->fd_table[fd] == NULL) sys_exit(-1);
  file_close(t->fd_table[fd]);
  // Close 했으니 t->fd_table[fd] = NULL을 넣어주면 같은 파일 close again할 때 t->fd_table[fd] == NULL라서 sys_exit(-1)
  // close-twice TEST
  t->fd_table[fd] = NULL;
}

/*
file.c
struct file *file_open (struct inode *);
struct file *file_reopen (struct file *);
void file_close (struct file *);
struct inode *file_get_inode (struct file *);

off_t file_read (struct file *, void *, off_t);
off_t file_read_at (struct file *, void *, off_t size, off_t start);
off_t file_write (struct file *, const void *, off_t);
off_t file_write_at (struct file *, const void *, off_t size, off_t start);

void file_deny_write (struct file *);
void file_allow_write (struct file *);

void file_seek (struct file *, off_t);
off_t file_tell (struct file *);
off_t file_length (struct file *);
*/

/*
filesys.c
void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);
*/