#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "threads/thread.h" 
#include <stdbool.h>        // bool
#include "filesys/off_t.h"  // off_t and uint32_t

#include "threads/synch.h"

typedef int mapid_t;        // mapid_t

typedef struct _support_page{
    bool load;          // load가 되었는지, 안되었는지 확인할 때 필요
    struct list_elem support_page_elem;

    // physical memory에 load할 때 필요한 정보가 supoort_page에 있어야한다
    // load_segment 함수에서 어떤 인자가 어떻게 사용되는지 확인 ㄱㄱ
    // page.txt에서 load_segment 함수 분석 후 추가
    void* page_num;         // Page fault에서 필요, sp_table search할 때 필요

    struct file* mapped_file;
    off_t offset;
    uint32_t read_bytes;
    uint32_t zero_bytes;

    bool write;

    /* 4.3.4. Memory Mapped files */
    mapid_t mapid;

    /* Second-chance Algorithm */
    bool disk;
    size_t idx_disk;  // 저장된 disk의 위치를 알아야 swap_in에서 disk로부터 load할 수 ㅣㅆ다

    /*process가 실행 후 바로 생성된 sp 인지*/
    bool using;

    uint32_t *pagedir; 
    
}support_page;

// Support_page_table is not a global list, but it is unique list in process
// struct list support_page_table;

// Support_page_table is not a global list, but it is unique list in process
// Init support_page_table
//void support_page_table_init(void);

support_page* support_page_init(void);              // Init support_page
void support_page_set(support_page*, struct file*, off_t, void*, uint32_t, uint32_t, bool);  // Set support_page
void support_page_free(support_page*);              // Free support_page
support_page* support_page_get(void*);              // Find support_page from support_page_table
bool support_page_load(support_page*);              // Load by support_page

//support_page* get_support_page(void*);
//void free_support_page(support_page*);


#endif /* vm/page.h */