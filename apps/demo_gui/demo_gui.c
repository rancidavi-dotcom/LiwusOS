#include <stdint.h>
#include <stdlib.h>
#include <liwus_gui.h>
#include <libliw.h>

int errno = 0;
int* __errno(void) { return &errno; }

int main() {
    // 1. Cria a janela base (Canvas)
    Canvas canvas = canvas_create(400, 300, "App Demo GUI");
    if (!canvas) {
        exit(1);
    }
    
    // 2. Cria alguns nodes usando a nova API
    Node title = text_create("Aplicativo User Space!");
    node_move(title, 20, 20);
    
    Node btn = button_create("Botão Nativo");
    node_move(btn, 20, 60);
    
    Node panel = panel_create();
    node_move(panel, 20, 110);
    
    // 3. Adiciona na janela
    canvas_add(canvas, title);
    canvas_add(canvas, btn);
    canvas_add(canvas, panel);
    
    // Loop infinito apenas para manter o processo vivo.
    // Como a renderização é no Kernel (Compositor), não precisamos 
    // de lgx_refresh ou loop de desenho!
    while (1) {
        for (volatile int i = 0; i < 1000000; i++) {}
    }
    
    return 0;
}
