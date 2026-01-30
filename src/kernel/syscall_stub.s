/* Adicionar ao final do interrupt.s ou em arquivo separado se preferir */

.global isr128
isr128:
    cli
    push $0        /* Dummy error code */
    push $128      /* Interrupt number */
    jmp isr_common_stub

/* Assumindo que isr_common_stub já existe no arquivo original e faz 'pusha', etc. 
   Vamos verificar se precisamos salvar mais alguma coisa, mas o padrão pusha é suficiente. */
