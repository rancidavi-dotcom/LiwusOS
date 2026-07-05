#include "liw_format.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Compilar com: gcc liw-builder.c -o liw-builder

void write_file(FILE *out, const char *filename, uint32_t *offset,
                uint32_t *size) {
  if (!filename) {
    *offset = 0;
    *size = 0;
    return;
  }

  FILE *in = fopen(filename, "rb");
  if (!in) {
    perror("fopen");
    *offset = 0;
    *size = 0;
    return;
  }

  fseek(in, 0, SEEK_END);
  *size = ftell(in);
  fseek(in, 0, SEEK_SET);

  *offset = ftell(out);

  void *buf = malloc(*size);
  fread(buf, 1, *size, in);
  fwrite(buf, 1, *size, out);
  free(buf);

  fclose(in);
}

int main(int argc, char **argv) {
  if (argc < 4) {
    printf("Usage: %s <output.liw> <input.elf> <manifest.json> [resources]\n",
           argv[0]);
    return 1;
  }

  FILE *out = fopen(argv[1], "wb");
  if (!out) {
    perror("fopen out");
    return 1;
  }

  liw_header_t header;
  memset(&header, 0, sizeof(header));
  header.magic = LIW_MAGIC;
  header.version = 1;

  // Placeholder header
  fwrite(&header, 1, sizeof(header), out);

  // Write ELF
  write_file(out, argv[2], &header.entry_offset, &header.entry_size);

  // Write Manifest
  write_file(out, argv[3], &header.manifest_offset, &header.manifest_size);

  // Write Resources (Optional)
  if (argc > 4) {
    write_file(out, argv[4], &header.resources_offset, &header.resources_size);
  }

  // Rewrite Header
  fseek(out, 0, SEEK_SET);
  fwrite(&header, 1, sizeof(header), out);

  fclose(out);
  printf("Created %s with ELF size %d\n", argv[1], header.entry_size);
  return 0;
}
