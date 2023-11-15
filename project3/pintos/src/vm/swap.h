#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "vm/page.h"
#include "vm/frame.h"

/*Slot Management를 위해 struct bitmap 사용 @lib/kernel/bitmap.c data structure*/
struct bitmap* swap_bmp;

/*Block Interface를 위해 struct block을 선언 @devices/block.h*/
struct block* disk_interface;

/*swap이 동시에 실행되면 안되기에 선언*/
struct semaphore swap_sema;

void swap_init(void);
size_t swap_out(frame*);
void swap_in(size_t, void*);
#endif /* vm/swap.h */