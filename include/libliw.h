#ifndef LIBLIW_H
#define LIBLIW_H

#include <stdint.h>

// Estruturas do manifesto (simplificado)
typedef struct {
  char name[64];
  char version[32];
  // permissions...
} liw_manifest_t;

// API de Runtime (para o próprio APP acessar seus dados)
// Como o app sabe onde está seu arquivo .liw?
// O kernel poderia passar um FD ou mmap address.
// Por enquanto, vamos assumir que o app não consegue se ler facilmente sem
// suporte extra do kernel. Vou criar stubs.

// API de Gerenciamento (para o 'liw' manager)
typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t flags;
  uint32_t entry_offset;
  uint32_t entry_size;
  uint32_t manifest_offset;
  uint32_t manifest_size;
  uint32_t resources_offset;
  uint32_t resources_size;
} liw_file_header_t;

// Lê o manifesto de um arquivo .liw
int liw_read_manifest(const char *filename, liw_manifest_t *out_manifest);

// Extrai um recurso de um arquivo .liw (To be implemented)
// int liw_extract_resource(const char* liw_file, const char* res_name, void*
// buffer, uint32_t size);

/* User-facing LibLiw Syscall Wrappers & Helpers */
void print(const char *s);
void print_int(int n);

int syscall_fork();
int syscall_waitpid(int pid, int *status, int options);
void syscall_exit(int status);
uint32_t syscall_brk(uint32_t addr);

#endif
