#ifndef BROWSER_H
#define BROWSER_H

#include <stdint.h>

/**
 * LiwusOS Browser CLI API
 */

/* Executa uma requisição web ou busca e retorna o conteúdo formatado em texto
 */
void browser_cli_execute(const char *input, char *output, uint32_t max_len);

#endif
