#include <stdio.h>
#include <stdint.h>

#define TRANSFORM_SCALE 65536

typedef struct {
    int32_t a, b, c, d, tx, ty;
} gui_transform_t;

int main() {
    gui_transform_t local = {TRANSFORM_SCALE, 0, 0, TRANSFORM_SCALE, 100, 100};
    gui_transform_t parent = {TRANSFORM_SCALE, 0, 0, TRANSFORM_SCALE, 0, 0};
    gui_transform_t r;
    r.a  = (int32_t)(((int64_t)parent.a * local.a + (int64_t)parent.c * local.b) / TRANSFORM_SCALE);
    r.tx = (int32_t)(((int64_t)parent.a * local.tx + (int64_t)parent.c * local.ty) / TRANSFORM_SCALE) + parent.tx;
    
    int32_t px = 0, py = 0;
    int out_x = (int32_t)(((int64_t)r.a * px + (int64_t)r.c * py) / TRANSFORM_SCALE) + r.tx;
    
    printf("r.a = %d, r.tx = %d, out_x = %d\n", r.a, r.tx, out_x);
    return 0;
}
