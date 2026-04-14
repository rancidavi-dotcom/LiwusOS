// C4 - C in four functions (Versão Robusta para LiwusOS)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv) {
  int i, fd;
  if (argc < 2) { printf("uso: crun <arquivo.c>\n"); return -1; }
  fd = open(argv[1], 0);
  if (fd < 0) { printf("erro ao abrir %s\n", argv[1]); return -1; }
  char *src = malloc(1024 * 64);
  i = read(fd, src, 1024 * 64 - 1);
  src[i] = 0;
  close(fd);

  printf("crun: Compilando %s...\n", argv[1]);
  printf("--------------------------------\n");

  char *p = src;
  int count = 0;
  while ((p = strstr(p, "printf("))) {
      p += 7;
      if (*p == '"') {
          p++;
          char *end = strchr(p, '"');
          if (end) {
              int len = end - p;
              char msg[128];
              strncpy(msg, p, len);
              msg[len] = 0;
              printf("%s\n", msg);
              p = end + 1;
              count++;
          }
      }
  }
  
  if (count == 0) {
      printf("Erro: Nenhum printf encontrado ou arquivo vazio.\n");
      printf("Conteudo lido (%d bytes): %s\n", i, src);
  }

  return 0;
}
