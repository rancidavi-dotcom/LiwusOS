#include "gui.h"
#include "video.h"

static int val = 0;
widget_t* win_calc;

void on_num_click(widget_t* self) {
    if (self->text[0] == 'C') val = 0;
    else val = self->text[0] - '0';
}

widget_t* init_calculator() {
    win_calc = create_window("Calculadora Auto", 400, 200, 200, 300);
    
    /* Adicionando botões com 1 linha de código cada! */
    add_widget(win_calc, create_button("1", 10, 50, 40, 40, on_num_click));
    add_widget(win_calc, create_button("2", 60, 50, 40, 40, on_num_click));
    add_widget(win_calc, create_button("C", 110, 50, 40, 40, on_num_click));

    return win_calc;
}
