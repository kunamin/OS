#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* project 3 */
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);


/*
230421 HJ
run_task
(gdb) print file_name
$1 = 0xc0007d50 "args-none"

(gdb) print file_name
$4 = 0xc0109000 "args-none"

load (esp=0xc010afa0, eip=0xc010af94, file_name=0xc0109000 "args-none")

PintOS 3.4 FAQ에서 examples/echo 써서 확인하면 될 듯? (지금 args_many 테스트케이스 돌리면 시작도 안하고 끝나는 중;;)
3.5.1에서 나온 예제인 '/bin/ls -l foo bar' 써서

pintos --filesys-size=2 -p ../examples/echo -a echo --gdb -- -q -f run '/bin/ls -l foo bar' 
기준으로 함수 실행 순서랑 file_name에 어떤 값 들어있는지 확인해보면

run_task
-> process_execute
    (gdb) print file_name
    $1 = 0xc0007d50 "/bin/ls -l foo bar"
-> start_process
    (gdb) print file_name
    $2 = 0xc0109000 "/bin/ls -l foo bar"
-> load
    (gdb) print file_name
    $3 = 0xc0109000 "/bin/ls -l foo bar"

*/

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char* fn_copy;
  char* fn_copy_parse;
  tid_t tid;

  /* Make a copy of FILE_NAME. Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Make a copy of FILE_NAME. Otherwise there's a race between the caller and load(). */
  fn_copy_parse = palloc_get_page (0);
  if (fn_copy_parse == NULL)
    return TID_ERROR;
  strlcpy (fn_copy_parse, file_name, PGSIZE);

  char* program_name;
  char* save_ptr;

  program_name = strtok_r(fn_copy_parse, " ", &save_ptr);

  // /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (program_name, PRI_DEFAULT, start_process, fn_copy);

  // 원래 있던 위치는 if(check_find) 뒤인데,
  // tid is invalid 인지 테스트하려면 load가 끝나야 테스트할 수 있다 => sema_down으로 load 끝날 때까지 ㄱㄷ
  struct thread* t = thread_current();
  sema_down(&(t->child_not_load));

  // tid is invalid
  if (tid == TID_ERROR){
    // free fn_copy and fn_copy_parse
    palloc_free_page (fn_copy);
    palloc_free_page (fn_copy_parse);
    return -1;  // thread_create fails
  }

  //palloc_free_page (fn_copy);
  //palloc_free_page (fn_copy_parse);
  struct list_elem* curr = NULL;
  struct list_elem* next = NULL;

  bool check_find = false;
  struct thread* child = NULL;

  for(curr = list_begin(&(t->hanging_children)); curr != list_end(&(t->hanging_children)); curr = next){
    struct thread *temp = list_entry(curr, struct thread, hanging_child);
    next = list_next(curr);
    if(temp->tid == tid){
      check_find = true;
      child = temp;
      break;
    }
  }
  // If the parent can find the child whose thread->tid == tid
  if(check_find){
    // If loading child process is failed, start_process() 함수의 if(!success) 부분에서 t->success_load = -1 하고 sema_up(&(child->child_not_load))하니까
    // parent process는 이 지점부터 시작
    // child->success_load == -1 => load 실패 => return -1
    if(child->success_load == -1){
      //child->success_exit = -1;
      return -1;
    }
  }
  // fn_copy_parse를 palloc_get 했기 때문에 free해줘야함
  // 어디서 free 해줘야하는지 생각해봤는데 exec가 끝나기 직전에는 당연히 다 사용했겠지라고 생각하고 free하니까 별 문제 없었음
  // fn_copy는 start_process에서 free됨
  if(fn_copy_parse != NULL){
    palloc_free_page (fn_copy_parse);
  }
  return tid;
}

// Argument Parsing
// 1. command 2 argv
// 2. argv 2 stack

// parameter설명
// 기본적으로 stackpointer를 --하면서 argv를 저장해야하니 esp가 필요
//    -> 생각해보니 argv construct할 때는 esp가 필요없을듯? (stack construct할 때는 필요하고 ㅇㅇ)
// parsing할 command 필요
// argv, argc 필요 (argc를 &argc로 넘기는게 될 줄 알았는데 C에서는 안되는듯)

// 3.3.3보면 argument passing하는 게 "Implement this functionality by extending process_execute() so that ~~"
// 보니까 process_execute로 가야할듯

// 23-04-26 HJ
// void** esp 추가 (이유는 argv2stack 함수 주석에)
void command2argv(const char* command, void** esp) {
  size_t len;
  char* command_copy;
  char** argv;
  int argc = 0;

  len = strlen(command) + 1;  // Including '\0'
  command_copy = malloc(sizeof(char) * len);  // 메모리 해제 완
  strlcpy(command_copy, command, len);

  char* token;
  char* save_ptr;

  // 원래 아래의 MKH's code로 사용했다가
  // multi-oom에서 command가 "multi-oom 1 " 뒤에 띄어쓰기 하나 때문에 argc가 원래 값보다 하나 커지게 되어서 아예 실행조차 되지 못한 거였다
  // 다행히도 오류 발생한 거 찾아서 HJ's code로 대체했다
  // HJ's Code
  for(token = strtok_r(command_copy, " ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr)){
    argc = argc + 1;
  }

  /*MKH's code
  int i;
  for(i = 0; i<strlen(command); i++){
      if(command[i] == ' '){
        argc++;
        if(command[i-1] == ' ') {
          argc--;
        }
      }
    }
    argc++;
  */
  //printf("command_copy@command2argv: %s\n", command_copy);
  //printf("command@command2argv: %s\n", command);
  
  // argc incrementing 후 printf(command_copy) == "args_multiples" 즉, 원래의 command가 아님 (이건 strtok_r function의 주석에도 원래 char* 바꾼다고 적혀있음 ㅇㅇ)
  // => 새로 strlcpy
  strlcpy(command_copy, command, len);

  // Stanford PintOS의 3.5.1을 보면
  // argv의 data type은 char**이고
  // argv의 char* 개수는 argc + 1개 (argv[4] == 0으로 적혀있고 fake return address라고하네)
  argv = (char**)malloc(sizeof(char*) * (argc + 1));  // 메모리 해제 완
  
  int idx;
  for(idx = 0, token = strtok_r(command_copy, " ", &save_ptr); token != NULL; idx++, token = strtok_r(NULL, " ", &save_ptr)){
    argv[idx] = token;
  }
  argv[argc] = NULL;

  // 23-04-30 HJ
  // test랑 output difference를 보니 args를 출력해야하는 부분이 있음
  // init.c의 run_action 함수에서 실행되는 거
  // 지금 안나오는 건 syscall 구현 다 안해서 그런듯
  /*
  (args) begin
  (args) argc = 5
  (args) argv[0] = 'args-multiple'
  (args) argv[1] = 'some'
  (args) argv[2] = 'arguments'
  (args) argv[3] = 'for'
  (args) argv[4] = 'you!'
  (args) argv[5] = null
  (args) end
  */
  // printf("(args) begin\n");
  // printf("(args) argc = %d\n", argc);
  // for(idx = 0; idx < argc; idx++){
  //   printf("(args) argv[%d] = '%s'\n", idx, argv[idx]);
  //   //printf("@command2argv - &argv[%d]: %p\n", idx, &argv[idx]);  // address of argument
  //   //printf("@command2argv - argv[%d]: %p\n", idx, argv[idx]);  // argument
  //   //printf("@command2argv - *argv[%d]: %s\n", idx, *argv[idx]);  // err
  // }
  // // null
  // printf("(args) argv[%d] = null\n", argc);
  // printf("(args) end\n");
  //hex_dump(0xc010510c, 0xc010510c, 0xc0105132-0xc010510c, true);  // argv[0] ~ argv[4]
  /*
  c0105100                                      61 72 67 73 |            args|
  c0105110  2d 6d 75 6c 74 69 70 6c-65 00 73 6f 6d 65 00 61 |-multiple.some.a|
  c0105120  72 67 75 6d 65 6e 74 73-00 66 6f 72 00 79 6f 75 |rguments.for.you|
  c0105130  21 00                                           |!.              |
  */
  
  //argv2stack(esp, argv, argc);

  int word_align;
  word_align = 0;
  
  char** ptr_arr;
  ptr_arr = (char **)malloc(sizeof(char*) * (argc + 1));  // 메모리 해제 완
  ptr_arr[argc] = NULL;
  //printf("size: %d", malloc_usable_size(ptr_arr));
  for(idx = argc - 1; idx >= 0; idx--){
    int len;
    len = strlen(argv[idx]) + 1;  // including '\0'
    word_align += len;
    //printf("@argv2stack - argv[%d]: %s\n", idx, argv[idx]);
    //printf("@argv2stack - esp: %s\n", esp);  // Not worth
    //printf("@argv2stack - *esp: %s\n", *esp);  // Not worth
    //printf("@argv2stack - *esp: %x\n", *esp);
    *esp -= len;
    //printf("@argv2stack - *esp: %x\n", *esp);
    //printf("%x\n", *argv[idx]);
    //printf("%d\n", **esp);
    //printf("%s\n", *argv[idx]);
    //printf("%d\n", idx);
    memcpy(*esp, argv[idx], len);  // *esp가 가리키고 있는 메모리에 argv[idx]가 가리키고 있는 메모리를 == argument값을 복사
    //printf("@argv2stack - *esp: %p\n", *esp);
    //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);
    //memcpy(ptr_arr[idx], *esp, sizeof(char *));  // memcpy는 ptr_arr[idx]기 가리키고 있는 메모리에 *esp가 가리키고 있는 메모리를 복사하면 안됨

    // 여기서 0xbfffffb 이렇게 안들어가고 0xfffffffb 이렇게 들어가길래
    //  ptr_arr = malloc(sizeof(char*) * ((*argc) + 1)); 에 char** 추가했더니 잘 들어감

    // stack 아랫부분에 0xbffffffc, 0xbffffff8 같은 애들 담아야해서 추가함
    // ptr_arr[idx]의 size는 char*이고 거기다가 그냥 *esp"값"을 넣어주면 됨
    ptr_arr[idx] = *esp;  

    //printf("@argv2stack - ptr_arr[%d]: %p\n", idx, ptr_arr[idx]);
    //ptr_arr[idx] = *esp;
    //printf("@argv2stack - pnt_arr[idx]: %x\n", pnt_arr[idx]);
    //printf("@argv2stack - *esp: %x\n", *esp);
    //*--esp = argv[idx];  // PintOS Manual 3.5에 *--sp = value
  }
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);
  
  // 23-04-27 HJ padding
  // if word_align % 4 == 0  => Do nothing
  // if word_align % 4 == 1  => *esp -= 3
  // if word_align % 4 == 2  => *esp -= 2
  // if word_align % 4 == 3  => *esp -= 1
  if(word_align % sizeof(uint8_t*)){
    *esp -= (sizeof(uint8_t*) - word_align % sizeof(uint8_t*));
  }
  
  //printf("%p\n", ptr_arr[1]);
  //printf(ptr_arr[1]);
  //printf(ptr_arr[2]);
  //printf(ptr_arr[3]);

  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  for(idx = argc; idx >= 0; idx--){
    *esp -= sizeof(char *);
    //printf("@argv2stack - ptr_arr[%d]: %p\n", idx, ptr_arr[idx]);
    //*((uintptr_t*)*esp) = ptr_arr[idx];
    memcpy(*esp, &ptr_arr[idx], sizeof(char *));  // *esp가 가리키고 있는 메모리에 &ptr_arr[idx]가 가리키고 있는 메모리를 == ptr_arr[idx]값을 복사
  }
  
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  //printf("until argv[0]\n");

  // argv (*esp + 4 == argv[0]이라서 *esp -= 4 이후에 *esp + 4값을 넣어야하는데 memory에다 써야하는데)
  // 그럼 **esp = *esp + 4 해야하는데
  // argv (Type: char**)
  *esp -= sizeof (char **);
  *((char**)*esp) = *esp + sizeof (char **);
  //**esp = *esp + 4;  // => error: dereferencing ‘void *’ pointer [enabled by default]
  // 우리가 stack 쌓을 때 **esp 못썻던 이유가 *esp가 void*인데 그걸 한 번 더 *하니까 error 걸렸던 건데
  // 그럼 *esp를 void *가 아니고 다른 자료형의 포인터(e.g. char**)로 cast시키면 어떻게 되는지 확인해봤는데 잘되네...
  // 그러면 memcpy를 쓸 이유가 없어지긴한데 삽질 레전드ㅠ
  //memcpy(*esp, *esp + 4, sizeof(char *));
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  // argc (Type: char**)
  *esp -= sizeof (int);
  *((int*)*esp) = argc;
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  // return address (Type: void (*) ()) return address니까 pointer로 casting해주면 될 거 같아서 했는데 됨 ㅇㅇ
  *esp -= sizeof(char*);
  *((char*)*esp) = NULL;
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  if(ptr_arr != NULL){
    free(ptr_arr);
  }

  if(argv != NULL){
    free(argv);
  }

  if(command_copy != NULL){
    free(command_copy);
  }
}


/*     Not used (Integrated to command2argv)    */
// command2argv -> argv2stack에서 오류가 나길래 합쳤는데 알고보니 이 문제가 아니라
// argc를 제대로 계산하지 못하는 문제였음

// esp를 void**로 받는 이유는 load랑 setup stack이 그렇게 받길래 생각해보다가
// stack pointer 자체는 void*인데
// 수정할 수 있도록 stack pointer의 pointer로 넘기기때문인듯
// 23-04-26
// 마지막 커밋날리고 계속 구현하면서 생각해본게
// 지금 문제가 command2argv에서 malloc한 argv가 function call이 끝나면서 자동으로 free되어서
// argv2stack에서 char** argv로 넘겨도 null이 넘어와서 kernel panic이 뜬다는 건데
// 그러면 argv2stack을 start_process에서 call하지 않고 command2argv에서 사용한다면 argv가 free되지 않은 상태에서 쓸 수 있는 게 아닌가?하는 생각
// 그러면 command2argv함수가 esp를 받을 수 있도록 header부분 수정해야할 듯?
void argv2stack(void** esp, char** argv, int argc) {
  int idx;
  int word_align;
  word_align = 0;
  
  char** ptr_arr;
  ptr_arr = (char **)malloc(sizeof(char*) * (argc + 1));   // Free 완료
  ptr_arr[argc] = NULL;
  //printf("size: %d", malloc_usable_size(ptr_arr));
  for(idx = argc - 1; idx >= 0; idx--){
    int len;
    len = strlen(argv[idx]) + 1;  // including '\0'
    word_align += len;
    //printf("@argv2stack - argv[%d]: %s\n", idx, argv[idx]);
    //printf("@argv2stack - esp: %s\n", esp);  // Not worth
    //printf("@argv2stack - *esp: %s\n", *esp);  // Not worth
    //printf("@argv2stack - *esp: %x\n", *esp);
    *esp -= len;
    //printf("@argv2stack - *esp: %x\n", *esp);
    //printf("%x\n", *argv[idx]);
    //printf("%d\n", **esp);
    //printf("%s\n", *argv[idx]);
    //printf("%d\n", idx);
    memcpy(*esp, argv[idx], len);  // *esp가 가리키고 있는 메모리에 argv[idx]가 가리키고 있는 메모리를 == argument값을 복사
    //printf("@argv2stack - *esp: %p\n", *esp);
    //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);
    //memcpy(ptr_arr[idx], *esp, sizeof(char *));  // memcpy는 ptr_arr[idx]기 가리키고 있는 메모리에 *esp가 가리키고 있는 메모리를 복사하면 안됨

    // 여기서 0xbfffffb 이렇게 안들어가고 0xfffffffb 이렇게 들어가길래
    //  ptr_arr = malloc(sizeof(char*) * ((*argc) + 1)); 에 char** 추가했더니 잘 들어감

    // stack 아랫부분에 0xbffffffc, 0xbffffff8 같은 애들 담아야해서 추가함
    // ptr_arr[idx]의 size는 char*이고 거기다가 그냥 *esp"값"을 넣어주면 됨
    ptr_arr[idx] = *esp;  

    //printf("@argv2stack - ptr_arr[%d]: %p\n", idx, ptr_arr[idx]);
    //ptr_arr[idx] = *esp;
    //printf("@argv2stack - pnt_arr[idx]: %x\n", pnt_arr[idx]);
    //printf("@argv2stack - *esp: %x\n", *esp);
    //*--esp = argv[idx];  // PintOS Manual 3.5에 *--sp = value
  }
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);
  
  // 23-04-27 HJ padding
  // if word_align % 4 == 0  => Do nothing
  // if word_align % 4 == 1  => *esp -= 3
  // if word_align % 4 == 2  => *esp -= 2
  // if word_align % 4 == 3  => *esp -= 1
  if(word_align % sizeof(uint8_t*)){
    *esp -= (sizeof(uint8_t*) - word_align % sizeof(uint8_t*));
  }
  
  //printf("%p\n", ptr_arr[1]);
  //printf(ptr_arr[1]);
  //printf(ptr_arr[2]);
  //printf(ptr_arr[3]);

  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  for(idx = argc; idx >= 0; idx--){
    *esp -= sizeof(char *);
    //printf("@argv2stack - ptr_arr[%d]: %p\n", idx, ptr_arr[idx]);
    //*((uintptr_t*)*esp) = ptr_arr[idx];
    memcpy(*esp, &ptr_arr[idx], sizeof(char *));  // *esp가 가리키고 있는 메모리에 &ptr_arr[idx]가 가리키고 있는 메모리를 == ptr_arr[idx]값을 복사
  }
  
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  //printf("until argv[0]\n");

  // argv (*esp + 4 == argv[0]이라서 *esp -= 4 이후에 *esp + 4값을 넣어야하는데 memory에다 써야하는데)
  // 그럼 **esp = *esp + 4 해야하는데
  // argv (Type: char**)
  *esp -= sizeof (char **);
  *((char**)*esp) = *esp + sizeof (char **);
  //**esp = *esp + 4;  // => error: dereferencing ‘void *’ pointer [enabled by default]
  // 우리가 stack 쌓을 때 **esp 못썻던 이유가 *esp가 void*인데 그걸 한 번 더 *하니까 error 걸렸던 건데
  // 그럼 *esp를 void *가 아니고 다른 자료형의 포인터(e.g. char**)로 cast시키면 어떻게 되는지 확인해봤는데 잘되네...
  // 그러면 memcpy를 쓸 이유가 없어지긴한데 삽질 레전드ㅠ
  //memcpy(*esp, *esp + 4, sizeof(char *));
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  // argc (Type: char**)
  *esp -= sizeof (int);
  *((int*)*esp) = argc;
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  // return address (Type: void (*) ()) return address니까 pointer로 casting해주면 될 거 같아서 했는데 됨 ㅇㅇ
  *esp -= sizeof(char*);
  *((char*)*esp) = NULL;
  //hex_dump(*esp, *esp, PHYS_BASE - *esp, true);

  if(ptr_arr != NULL){
    free(ptr_arr);
  }
  return;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  // Please See debugging pictures in commit (d2fbc0f434de58015321a169bbb960aa3d5176ff)
  // 디버깅해보니까 thread_create에 program_name이 들어가고
  // start_process에는 file_name이 들어가서 start_process 부분에 command2argv()랑 argv2stack()이 들어가면 될 거 같다

  // HJ
  // char** argv;
  // int argc;

  // argv = NULL;
  // argc = 0;

  // 23-04-26 HJ
  // if_.esp: stack pointer
  // 함수 parameter로 넘기면 수정할 수 없으니 if_.esp의 주소를 넘긴다 (load에서 &if_.esp로 넘기는 거랑 비슷)
  
  // 23-04-26 HJ
  // argv2stack 디버깅해보니까 &if_.esp가 0xc0000000(PHYS_BASE)이 아니라 다른값이 들어가있는 거 같아서
  // printf로 출력해보니까 아니다다를까 &if_.esp가 다른값이었네
  
  // 23-04-26 HJ
  // PHYS_BASE로 검색을 해보니 setup_stack(called at load)에서 *esp = PHYS_BASE;이 되면서 바뀌는데
  // 지금 start_process 함수에서는 load function보다 command2argv가 먼저 call되기때문에 
  // esp가 제대로 바뀌기 전에 command2argv , argv2stack이 call되어 이상한 곳에 저장을 하고 있다는 것
  // load 함수의 setup_stack 뒷부분으로 옮겨야할 듯...?
  //printf("@start_process - if_.esp: %x\n", if_.esp);  // @start_process - if_.esp: c010afa0
  // command2argv(file_name, argv, &argc, &if_.esp);

  // 23-04-26 HJ
  // 아 ㅋㅋ... 옮기면 file_name이 full arguments가 들어가네
  //printf("@start_process - %s\n", file_name);
  
  // 23-04-27 HJ
  // file_name에 full arguments 대신 file_name만 parsing하는 거 process_execute에서 했지않나

  // Copied from process_execute
  size_t len = strlen(file_name) + 1;  // Including '\0'
  char* fn_copy = malloc(sizeof(char) * len);  // 메모리 해제 완료
  strlcpy(fn_copy, file_name, len);
  
  char* program_name;
  char* save_ptr;
  program_name = NULL;
  save_ptr = NULL;

  program_name = strtok_r(file_name, " ", &save_ptr);

  // Hotfix
  //thread_set_name(program_name);
  // 23-04-27 HJ
  // 이러면 file_name이 parsed된 file_name이 들어가니까 load - setup_stack function call 아래에 있는 argument passing 부분이 안돌아가니까
  // start_process - load function call 아래로 옮기는 게 낫겠다 ...

  // 23-04-24 HJ
  // command2argv 안에서 동적할당시킨거라 Function call 끝나면 자동적으로 free하나보네 ...
  // int idx;
  // for(idx = 0; idx <= argc; idx++){
  //   printf("argv[%d]: %s\n", idx, argv[idx]);
  // }
  // argv2stack(&if_.esp, argv, &argc);

  //printf("@start_process - program_name: %s\n", program_name);
  success = load (program_name, &if_.eip, &if_.esp);
  //printf("@start_process - file_name: %s\n", file_name);

  //printf("file_name: %s\n", file_name);
  palloc_free_page (file_name);  // process_execute의 fn_copy 해제 완료

  /* If load failed, quit. */
  // The child must store whether the load is success or not in its own variable "success_load" and notify the parent that the load is done
  if (!success) {
    struct thread* t = thread_current();
    t->success_load = -1;  // store the child failed to load
    sema_up(&t->parent->child_not_load);  // notify the parent that the load is done
    
    if(fn_copy != NULL){
      free(fn_copy);  // start_process의 fn_copy 해제 완
    }
    /* thread_exit(): Deschedules the current thread and destroys it.  Never returns to the caller. */
    //thread_exit ();
    // load가 실패했을 떄 thread_exit만 호출하면 이 process를 기다리는 parent는 infinite wait에 걸리게 된다
    // 따라서 external_exit(-1)로 안전하게 exit하도록 하니 multi-oom이 성공적으로 돌아감
    external_exit(-1);
    return;
  }

  // If load succeed, resume the execution.
  command2argv(fn_copy, &if_.esp);
  
  struct thread* t = thread_current();
  t->success_load = 1;  // store the child succeed to load
  sema_up(&t->parent->child_not_load);   // notify the parent that the load is done

  if(fn_copy != NULL){
    free(fn_copy);  // start_process의 fn_copy 해제 완
  }
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */

/* 
위에 함수 설명 읽어보면
child_tid가 함수 argument로 들어오고 해당 tid를 가진 child thread를 찾고
chile_thread의 status(stored in success_exit)를 확인해서 어떤 상태인지 알아낸 다음에 return 하는 거 ㅇㅇ
*/
int
process_wait (tid_t child_tid) 
{
  /*
  // thread/init.c
  // run_action() in main
  // run_task()

  // static void
  // run_task (char **argv)
  // {
  //   const char *task = argv[1];
  //   printf ("Executing '%s':\n", task);  // printf()는 대기 중
  // #ifdef USERPROG
  //   process_wait (process_execute (task));
  // #else
  //   run_test (task);
  // #endif
  //   printf ("Execution of '%s' complete.\n", task);
  // }

  // run_task의 process_wait (process_execute (task));에서 
  // process_execute()
  // -> process_wait (Now just return -1;)
  // -> print하는 process로 context switching
  // -> start_execute()
  // -> page fault 함수 돌아가는데 ...?
  // 이거 2-2에서 구현하는거 라든데??
  // (This function will be implemented in problem 2-2.  
  //  For now, it does nothing.)

  // ㄴㄴ 중간고사 치기 전에 args_none 디버깅해봤을 때
  // process_execute() - process_wait() 부분에서 
  // 지금 process_wait는 just return -1이라서 wait 하지 않고 그냥 넘어감
  // Stanford PintOS 메뉴얼 3.2 suggested order of implementation 맨 아랫줄에
  // Change process_wait() to an infinite loop라고 적힌 거 보니 여기서 busy wait하게 만들면 될듯?
  // while (1);  //infinite loop쓰면 안돌아감
  // 어느정도 돌다가 다시 일어나게해야할 거 같은데

  
  지금 exec-once가 pass안되는 이유가
  system call: exec
  system call: wait
  순으로 실행되는데 wait 부분 구현이 제대로 안돼서 pass가 안됨
  

  for loop는 threads/thread.c의 get_max_priority함수처럼 구현
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
  
  
  // 일단 process_wait에서는 hanging_children list 돌면서 == tid인 thread를 찾는데
  // 언제 어느 함수에서 hanging_children에 process를 추가해야하는지 찾아서 구현할 예정
  // 이걸 구현안하고 실행해보니까
  // main -> args-none하는 과정에서 아래의 for문이 안돌아가니까 바로 ret=-1을 return해서 output이 main:exit(-1)이 뜬다

  //int temp_tid = child_tid;  // For (gdb) print temp_tid

   230518 HJ
  지금 구현한 거에서 args-none 돌린 거랑 원래 거에서 args-none 걸린 거랑 비교해보면
  threads/init.c의 run_task함수에서 process_wait(process_execute(task))부분에서
  process_execute -> thread_create -> process_wait인거 까지는 같은데
  원래거는 process_wait에서 for 문으로 delay줘서 start_process 끝날 때까지 기다리는데
  지금 구현한 거는 process_wait에서 child process가 끝날 때까지 안기다리고 바로 return해버려서 output이 없음
  => synchronization이 필요
  Stanford Manual: If pid is still alive, waits until it terminates.

  */
  struct thread* t = thread_current();
  struct thread* p = t->parent;

  struct list_elem* curr = NULL;
  struct list_elem* next = NULL;
  bool check_find = false;    // child_tid가 direct child인지 아닌지 check하는 변수 (default: false, for 문에서 == child_tid인 thread 찾았을 경우: true)
  struct thread* child = NULL;
  for(curr = list_begin(&(t->hanging_children)); curr != list_end(&(t->hanging_children)); curr = next){  // loop to find child thread of which tid is the same as child_tid(함수인자)
    struct thread *temp = list_entry(curr, struct thread, hanging_child);
    next = list_next(curr);
    // if child_tid == temp->tid => child_tid is direct child of the current thread => check_find = true
    //printf("temp->tid: %d\n", temp->tid);
    if(child_tid == temp->tid){
      check_find = true;
      child = temp;
      break;
    }
  }
  // invalid case
  // Stanford Manual: pid does not refer to a direct child of the calling process => ret = -1
  int exit_return;
  if(check_find == false){
    exit_return = -1;
  }
  // find direct child of the calling process => ret = child->success_exit
  else {
    // Child process의 thraed_exit - unmap하는 부분에서 list돌다가 exit(-1)이 되어서 sema_down의 위치를 옮기고 thread_exit의 unmap하는 부분도 옮김
    sema_down(&child->child_not_give_to_unmap);  // child process가 unmap을 완료할 때까지 ㄱㄷ
    sema_down(&(child->child_not_exit));  // parent must wait for the child to exit
    exit_return = child->success_exit;  // parent should retrieve the exit status of the child
    // parent가 exit_state를 볼 수 있도록 child는 완전히 exit되지 않고 
    //parent가 exit를 허락해줄 때(exit_state를 확인할 때까지)까지 기다려야한다
    sema_up(&child->child_not_give_to_succ_ex);  // The child should not be exited entirely before the parent retrieve the exit status of the child
    list_remove(&(child->hanging_child));   // Now, the child can be exited entirely, then the list_elem hanging_child should be removed from the hanging_children
    //list_remove(&(child->hanging_child));
    // if(exit_return == -1){  // exit_return == -1인 경우는, 아래의 return exot_return에서 -1이 되니까 여기서 return -1 해줄 필요는 없다
    // 
  }
  int debug_ret = exit_return;  // For debugging: print debug_ret
  return exit_return;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
      // struct list_elem* currf = NULL;
      // struct list_elem* nextf = NULL;

      // Find frame->page == page
      // for(currf = list_begin(&frame_table); currf != list_end(&frame_table); currf = nextf){
      //   frame *tempf = list_entry(currf, frame, frame_elem);
      //   nextf = list_next(currf);
      //   palloc_free_page_wf(tempf->page);
      // }



  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *page_num,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  // // 23-04-26 moved from start_process
  // char** argv;
  // int argc;

  // argv = NULL;
  // argc = 0;

  // printf("@start_process - esp: %x\n", esp);  // @load - esp: 0xc0000000
  // command2argv(file_name, argv, &argc, esp);



  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  //file_close (file);  
  //page_fault에서 support_page_load->file_read할때 자꾸 bytes_read가 0이라서 헤맸는데
  //<= page_fault에서 파일이 접근 가능해야하는데,.. 여기가 자꾸 파일을 닫아서 그랬던 거였음...
  //주석처리하니까 잘 돌아감
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */

/* project 3 */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *page_num,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (page_num) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Lazy Loading by Support_page */
      // load segment에서는 파일 읽어서 physical memory에 load하면 안되고
      // support_page만 만들어야함 (physical_memory에 올리는 건 page_fault에서)
      support_page* sp = support_page_init();
      if(sp == NULL) return false;
      support_page_set(sp, file, ofs, page_num, page_read_bytes, page_zero_bytes, writable);
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      page_num += PGSIZE;

      // support_page에 ofs로 들어가야해서 increment해줘야함 => 안해줘서 오류 => 해결 완
      ofs += page_read_bytes;  // need to increment ofs by page_read_bytes 
    }
  return true;
}


/* Original load_segment (분석 at page.txt) */

// static bool
// load_segment (struct file *file, off_t ofs, uint8_t *upage,
//               uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
// {
//   ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
//   ASSERT (pg_ofs (upage) == 0);
//   ASSERT (ofs % PGSIZE == 0);

//   file_seek (file, ofs);
//   while (read_bytes > 0 || zero_bytes > 0) 
//     {
//       /* Calculate how to fill this page.
//          We will read PAGE_READ_BYTES bytes from FILE
//          and zero the final PAGE_ZERO_BYTES bytes. */
//       size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
//       size_t page_zero_bytes = PGSIZE - page_read_bytes;

//       /* Get a page of memory. */
//       //uint8_t *kpage = palloc_get_page (PAL_USER);
//       uint8_t *kpage = palloc_get_page_wf (PAL_USER);
//       if (kpage == NULL)
//         return false;

//       /* Load this page. */
//       if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
//         {
//           // palloc_free_page (kpage);
//           palloc_free_page_wf (kpage);
//           return false; 
//         }
//       memset (kpage + page_read_bytes, 0, page_zero_bytes);

//       /* Add the page to the process's address space. */
//       if (!install_page (upage, kpage, writable)) 
//         {
//           // palloc_free_page (kpage);
//           palloc_free_page_wf (kpage);
//           return false; 
//         }

//       /* Advance. */
//       read_bytes -= page_read_bytes;
//       zero_bytes -= page_zero_bytes;
//       upage += PGSIZE;
//     }
//   return true;
// }

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
/* project 3 */
static bool
setup_stack (void **esp) 
{
  /* Lazy Loading by Support_page */
  // setup_stack에서는 기존의 함수 구현이 physical memory에 올리는 부분이 아니라서 남겨도 됨
  // 여기서도 support_page를 set
  support_page* sp = support_page_init();
  if(sp == NULL) return false;
  support_page_set(sp, NULL, 0, ((uint8_t *) PHYS_BASE) - PGSIZE, 0, 0, true);
  // ((uint8_t *) PHYS_BASE) - PGSIZE 는 아래에 install_page에서 upage 인자로 들어가는 거 보니 page_num이다
  // sp->file = NULL, sp->offset = 0, sp->readbytes = 0, sp->zero_bytes = 0 
  // load_segment때는 얘네 알 수 있었는데, setup_stack에서는 zero page라서 얘네에 대한 정보가 없어서 모르는 채로 둬야함

  uint8_t *kpage;
  bool success = false;

  //kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  kpage = palloc_get_page_wf (PAL_USER | PAL_ZERO, ((uint8_t *) PHYS_BASE) - PGSIZE, sp);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success) {
        *esp = PHYS_BASE;
        sp->mapped_file = -1;
        sp->load = true;
      }
      else 
        //palloc_free_page (kpage);
        palloc_free_page_wf (kpage);
    }
  return success;
}

/* original setup_stack */
// static bool
// setup_stack (void **esp) 
// {
//   uint8_t *kpage;
//   bool success = false;

//   //kpage = palloc_get_page (PAL_USER | PAL_ZERO);
//   kpage = palloc_get_page_wf (PAL_USER | PAL_ZERO);
//   if (kpage != NULL) 
//     {
//       success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
//       if (success)
//         *esp = PHYS_BASE;
//       else
//         //palloc_free_page (kpage);
//         palloc_free_page_wf (kpage);
//     }
//   return success;
// }

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

// wrapper function of static bool install_page()
bool external_install_page (void *page_num, void *kpage, bool writable) {
  return install_page(page_num, kpage, writable);
}