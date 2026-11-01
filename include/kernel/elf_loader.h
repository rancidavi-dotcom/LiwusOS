#ifndef KERNEL_ELF_LOADER_H
#define KERNEL_ELF_LOADER_H

#include <stdint.h>
#include "vmm.h"

int elf_class(void *file_buffer, uint32_t file_size);
uint64_t elf32_load_file_in(page_directory_t *mm, void *file_buffer,
                            uint32_t file_size);
uint64_t elf64_load_file_in(page_directory_t *mm, void *file_buffer,
                            uint32_t file_size);
uint64_t elf32_load_file(void *file_buffer, uint32_t file_size);
uint64_t elf64_load_file(void *file_buffer, uint32_t file_size);

#endif
