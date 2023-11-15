#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

// Additional header
#include "threads/vaddr.h"
#include "devices/shutdown.h"

// Additional typedef
typedef int pid_t;

static void syscall_handler (struct intr_frame *);
static void sys_halt(void);
static void sys_exit(int status);
static pid_t exec_command(const char* command);
static int wait_pid(pid_t pid);
static bool create_file(const char* file, unsigned initial_size);
static bool remove_file(const char* file);
static int get_filesize(int fd);
static int read_fd2buffer(int fd, void* buffer, unsigned size);
static int write_buffer2fd(int fd, const void* buffer, unsigned size);
static void change_offset(int fd, unsigned position);
static unsigned tell_offset(int fd);
static void sys_close(int fd);

void filesys_at_once_init(void){
  // only 1 process can enter to filesys which is regareded as critical section
  sema_init(&filesys_at_once, 1);
}

void
syscall_init (void) 
{
  filesys_at_once_init();
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* 
  @Stanford PintOS Manual
  Thus, when the system call handler syscall_handler() gets control, the system call number is in the 32-bit word at the caller's stack pointer
  , the first argument is in the 32-bit word at the next higher address, and so on. 
  The caller's stack pointer is accessible to syscall_handler() as the esp member of the struct intr_frame passed to it. (struct intr_frame is on the kernel stack.)
  */

  /*
  @Project_2-1.pdf posted at blackboard
  For # of arguments is
  1: argument is located in esp+1
  2: arguments are located in esp+4 (arg0), esp+5 (arg1)
  3: arguments are located in esp+5 (arg0), esp+6 (arg1), esp+7 (arg2)
  */

  uint32_t* esp = f->esp;
  //hex_dump(esp, esp, 100, true);
  int sysnum;
  access_addr2usermem(esp);
  sysnum = *esp;  // void* f->esp
  //printf("@syscall_handler-sysnum: %d\n", sysnum);
  //esp+=1;
  //printf("esp: %p\n", esp);
  
  // sysnum == system call number
  // Compare with enumerate located in lib/syscall-nr.h
  // 20 enumerate => switch-case
  switch(sysnum) {                          /* Comment convention: enum. (# of arguments) functionality */
    case SYS_HALT:                          /* 0. (0) Halt the operating system. */
      sys_halt();
      break;
    case SYS_EXIT:                          /* 1. (1) Terminate this process. */
      // hex_dump(esp, esp, 100, true);
      // printf("1st: %x\n", esp + 1);
      
      // check if the address of arguments is valid or not
      access_addr2usermem(esp + 1);
      sys_exit(*(esp + 1));  // Call sys_exit
      break;
    case SYS_EXEC:                          /* 2. (1) Start another process. */
      access_addr2usermem(esp + 1);
      f->eax = exec_command(*(esp + 1));  // exec_command
      break;
    case SYS_WAIT:                          /* 3. (1) Wait for a child process to die. */
      access_addr2usermem(esp + 1);
      f->eax = wait_pid(*(esp + 1));  // wait_pid
      break;
    case SYS_CREATE:                        /* 4. (2) Create a file. */
      // hex_dump(esp, esp, 100, true);
      // printf("1st: %x\n", esp + 4);
      // printf("2nd: %x\n", esp + 4 + 1);

      // check if the address of arguments is valid or not
      access_addr2usermem(esp + 4);  
      access_addr2usermem(esp + 4 + 1);

      // Call sys_create
      // Store the return value @f->eax
      f->eax = create_file(*(esp + 4), *(esp + 4 + 1));
      break;
    case SYS_REMOVE:                        /* 5. (1) Delete a file. */
      access_addr2usermem(esp + 1);
      f->eax = remove_file(*(esp + 1));
      break;
    case SYS_OPEN:                          /* 6. (1) Open a file. */
      // hex_dump(esp, esp, 100, true);
      // printf("1st: %x\n", esp + 1);

      // check if the address of argument is valid or not
      access_addr2usermem(esp + 1);

      f->eax = load_file2fd(*(esp + 1));  // Call load_file2fd
      break;
    case SYS_FILESIZE:                      /* 7. (1) Obtain a file's size. */
      // check if the address of argument is valid or not
      access_addr2usermem(esp + 1);
      f->eax = get_filesize(*(esp + 1));  // Call get_filesize
      break;
    case SYS_READ:                          /* 8. (3) Read from a file. */
      access_addr2usermem(esp + 4 + 1);
      access_addr2usermem(esp + 4 + 2);
      access_addr2usermem(esp + 4 + 3);

      f->eax = read_fd2buffer(*(esp + 4 + 1), *(esp + 4 + 2), *(esp + 4 + 3));  // Call sys_read
      break;
    case SYS_WRITE:                         /* 9. (3) Write to a file. */
      // hex_dump(esp, esp, 40, true);
      // printf("1st: %x\n", esp + 4 + 1);
      // printf("2st: %x\n", esp + 4 + 2);
      // printf("3st: %x\n", esp + 4 + 3);

      // check if the address of arguments is valid or not
      access_addr2usermem(esp + 4 + 1);
      access_addr2usermem(esp + 4 + 2);
      access_addr2usermem(esp + 4 + 3);
      //printf("1: esp+4+1: %x\n", esp + 4);
      //printf("2: esp+4+2: %x\n", esp + 4 + 1);
      //printf("3: esp+4+3: %x\n", esp + 4 + 2);
      
      f->eax = write_buffer2fd(*(esp + 4 + 1), *(esp + 4 + 2), *(esp + 4 + 3));  // Call sys_write
      //printf("%d\n", f->eax);
      break;
    case SYS_SEEK:                          /* 10. (2) Change position in a file. */
      access_addr2usermem(esp + 4);  
      access_addr2usermem(esp + 4 + 1);

      change_offset(*(esp + 4), *(esp + 4 + 1));
      break;
    case SYS_TELL:                          /* 11. (1) Report current position in a file. */
      access_addr2usermem(esp + 1);
      f->eax = tell_offset(*(esp + 1));
      break;
    case SYS_CLOSE:                         /* 12. (1) Close a file. */
      // hex_dump(esp, esp, 40, true);
      // printf("1st: %x\n", esp + 1);

      // check if the address of argument is valid or not
      access_addr2usermem(esp + 1);

      sys_close(*(esp + 1));  // Call sys_close
      break;
  }
}

bool check_addr2usermem(void* addr){
  // Stanford PintOS: "look at pagedir.c and threads/vaddr.h"
  // is_user_vaddr is check whether vaddr < PHYS_BASE;
  return addr != NULL && is_user_vaddr(addr);
  // 1. addr should not equal to NULL (Because it will be dereferenced later)
  // 2. addr should be user memory (addr < PHYS_BASE)
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
  
  sema_down(&filesys_at_once);
  struct file* open_fd = filesys_open(file);

  // open-empty test
  if(open_fd == NULL) {
    sema_up(&filesys_at_once);
    return -1;
  }
  
  /*
  open_fd를 했으니 그 file을 thread->fd_table에 load해야함 (load file2fd)
  Stanford PintOS
  fd_idx == 0, 1 are reserved for STDIN_FILENO and STDOUT_FILENO, respectively
  근데 file descriptor wikipedia에는 2까지 STDERR_FILENO으로 reserved
  */


  //printf("%d", strcmp(thread_current()->name, open_fd));
  // rox-simple, rox-multi, rox-multichild는 현재 열려있는 파일이 현재 실행되는 파일이고
  // 현재 실행중인 파일이 open되어 write되는 걸 막아야함
  // tests/userprog/rox series source code랑 output보면 됨!!
  // Stanford Manual 3.3.5
  // Denying writes to executables (현재 실행중인 파일에 write 못하도록 할 때 file_deny_write()쓰라고 나와있음!!)
  // thread_current()->name == file => file_deny_write!!
  if(strcmp(thread_current()->name, file) == 0){
    file_deny_write(open_fd);
  }
  
  //bool success_open = false;
  int fd_idx;
  struct thread* t = thread_current();
  for(fd_idx = 3; fd_idx < FD_MAX; fd_idx++){
    if(t->fd_table[fd_idx] == NULL){
      //success_open = true;
      t->fd_table[fd_idx] = open_fd;
      break;
    }
  }
  // If all file descriptor is reserved by other files, then sys_exit(-1)
  
  sema_up(&filesys_at_once);
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
  //sema_down(&t->child_success_exit);
  // whether the child process is exit successfully or not
  t->success_exit = status;
  printf("%s: exit(%d)\n", t->name, status);  // struct thread char name[16];
  // the parent stil waits for the exit of the child, then the child should notify the parent that "Now, I'm exit"
  sema_up(&t->child_not_exit);
  // store t->wait_exit = true (But t->wait_exit_done is stil false (this is set to true after the parent retrieve the exit status of the child))
  t->wait_exit = true;
  // The parent must retrieve the exit status of the child, then the child must wait for notification from the parent that "Now, I'm retrieve the exit status of the child"
  sema_down(&t->child_not_give_to_succ_ex);
  // store t->wait_exit_done = true  
  t->wait_exit_done = true;
  // Finally, the child process can be exited by calling function "thread_exit()"
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

static bool create_file(const char* file, unsigned initial_size){
  //printf("argument1@sys_create: %p\n", file);
  //printf("argument2@sys_create: %d\n", initial_size);
  // create-null test
  access_addr2usermem(file);
  // TEST create-null
  //if(file == NULL) sys_exit(-1);  // create-null test // access_addr2usermem이 check해줌
  //printf("file: %s\n", file);
  sema_down(&filesys_at_once);
  bool success = filesys_create(file, initial_size);
  sema_up(&filesys_at_once);
  return success;
}

static bool remove_file(const char* file){
  // Logic is similar to create_file(file, initial_size)
  // The only difference is the function called from filesys.c
  // /filesys/filesys.c
  // bool filesys_remove (const char *name);
  access_addr2usermem(file);
  sema_down(&filesys_at_once);
  bool success = filesys_remove(file);
  sema_up(&filesys_at_once);
  return success;
}

// 쓸만한 함수가 없나 하면서 file.c, filesys.c 함수 찾다가
// file.c에서 file_length 함수 발견
static int get_filesize(int fd){
  if(fd < 0 || fd >= FD_MAX) sys_exit(-1);
  else{  // 0 <= fd || fd < FD_MAX
    int file_size;
    struct thread* t = thread_current();
    // filesize를 구할 file이 현재 current thread의 file descriptor table[fd]에 있어야하는데 없으면 실패
    if(t->fd_table[fd] == NULL) return -1;
    //off_t file_length (struct file *file) 
    sema_down(&filesys_at_once);
    file_size = file_length(t->fd_table[fd]);
    sema_up(&filesys_at_once);
    return file_size;
  }
}

// @Project_2-2.pdf posted at blackboard에 나온 순서대로 구현중 ㅇㅇ
// read_fd2buffer()는 write_buffer2fd()와 유사
static int read_fd2buffer(int fd, void* buffer, unsigned size){
  // invalid fd
  access_addr2usermem(buffer);
  if(fd < 0 || fd >= FD_MAX) sys_exit(-1);

  // Can't read this file descriptor (fd == 1: STDOUT_FILENO and fd == 2: STDERR_FILENO)
  else if(fd == 1 || fd == 2) return -1;

  // STDIN_FILENO == fd => input_getc()
  else if(fd == 0){
    /*
    Stanford Pintos
    fd == 0: input_getc()
    */
    int read_size;
    sema_down(&filesys_at_once);
    read_size = input_getc();
    sema_up(&filesys_at_once);
    return read_size;
  }

  else{  // 2 < fd && fd < FD_MAX
    int read_size;
    struct thread* t = thread_current();
    // Read할 file이 현재 current thread의 file descriptor table[fd]에 있어야하는데 없으면 실패
    if(t->fd_table[fd] == NULL) return -1;
    //off_t file_read (struct file *file, void *buffer, off_t size) 
    sema_down(&filesys_at_once);
    read_size = file_read(t->fd_table[fd], buffer, size);
    sema_up(&filesys_at_once);
    return read_size;
  }
}

// args_single에서 sys_write debugging해보면 sys_write (size=13, buffer=0x1, fd=13) 이렇게 넘어오는데
// Stanford pintOS를 보면 each process has an independent set of file descriptors라고 적혀있는걸 보니
// file_descriptor 부분 배우고 구현해야할듯

// 저기서 말하는 set of file descriptors 부분이 file descriptor table 인듯
static int write_buffer2fd(int fd, const void* buffer, unsigned size){
  //printf("argument1@sys_write: %d\n", fd);
  //printf("argument2@sys_write: %s\n", buffer);
  //printf("argument3@sys_write: %d\n", size);
  access_addr2usermem(buffer);
  // invalid fd
  if (fd < 0 || fd >= FD_MAX) sys_exit(-1);
  
  // Can't write this file descriptor (fd == 0: STDIN_FILENO and fd == 2: STDERR_FILENO)
  else if(fd == 0 || fd == 2) return -1;
  
  // STDOUT_FILENO == fd => print at command line
  else if(fd == 1){
    /*
    Stanford PintOS
    fd == 1: putbuf
    */
    sema_down(&filesys_at_once);
    putbuf(buffer, size);
    sema_up(&filesys_at_once);
    return size;
  }
  else {  // 2 < fd && fd < FD_MAX
    int written_size;
    struct thread* t = thread_current();
    // Write할 file이 현재 current thread의 file descriptor table[fd]에 있어야하는데 없으면 실패
    if(t->fd_table[fd] == NULL) return -1;
    //off_t file_write (struct file *file, const void *buffer, off_t size) 
    sema_down(&filesys_at_once);
    written_size = file_write(t->fd_table[fd], buffer, size);
    sema_up(&filesys_at_once);
    return written_size;
  }
}

/*
seek past the current end of a file is not an error. A later read obtains 0 bytes,
indicating end of file. A later write extends the file, filling any unwritten gap with
zeros. (However, in Pintos files have a fixed length until project 4 is complete, so
writes past end of file will return an error.) These semantics are implemented in the
file system and do not require any special effort in system call implementation.
*/
static void change_offset(int fd, unsigned offset){
  // invalid fd
  if(fd < 0 || fd >= FD_MAX) sys_exit(-1);
  
  // find file from fd_table[fd] of the current thread
  struct thread* t = thread_current();

  // invalid fd_table[fd]
  if(t->fd_table[fd] == NULL) sys_exit(-1);

  //처음 구현했을 때는 직접 t->fd_table[fd]에 접근해서 pos를 offset으로 assign하도록 구현했는데,
  //Compile Error: error: dereferencing pointer to incomplete type @t->fd_table[fd]->pos = offset;
  //file.c에 file_seek() 함수가 있어서 function call로 대체
  //t->fd_table[fd]->pos = offset;
  sema_down(&filesys_at_once);
  file_seek(t->fd_table[fd], offset);
  sema_up(&filesys_at_once);
}

static unsigned tell_offset(int fd){
  if(fd < 0 || fd >= FD_MAX) sys_exit(-1);

  struct thread* t = thread_current();

  if(t->fd_table[fd] == NULL) sys_exit(-1);
  
  //처음 구현했을 때는 직접 t->fd_table[fd]에 접근해서 offset = pos하고 return offset하도록 구현했는데,
  //Compile Rrror: dereferencing pointer to incomplete type @unsigned offset = f->pos;
  //file.c 파일 찾아보니까 file_tell() 함수가 있어서 function call로 대체
  //unsigned offset = t->fd_table[fd]->pos;
  sema_down(&filesys_at_once);
  unsigned offset = file_tell(t->fd_table[fd]);
  sema_up(&filesys_at_once);
  return offset;
}

// Implement wrapper function of sys_close()
// sys_close를 static function으로 선언했는데 thread.c 부분에서 써야해서 wrapper function 방법 이용
void external_close(int fd){
  sys_close(fd);
}

static void sys_close(int fd){
  // close-bad-fd TEST
  if(fd < 0 || fd >= FD_MAX) sys_exit(-1);

  struct thread* t = thread_current();
  // close-null TEST
  if(t->fd_table[fd] == NULL) sys_exit(-1);
  sema_down(&filesys_at_once);
  file_close(t->fd_table[fd]);
  sema_up(&filesys_at_once);
  // Close 했으니 t->fd_table[fd] = NULL을 넣어주면 같은 파일 close again할 때 t->fd_table[fd] == NULL라서 sys_exit(-1)
  // close-twice TEST
  t->fd_table[fd] = NULL;
}

static pid_t exec_command(const char* command){
  // invalid command일 때는 process_execute -> start_process -> load에서 처리가 되는 듯? (goto done -> file_close -> return success)
  access_addr2usermem(command);
  //printf("execute command: %s\n", command);
  sema_down(&filesys_at_once);
  pid_t pid = process_execute(command);
  sema_up(&filesys_at_once);
  //printf("pid: %d\n", pid);
  return pid;
}

static int wait_pid(pid_t pid){
  // Stanford Pintos
  //printf("wait pid: %d\n", pid);
  // implement the wait system call in terms of process_wait
  int status = process_wait(pid);
  return status;
}


/*
exec_command
the parent process cannot return from the exec until it knows whether the child process successfully loaded its executables
=> You must use appropriate synchronization to ensure this
Parent일떄는 child process가 exec될 때까지 wait (pid of child is valid)
*/

/*
semaphore 정리
Stanford Manual
1. wait: If pid is still alive, waits until it terminates
2. exec: The parent cannot return from the exec until it knows whether the child process successfully loaded its executables
따라서 semaphore가 2개 필요하다

*/

/*
Synchronization 정리
1. Open:  Parent, Child, or other process가 같은 file을  open해도 그 Thread가 가진 unique한 fd_table에서 알아서 관리되기떄문에 문제가 없다
2. Close: 현재 Current thread의 fd_table[fd]를 close하는 거라서 문제가 없을듯?
=> fd_table[fd]에다가 file을 assign하거나 null로 바꾸는 거는 current_thread의 fd_table에 bound되기 때문에 Sync를 생각할 필요가 없다.

3, 4 read, write: 얘네는 current_thread->fd_table[fd]로 file에 접근해서 file의 내용을 read하고 write하는 거라서
=> 여러 개 process가 동일한 struct *file에 대해서 read하고 write를 시도한다면 consistency 문제가 발생
=> Need to implement more to maintain consistency
=> 사용할 수 있는 선택지가 Mutex, lock, semaphore

5. create: create만 하는거라서 no matter what
6. remove: remove만 하는거라서 no matter what

=> 인 줄 알았으나,
Stanford Manual
You must synchronize system calls so that any number of user processes can make them at once. 
In particular, it is not safe to call into the file system code provided in the filesys directory from multiple threads at once. 
Your system call implementation must treat the file system code as a critical section. 
Don't forget that process_execute() also accesses files. 
For now, we recommend against modifying code in the filesys directory.

=> Filesys를 위한 semaphore
syscall.h semaphore
*/

/*
thread/synch.c && thread/synch.h에 lock이랑 semaphore가 구현되어있음
void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);
*/

/*
이 부분은 구현하면서 본 적 없는 거 같은데, Manual에 나와있으니까 필요한 부분일 수도 있겟다 생락해서 읽어봤음
3.3.5 Denying Writes to Executables
근데 it can't hurt even now라고 적혀있여서 굳이?
내용 자체는 
file_deny_write(): Prevent writes to an open files (다른 process가 쓰고 있으면 write할 수 없게 ~= lock, semaphore이랑 같은 느낌)
file_allow_write(): Allow ~
*/

/*
/filesys/file.c
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
/filesys/filesys.c
void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);
*/