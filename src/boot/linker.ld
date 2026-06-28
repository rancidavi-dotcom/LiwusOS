/* O ponto de entrada definido no boot.s */
ENTRY(_start)

SECTIONS
{
	/* O kernel começa em 1MB de memória física */
	. = 1M;

	/* Primeiro coloque o cabeçalho multiboot, pois ele é obrigatório nos primeiros 8KB */
	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)
		*(.text)
	}

	/* Dados somente leitura (strings, constantes) */
	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	}

	/* Dados inicializados de leitura e escrita */
	.data BLOCK(4K) : ALIGN(4K)
	{
		*(.data)
	}

	/* Dados não inicializados (e a pilha) */
	.bss BLOCK(4K) : ALIGN(4K)
	{
		*(COMMON)
		*(.bss)
	}

    end = .;

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.interp)
    }
}
