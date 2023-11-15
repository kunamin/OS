#include "vm/swap.h"
#include "threads/vaddr.h"

void swap_init(void){
    //Stanford Pintos
    // You may use the BLOCK_SWAP block device for swapping, obtaining the struct block that represents it by calling block_get_role()
    disk_interface = block_get_role(BLOCK_SWAP);
    // 4KB: Page Size
    // 512B: Sector Size
    // disk_interface->size: Block size
    int k = ((1<<12) / (1<<9));
    swap_bmp = bitmap_create(block_size(disk_interface) / k);
    bitmap_set_all(swap_bmp, 0);
    sema_init(&swap_sema,1);
}

// Implmented by HJ
// swap_out: Page in Main Memory to Block in Disk => block_write
// void block_write (struct block *block, block_sector_t sector, const void *buffer)
size_t swap_out(frame* victim_frame){
    sema_down(&swap_sema);
    // void block_write (struct block *block, block_sector_t sector, const void *buffer)
    // We need to know block_set_t sector
    // there is no way for swap_out to know which page should be swapped out (X)
    // We need to scan free frame by using the given data structure, swap_bmp
    size_t idx_free;
    // sema 
    //sema_down(&swap_sema);
    // second_change algorithm을 여기서 구현해야하나 생각했었는데
    // 그 알고리즘은 victim page를 찾을 때 Approximate LRU하게 찾기 위해서 사용하는 알고리즘이라서 여기서는 사용할 필요가 없다 (page_fault쪽에서 구현해야할듯)
    // swap frame을 찾을 때는 그냥 bitmap scan해서 0인 거 찾으면 된다
    // size_t bitmap_scan (const struct bitmap *b, size_t start, size_t cnt, bool value)
    // 우리는 연속해서 8개의 block에 victim_page를 swap-out할 예정이므로 start(0)부터 8개의 bit가 연속으로 0인 idx를 찾는다
    idx_free = bitmap_scan(swap_bmp, 0, 1, 0);
    if(idx_free == SIZE_MAX)
		return -1;
    int k = ((1<<12) / (1<<9));
    int idx;
    int page_offset = 0;


    //printf("@@@@outidx : %d \n",idx_free);
    for(idx = idx_free*8; idx < idx_free*8 + k; idx++){
        // 루프 한 번씩 돌 때마다 idx increment해주고 buffer pos도 update해준다 (block size == 512B == (1<<9))
        block_write(disk_interface, idx, victim_frame->page + page_offset);
        page_offset += (1<<9);
    }

    //void bitmap_set (struct bitmap *b, size_t idx, bool value) 
    bitmap_set(swap_bmp, idx_free, 1);
    //bitmap_set(disk_interface, idx_free, 1);
    //printf("victim_page: %x\n", victim_frame);
    //support_page* sp = support_page_get(victim_frame->); // Now support_page_get returns 0

    // // 현재 support_page_get에서 victim_page에서 null이 return되고 sp->load로 접근하니까 exit(-1)
    // sp->load = false;
    // sp->disk = true;
    // sp->idx_disk = idx_free;  // 저장된 disk의 위치를 알아야 swap_in에서 disk로부터 load할 수 ㅣㅆ다
    // //return idx_free; 
    //sema_up(&swap_sema);
    sema_up(&swap_sema);
    return idx_free;
}

// Implmented by KH
// swap_in: Block in Disk to Page in Main Memory => block_read
//void block_read (struct block *block, block_sector_t sector, void *buffer)
void swap_in(size_t idx_use, void *page_buffer){
    sema_down(&swap_sema);
    /*disk에서 메모리로 read*/
    int i;
    //printf("@@@@%d idx!!!!!!!!\n\n", idx_use);
    /*((1<<12) / (1<<9) = 8*/
    if(bitmap_test(swap_bmp, idx_use) == 0)
		return;
    int page_offset = 0;
    for(i = idx_use*8; i<idx_use*8+8; i++){
        block_read(disk_interface, i, page_buffer+page_offset);
        page_offset += (1<<9);
    }
    /*이제 idx가 비어있다고 변경*/
    bitmap_set(swap_bmp, idx_use, 0);
    // support_page* sp = support_page_get(page_buffer);
    // sp->load = false;
    // sp->disk = false;
    // sp->idx_disk = 0;
    //printf("@swap_in\n");
    sema_up(&swap_sema);
}