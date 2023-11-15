#include "vm/page.h"

#include <stdio.h>           // debugging용 printf함수쓸 때
#include "threads/malloc.h"  // malloc, free
#include "threads/palloc.h"  // palloc functions
#include <string.h>          // memset

#include "threads/thread.h"
#include "userprog/process.h" // install_page
#include "userprog/syscall.h"
#include "vm/swap.h"
// void support_page_table_init(void){
//     list_init(&support_page_table);
// }

// Init support_page
support_page* support_page_init(void){
    support_page* sp;
    sp = malloc(sizeof(support_page));      // support_page 할당 완
    memset(sp, 0, sizeof(support_page));    // Initialize Member vars (Done by memset as below)
    // sp->load = false;
    // sp->page_num = NULL;
    // sp->mapped_file = NULL;
    // sp->offset = 0;
    // sp->read_bytes = 0;
    // sp->zero_bytes = 0;
    // sp->write = false;
    return sp;
}

// Set support_page
void support_page_set(support_page* sp, struct file* file, off_t ofs, void* page_num, uint32_t read_bytes, uint32_t zero_bytes, bool write) {
    // Set Member vars
    sp->load = false;
    sp->page_num = page_num;
    sp->mapped_file = file;
    sp->offset = ofs;
    sp->read_bytes = read_bytes;
    sp->zero_bytes = zero_bytes;
    sp->write = write;
    sp->mapid = 0;
    sp->using = false;
    sp->disk = false;
    sp->idx_disk = -1;
    // Update support_page_table
    struct thread* t = thread_current();
    sp->pagedir = t->pagedir;
    list_push_back(&t->support_page_table, &sp->support_page_elem);
}


// Free support_page
void support_page_free(support_page* sp){
    // frame* frame_candidate = NULL;
    // frame* frame_victim = NULL;
    // // frame이 가리키고 있는 page가 access 되었는지
    // bool loop = true;
    // // frame_table_init에서 iterator에는 list_head가 들어가있기 때문에 
    // // (list_begin으로 했다가 빈 table에서는 list_begin이 null이라 안돼서 list_head로 바꿨었음)
    // // find_frame_victim이 처음 실행되었을 때, iterator를 바로 list_entry하게 되면 list_head에 접근하는 꼴이 된다...
    // // 그래서 frame->flag랑 frame->sp를 printf했을 때 이상한 값이 들어가있었던거
    // // iterator == list_head && !list_empty(&frame_table) 일때는 list_begin을 넣어주자
    // //if(iterator == list_head(&frame_table) && !list_empty(&frame_table)) iterator = list_begin(&frame_table);
    // struct list_elem* curr = list_begin(&frame_table);
    // //for(curr = iterator;;curr = next){
    // while(loop) {
    //     frame_candidate = list_entry(curr, frame, frame_elem);
    //     if(frame_candidate->addr == sp->page_num){
    //         loop = false;
    //         frame_victim = frame_candidate; 
    //     }
    //     // iterator는 실패했어도 다음 iterator로 넘어가야하고 성공했어도 다음 element로 넘어가야함
    //     curr = list_next(curr);
    //     if(curr == list_end(&frame_table)) break;
    // }
    // if(frame_victim != NULL) {
    //     if(iterator != NULL)
    //         if(list_entry(iterator, frame, frame_elem)->page == frame_victim ->page)
    //             if(curr != list_end(&frame_table))
    //                 iterator = curr;
    //             else 
    //                 iterator = list_begin(&frame_table);
    //     palloc_free_page_wf(frame_victim->page);
    //     printf("Hello free fram\n\n\n\n\n");
    //     sema_down(&sp->wait_free_frame);
    // }
    list_remove(&sp->support_page_elem);     // Update support_page_table
    // if(sp->load){
    //     void* page = frame_get_addr2page(sp->page_num);
    //     if(page != NULL){
    //         palloc_free_page_wf(page);
    //     }
    // }
    //palloc_free_page_wf(pagedir_get_page(sp->pagedir, sp->page_num));
    //if(sp->load || sp->disk || sp->mapped_file == -1){
    if(frame_get_addr2page(sp->page_num) != NULL){
        palloc_free_page_wf(pagedir_get_page(thread_current()->pagedir, sp->page_num));
    }
    free(sp);                                // support_page 해제 완
}

support_page* support_page_get(void* page_num){
    struct thread* t = thread_current();

    struct list_elem* curr = NULL;
    struct list_elem* next = NULL;

    // Find support_page->addr == addr
    for(curr = list_begin(&t->support_page_table); curr != list_end(&t->support_page_table); curr = next){
        support_page* temp = list_entry(curr, support_page, support_page_elem);
        //printf("support_page_get: %x\n", temp);
        //printf("support_page_get: %x\n", temp->page_num);
        next = list_next(curr);
        if(temp->page_num == page_num){
            return temp;
        }
    }
    return NULL;
}

// Load by support_page referred to load_segment_original (Original version of load_segment)
bool support_page_load(support_page* sp){
    //if(sp->mapped_file == NULL) return false;
    //sema_down(&filesys_at_once);
    // if(sp->idx_disk == -1){

    // }
    // else {

    // }
    //if(!sp->disk && sp->mapped_file != NULL) file_seek (sp->mapped_file, sp->offset);  // disk에 있지 않으면 mapped_file의 offset을 sp->offset으로
    //sema_up(&filesys_at_once);
    uint8_t *kpage = palloc_get_page_wf (PAL_USER, sp->page_num, sp);
    //printf("%x\n", kpage);
    if (kpage == NULL) return false;

    // setup_stack에서 만들어진 파일 (얘네는 file도 설정안되어있고 read_byte, zero_byte도 0이라서 따로 처리)
    if(sp->mapped_file == -1 && !sp->disk){
        if(!external_install_page(sp->page_num, kpage, sp->write)){
            // palloc_free_page (kpage);
            palloc_free_page_wf (kpage);
            return false;
        }
        sp->load = true;  // frame에 들어옴 ㅇㅇ
        return true;
    }
    // disk에 있는 파일
    if(sp->disk){
        //sema_down(&swap_sema);
        swap_in(sp->idx_disk, kpage);
        //sema_up(&swap_sema);

        //printf("swap_in: %x\n", sp);

        if(!external_install_page(sp->page_num, kpage, sp->write)){
            // palloc_free_page (kpage);
            palloc_free_page_wf (kpage);
            return false;
        }

        sp->load = true;  // frame에 들어옴 ㅇㅇ
        sp->disk = false;
        sp->idx_disk = -1;
        return true;
    }
    
    file_seek (sp->mapped_file, sp->offset); 
    if (file_read (sp->mapped_file, kpage, sp->read_bytes) != (int) sp->read_bytes) {
        // palloc_free_page (kpage);
        palloc_free_page_wf (kpage);
        return false; 
    }
    memset (kpage + sp->read_bytes, 0, sp->zero_bytes);
    if(!external_install_page(sp->page_num, kpage, sp->write)){
        // palloc_free_page (kpage);
        palloc_free_page_wf (kpage);
        return false;
    }
    sp->load = true;  // frame에 들어옴 ㅇㅇ
    return true;

    /*
    // disk에 있을 때는 file_read가 아니라 swap_in으로 가져와야한다
    if(sp->disk){
        sema_down(&swap_sema);
        swap_in(sp->idx_disk, kpage);
        sema_up(&swap_sema);
        //printf("sp @swap: %x\n", sp->page_num);
        // frame_page 새로 만들어서 추가 => palloc_get
        //printf("success in swap_in\n");
        // sp->load = true;  // frame에 들어옴 ㅇㅇ
        // sp->disk = false;
        // sp->idx_disk = -1;
        //pagedir_set_accessed(thread_current()->pagedir, sp->page_num, 1);
        if(!external_install_page(sp->page_num, kpage, sp->write)){
            // palloc_free_page (kpage);
            palloc_free_page_wf (kpage);
            return false;
        }

        sp->load = true;  // frame에 들어옴 ㅇㅇ
        sp->disk = false;
        sp->idx_disk = -1;
        return true;
    }
    else if (file_read (sp->mapped_file, kpage, sp->read_bytes) != (int) sp->read_bytes) {
        // palloc_free_page (kpage);
        palloc_free_page_wf (kpage);
        return false; 
    }
    */
    /* zero padding */
    /*
    memset (kpage + sp->read_bytes, 0, sp->zero_bytes);
    //printf("After zero padding\n");
    if(!external_install_page(sp->page_num, kpage, sp->write)){
        // palloc_free_page (kpage);
        palloc_free_page_wf (kpage);
        return false;
    }
    sp->load = true;  // frame에 들어옴 ㅇㅇ
    return true;
    */

}


// support_page* get_support_page(void* addr){
//     support_page* sp;
//     sp = support_page_init();
// }

// void free_support_page(support_page* sp){
    
// }