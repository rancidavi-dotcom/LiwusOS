#include "kheap.h"

extern uint32_t end;
static uint32_t placement_address = 0;

void init_kheap() {
    placement_address = (uint32_t)&end + 0x10000; /* 64KB de folga após o kernel */
}

void* kmalloc(size_t size) {
    if (placement_address == 0) init_kheap();
    uint32_t tmp = placement_address;
    placement_address += size;
    return (void*)tmp;
}

void* kmalloc_a(size_t size) { return kmalloc(size); }
void* kmalloc_ap(size_t size, uint32_t* phys) { 
    void* addr = kmalloc(size);
    if(phys) *phys = (uint32_t)addr;
    return addr;
}
