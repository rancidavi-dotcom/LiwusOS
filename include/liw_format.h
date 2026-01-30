#ifndef LIW_FORMAT_H
#define LIW_FORMAT_H

#include <stdint.h>

#define LIW_MAGIC 0x5845574C /* "LWEX" in little endian (L, W, E, X) */
// "LIWEXEC\0" eram 8 bytes, vou usar um uint32 simples para magic inicial: LWEX

typedef struct {
  uint32_t magic;            // LIW_MAGIC
  uint32_t version;          // 1
  uint32_t flags;            // 0
  uint32_t entry_offset;     // Offset in file where ELF starts
  uint32_t entry_size;       // Size of ELF binary
  uint32_t manifest_offset;  // Offset to JSON manifest
  uint32_t manifest_size;    // Size of manifest
  uint32_t resources_offset; // Offset to resources bundle
  uint32_t resources_size;   // Size of resources
  uint8_t padding[32];       // Reserved
} liw_header_t;

#endif
