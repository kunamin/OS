#include "vm/frame.h"

#include <stdio.h>           // debugging용 printf함수쓸 때
#include "threads/malloc.h"  // malloc, free
#include "threads/palloc.h"  // palloc functions
#include <string.h>          // memset

//#include "vm/page.h"
#include "vm/swap.h"

/* Second-chance Algorithm */
#include <stdbool.h>

/* For ease of debugging*/
#include<stdio.h>
/*
enum palloc_flags
  {
    PAL_ASSERT = 001,
    PAL_ZERO = 002,
    PAL_USER = 004  
  }
*/

// Init frame_table
void frame_table_init(void){
    list_init(&frame_table);
    sema_init(&find_victim,1);
    sema_init(&frame_table_lock, 1);
    sema_init(&palloc_at_once, 1);
    /* Second-chance Algorithm */
    // 1. next victim을 가리킬 (circular queue의 포인터 역할을 하는) 변수 초기화 (전역변수라서 해줘야함)
    // list_begin으로 했는데 오류남
    // list_next에서 오류가 나서 디버깅해보니까 iterator로 들어가는 값을 frame_table이 empty일 때인데
    // 그때 할당된 iterator로 list_entry하려니까 오류가 나는듯
    // list.c를 살펴보니 empty list initialize하면 head-tail부터 시작이라서 처음 iterator에 할당하는 값을 list_head로 해야함
//     iterator = list_head(&frame_table);
//     frame *temp = list_entry(iterator, frame, frame_elem);
//     //temp->flag: -1072689152
//     //temp->sp: 0
//     //찾았다.... iterator가 list_head로 해가지고 바로 첫번째 find_victim_frame에서 frame_table의 head에 대해서 list_entry를 해가지고
//     //이상하게 오류가 났던거 ...
//     printf("temp->flag: %d\n", temp->flag);
//     printf("temp->sp: %x\n", temp->sp);
}

// Init frame
frame* frame_init(void){
    frame* fr;
    fr = malloc(sizeof(frame));     // Frame 동적할당
    memset(fr, 0, sizeof(frame));   // Initialize Member vars (Done by memset as below)
    // fr->page = NULL;
    // fr->flag = 0;
    return fr;
}

// Set frame
void frame_set(frame* fr, void* p, enum palloc_flags f, uint32_t* pagedir, void* addr, support_page* sp){
    fr->page = p;                                   // set fr->page
    //printf("fr->f: %d\n", f);
    fr->flag = f;                                   // set fr->flag
    fr->pagedir = pagedir;                         // set fr->pagedir
    fr->addr = addr;
    //printf("fr->sp: %x\n", fr->sp);             // 왜 출력하는 걸 보면 항상 0이지? 제대로 sp주소를 못받아오는 거 같은데? => 아 아직 assign을 안했네 ㅋㅋ
    fr->sp = sp;
    //printf("fr->sp: %x\n", fr->sp);
    sema_down(&frame_table_lock);
    list_push_back(&frame_table, &fr->frame_elem);  // frame_table Update
    sema_up(&frame_table_lock);
}

// Free frame
void frame_free(frame* fr){
    sema_down(&frame_table_lock);
    //printf("Free fr: %x\n", fr);
    //sema_down(&palloc_at_once);
    palloc_free_page(fr->page);      // fr->page 해제
    //sema_up(&palloc_at_once);
    list_remove(&fr->frame_elem);   // frame_table update
    free(fr);                       // Frame 해제
    sema_up(&frame_table_lock);
}

// Find frame from frame_table
frame* frame_get(void* page){
    struct list_elem* curr = NULL;
    struct list_elem* next = NULL;

    // Find frame->page == page
    for(curr = list_begin(&frame_table); curr != list_end(&frame_table); curr = next){
        frame *temp = list_entry(curr, frame, frame_elem);
        next = list_next(curr);
        if(temp->page == page){
            return temp;
        }
    }
    return NULL;
}

// Find page from addr
void* frame_get_addr2page(void* addr){
    struct list_elem* curr = NULL;
    struct list_elem* next = NULL;

    // Find frame->page == page
    for(curr = list_begin(&frame_table); curr != list_end(&frame_table); curr = next){
        frame *temp = list_entry(curr, frame, frame_elem);
        next = list_next(curr);
        if(temp->addr == addr){
            return temp->page;
        }
    }
    return NULL;
}

/*
palloc_get
frame_init
*/
void* palloc_get_page_wf(enum palloc_flags f, void* addr, support_page* sp){
    //printf("sp @palloc_get_page_wf: %x\n", sp);  // 여기서는 제대로 출력되는데
    void* p;
    sema_down(&palloc_at_once);
    p = palloc_get_page(f);
    sema_up(&palloc_at_once);
    //printf("palloc_get_page_wf: %x\n", p);
    /* @palloc.c: If no pages are available, returns a null pointer, unless PAL_ASSERT is set in FLAGS, in which case the kernel panics. */
    while(p == NULL) {
        // If no pages are available => Swap out
        //printf("sp1 @palloc_get_page_wf: %x\n", sp);  // 여기서는 제대로 출력되는데
        //sema_down(&swap_sema);
        swap();
        //sema_up(&swap_sema);
       // printf("sp2 @palloc_get_page_wf: %x\n", sp);  // 여기서는 제대로 출력되는데
        // One more time
        sema_down(&palloc_at_once);
        p = palloc_get_page(f);
        sema_up(&palloc_at_once);
        //printf("second  frame page ::  %x\n", p);
        if(p != NULL){
            frame* fr = frame_init();
            frame_set(fr, p, f, thread_current()->pagedir, addr, sp);
            //printf("Finish swap at palloc_get_page_wf\n\n\n");
            return fr->page;
        }
    }
    //else{
        //printf("sp3 @palloc_get_page_wf: %x\n", sp);  // 여기서는 제대로 출력되는데
    frame* fr = frame_init();
    frame_set(fr, p, f, thread_current()->pagedir, addr, sp);
    return fr->page;
   // }
    //return p;
}

/*
palloc_free
frame_free
process.c에서 page만으로는 frame에 대한 정보를 가지고 있기 않기 때문에
frame_table을 돌면서 일치하는 page를 search 해서 찾게 되면 free
*/
void palloc_free_page_wf(void* page){
    // struct list_elem* curr = NULL;
    // struct list_elem* next = NULL;

    // // find frame->page == page
    // for(curr = list_begin(&frame_table); curr != list_end(&frame_table); curr = next){
    //     frame *temp = list_entry(curr, frame, frame_elem);
    //     next = list_next(curr);
    //     if(temp->page == page){
    //         // Page 해제 이후 frame도 해제
    //         palloc_free_page(temp->page);
    //         frame_free(temp);
    //         break;
    //     }
    // }
    frame* f = frame_get(page);
    // support_page* sp = f->sp;
    // // if(f != NULL && pagedir_is_dirty(f->pagedir, f->addr)){
    // //     support_page* sp = f->sp;
    // //     size_t idx_free = sp->idx_disk;
    // //     if(sp->mapid > 0)
    // //         update_dirty_page(f->pagedir, sp->mapid);
    // //     frame_free(f);
    // // }
    // if(f != NULL && sp->mapid < 0) {
    //     //while(idx_free == -1) 

    //         size_t idx_free = sp->idx_disk;

    //         idx_free = swap_out(f); //  3. Swap out 시켜주고
    //         sp->idx_disk = idx_free;
    //         sp->disk = true;  // 저장된 disk의 위치를 알아야 swap_in에서 disk로부터 load할 수 ㅣㅆ다
    //         sp->load = false;
    //     //pagedir_clear_page(f->pagedir, f->addr);
    //     frame_free(f);
    // }
    if(f != NULL) {

        pagedir_clear_page(thread_current()->pagedir, f->addr);
        frame_free(f);
    }
    // if(sp != NULL)
    //     sema_up(&sp->wait_free_frame);
}


// frame_table을 기반으로 탐색해야하니까 frame.c에 구현
// Approximate LRU Algorithm (Second change algorithm (a.k.a clock algorithm))
// HW support로 page가 참조되면 access bit가 set to 1이 된다
// page를 일일이 다 탐색할 수는 없는 노릇이고
// 우리는 frame_table을 가지고 있으니까 (page의 요약본) 이 data structure를 통해서 second chance algorithm을 구현하면 된다

/*
    1. next victim을 가리킬 (circular queue의 포인터 역할을 하는) 변수 정의 (전역변수) (완)
    2. frame_table을 돌면서 frame이 가리키고 있는 page가 access되었는지 확인 (완)
        if access == 1 => access = 0 => 다음 frame으로
        if access == 0 =? Find Victim
        (다음 frame으로 넘어가다가 끝에 도달하면 다시 처음으로 돌려야함 (그래야 circular queue))
    3. return frame (완)
*/ 

frame* find_frame_victim(void){
    sema_down(&frame_table_lock);
    frame* frame_candidate;
    frame* frame_victim;
    // frame이 가리키고 있는 page가 access 되었는지
    bool loop = true;
    // frame_table_init에서 iterator에는 list_head가 들어가있기 때문에 
    // (list_begin으로 했다가 빈 table에서는 list_begin이 null이라 안돼서 list_head로 바꿨었음)
    // find_frame_victim이 처음 실행되었을 때, iterator를 바로 list_entry하게 되면 list_head에 접근하는 꼴이 된다...
    // 그래서 frame->flag랑 frame->sp를 printf했을 때 이상한 값이 들어가있었던거
    // iterator == list_head && !list_empty(&frame_table) 일때는 list_begin을 넣어주자
    //if(iterator == list_head(&frame_table) && !list_empty(&frame_table)) iterator = list_begin(&frame_table);
    if(iterator == NULL){
        iterator = list_begin(&frame_table);
    }
    //for(curr = iterator;;curr = next){
    while(loop) {
        frame_candidate = list_entry(iterator, frame, frame_elem);
        bool access = pagedir_is_accessed(frame_candidate->pagedir, frame_candidate->addr);
        // if access == 1 => access = 0 => 다음 frame으로
        // (다음 frame으로 넘어가다가 끝에 도달하면 다시 처음으로 돌려야함 (그래야 circular queue))
        
        //if(frame_candidate->sp->page_num < 0x824c000 && frame_candidate->sp->page_num > 0x81c3000)
        if(frame_candidate->sp->using){
            // pass (disk_interlock)
        }
        else if(access){
            pagedir_set_accessed(frame_candidate->pagedir, frame_candidate->addr, 0);
        }
        // if access == 0 =? loop = false해서 while문 그만돌게하고 frame_victim에 찾은 frame_candidate를 assign
        else {
            loop = false;
            frame_victim = frame_candidate; 
            pagedir_set_accessed(frame_candidate->pagedir, frame_candidate->addr, 1);
        }
        // iterator는 실패했어도 다음 iterator로 넘어가야하고 성공했어도 다음 element로 넘어가야함
        iterator = list_next(iterator);
        if(iterator == list_end(&frame_table)) iterator = list_begin(&frame_table);
    }
    //iterator = next;
    sema_up(&frame_table_lock);
    return frame_victim;
}

// Swap하는 함수
/* 1. find_frame_victim으로부터 frame_victim return받고
   2. dirty bit 확인해주고
        2.1 dirty bit == 1 이면 업데이트 by update_dirty_page
        2.2 dirty bit == 0 이면 냅둠
    3. Swap out 시켜주고
    4. page table 초기화 시켜주고
    5. frame free 시켜주고 (+ frame_table 최신화)
*/
void swap(void){
     //sema_down(&find_victim);
     frame* frame_victim = find_frame_victim();
     //sema_up(&find_victim);
    //int i;
    //for(i = 0; i < 10000000000; i++) // For debugging (Too many interrupts interfere to continue break point in here)
    //timer_mdelay (1000); 
    //size_t idx_free;  // 저장된 disk의 위치를 알아야 
    //if(pagedir_is_dirty(frame_victim->pagedir, frame_victim->page)){
    //    size_t idx_free = swap_out(frame_victim->page);
    //}
    // if(pagedir_is_dirty(frame_victim->pagedir, frame_victim->page)){
    //     update_dirty_page(frame_victim->pagedir, support_page_get(frame_victim->page));
    // }
    // printf("frame_victim->page: %x\n", frame_victim->page);
    // printf("frame_victim->addr: %x\n", frame_victim->addr);
    // printf("frame_victim->sp: %x\n", frame_victim->sp);
    struct list_elem* curr = NULL;
    struct list_elem* next = NULL;

    // Find support_page->addr == addr
    // struct thread* t = thread_current();
    // for(curr = list_begin(&t->support_page_table); curr != list_end(&t->support_page_table); curr = next){
    //     support_page* temp = list_entry(curr, support_page, support_page_elem);
    //     //printf("support_page_get: %x\n", temp);
    //     //printf("support_page_get: %x\n", temp->page_num);
    //     next = list_next(curr);
    //     //printf("temp->page: %x\n", temp->page_num);
    //     //printf("temp->addr: %x\n", temp->addr);
    // }
    //printf("before swap_out\n");
    support_page* sp = frame_victim->sp;
    size_t idx_free = sp->idx_disk;
    bool loop = true;
    // disk에 있을 때
    if(sp->disk){
        idx_free = swap_out(frame_victim); //  3. Swap out 시켜주고
        sp->idx_disk = idx_free;
        sp->disk = true;  // 저장된 disk의 위치를 알아야 swap_in에서 disk로부터 load할 수 ㅣㅆ다
        sp->load = false; 
    }
    // mapped file일 때
    else if(sp->mapid > 0){
        if(pagedir_is_dirty(frame_victim->pagedir, frame_victim->addr)){
            update_dirty_page_without_sp_free(frame_victim->pagedir, sp);
        }
        sp->load = false;
    }
    // 그 외에 다른 케이스 + setup_stack에서 만들어진 support_page
    else if(idx_free == -1 || sp->mapped_file == -1){
        idx_free = swap_out(frame_victim); //  3. Swap out 시켜주고
        sp->idx_disk = idx_free;
        sp->disk = true;  // 저장된 disk의 위치를 알아야 swap_in에서 disk로부터 load할 수 ㅣㅆ다
        sp->load = false; 
    }
    else{
        //printf("형이 왜 거기있어?\n");
    }


    // if(pagedir_is_dirty(frame_victim->pagedir, frame_victim->addr) || sp->mapid <= 0){
    //     if(sp->mapid > 0){
            
    //         update_dirty_page(frame_victim->pagedir, sp);
    //     }
    //     else if(idx_free == -1){
    //     //while(idx_free == -1) 
    //         idx_free = swap_out(frame_victim); //  3. Swap out 시켜주고
            
    //         sp->idx_disk = idx_free;
    //         sp->disk = true;  // 저장된 disk의 위치를 알아야 swap_in에서 disk로부터 load할 수 ㅣㅆ다
    //         sp->load = false; 
    //     }
    //     else{
    //         printf("형이 왜 거기있어?\n");
    //     }
    // }
    // if(idx_free == -1){
    //     return;
    // }
    //printf("before support_page_get\n");
    //support_page* sp = support_page_get(frame_victim->addr);  // 4. Support page에 기록시켜주기 위해서 찾아오고
    // 근데 지금 support_page에 page랑 인자로 받는 frame_victim->addr이 달라서 support_page_get에서 못찾고 그냥 null return하는 듯... 
    //그래서 밑에 sp->load에서 null 참조해서 exit(-1)
    // 지금 해야하는게 frame_victim으로부터 support_page를 찾는 방법을 찾아야함
    if(sp == NULL){
        //printf("sp==NULL, then sp:%x\n", sp);
        //printf("frame_victim->flag: %d\n", frame_victim->flag);  // ??? flag에 이상한 값이 들어가있는데 (정상적으로 선언된 frame이 아닌 거 같은데...)
    }
    //printf("sp @swap: %x\n", sp);
    //printf("before_load\n");
    
    pagedir_clear_page(frame_victim->pagedir, frame_victim->addr);  // frame_free했는데 pagedir, addr 접근해서 free해주면 안되고 frame_free보다 앞에서해줘야지
    frame_free(frame_victim);  //     4. frame free 시켜주고 (+ frame_table 최신화)
    //pagedir_clear_page(frame_victim->pagedir, frame_victim->page);  // 5. page table 초기화 시켜주고
    //printf("finish swap\n");
    //return frame_victim->page;  // 비어진 page의 주소를 할당
}