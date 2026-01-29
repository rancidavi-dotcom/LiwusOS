# Estado da Implementação e Arquitetura — Iteração 2

Este documento detalha as atualizações arquiteturais, o ciclo de vida e o estado real da infraestrutura gráfica do LiwusOS, conforme implementado na Iteração 2.

## 1. Arquitetura Atualizada

### Fluxos de Execução
O sistema baseia-se no **Scene Graph (`node_t`)** como única fonte de verdade.
1. **Input**: `input_manager.c` traduz interações de hardware (mouse/teclado) e as posta no `event_bus.c`.
2. **Focus / Ferramentas**: `focus_manager.c` intercepta o input.
   - Pressionar `TAB` invoca o `app_registry_show_launcher()`.
   - Clicar sobre um nó altera o foco (`GUI_EVENT_WIN_FOCUS`) e dispara o `window_manager_bring_to_front()`, realocando o nó para o fim do array do root (Z-Order máximo).
3. **Ferramenta de Pan**: `pan_tool.c` reage a `WASD` e setas direcionais para alterar o `pos_x/pos_y` da câmera, movendo o "Infinite Canvas".
4. **Ciclo de Vida das Janelas**: Cada janela é um `node_t` de tipo `NODE_WINDOW`.

## 2. Subsistema GUI e Janelas

### Ciclo de Vida Real
* **Criação**: `window_node_create()` aloca o nó e `window_node_data_t` (que guarda estado local como `dragging`).
* **Arrasto (Drag)**: Ao receber `MOUSE_DOWN` especificamente no retângulo da `Titlebar`, a flag `dragging` é ativada. O movimento subsequente (`MOUSE_MOVE`) atualiza o offset mundial da janela (descontando o nível de zoom atual).
* **Foco e Z-Order**: O clique também dispara a lógica de foco. O `window_manager.c` escuta isso e reordena os filhos do root, trazendo a janela selecionada para frente.
* **Destruição Segura**:
  - Clique no botão de fechar posta `GUI_EVENT_WIN_CLOSE`.
  - O `window_manager.c` retira o nó do Scene Graph e marca o pai para *repaint*.
  - A destruição chama `node_destroy()`, que, por design, **destrói recursivamente todos os nós filhos**, liberando os botões, labels e a própria estrutura primária.

### Gerenciamento do Canvas
A câmera opera de modo global. Todos os nós calculam a projeção para tela através de:
`screen_position = (world_position - camera_position) * zoom`

## 3. Memória

* **Estruturas Criadas**: `app_registry.c` possui um array estático que lista os ponteiros de funções para lançadores.
* **Ownership**: O nó pai ("Canvas") é dono de seus nós filhos. Se uma janela é fechada, o próprio gerenciador remove o ponteiro e o `node_destroy` cuida do free recursivo, prevenindo dangling pointers e memory leaks.

## 4. Estado Real do Projeto

### Implementado (Totalmente Funcional)
- **Sistema de Câmera (Pan)**: Deslocamento via `WASD`, Setas, SCROLL e Click do Meio no mouse.
- **Sistema de Multijanela**: Múltiplas janelas através da árvore de nós, mantendo independência de estado, callback e renderização.
- **Arrastar e Focar**: Janelas podem ser arrastadas pela *titlebar*. O Z-order é preservado e atualizado corretamente.
- **Gerenciador de Aplicativos (TAB)**: O sistema de registro de apps (`app_registry.c`) renderiza um menu flutuante.
- **Destruição Segura**: O botão de fechar destrói fisicamente o nó e seus componentes da memória e da tela.

### Não Implementado (Planejado)
- **Desvinculação PID (Task Manager)**: Quando uma thread *userspace* for morta de fora, o Kernel ainda precisará de um callback para varrer o Scene Graph removendo janelas daquele PID.
- **Busca no Launcher**: O launcher lista botões via `VBOX`, mas ainda não permite digitação/filtro por texto.

### Bugs Conhecidos
- Animações e movimentações intensas do Canvas podem gerar atrasos na captura do ponteiro caso a taxa de polling do hardware caia, exigindo otimização de *dirty rects* futura.
