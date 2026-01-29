# LiwusOS GUI — Layout Engine

## Objective

Document the layout engine that computes child positions and sizes within container nodes. Implements a Flexbox-inspired model with absolute, vertical box (VBOX), and horizontal box (HBOX) layout modes, plus alignment, padding, margin, and flex weight distribution.

---

## Problems Solved

- **Manual positioning**: Without a layout engine, every widget must set `local_x`/`local_y` explicitly, making dynamic UIs tedious.
- **Responsive resizing**: Flex weights distribute remaining space proportionally when containers resize.
- **Predictable alignment**: Four alignment modes (START, CENTER, END, STRETCH) for both axes.
- **Extensibility**: Custom layout via `node_vtable_t.layout` override.

---

## Architecture

```
layout_engine_compute(node)
  │
  ├─ if node->vtable->layout exists  → call custom layout
  │
  ├─ if node->layout_type == LAYOUT_VBOX → layout_vbox(node)
  │     └─ vertical stacking with flex weight distribution
  │
  ├─ if node->layout_type == LAYOUT_HBOX → layout_hbox(node)
  │     └─ horizontal stacking with flex weight distribution
  │
  └─ else (LAYOUT_ABSOLUTE) → for each child: layout_engine_compute(child)
       └─ no automatic positioning; child positions are explicit
```

Defined in `src/kernel/gui/layout/layout_engine.c`.

---

## Layout Types

| Enum              | Value | Behavior                                       |
|-------------------|-------|------------------------------------------------|
| `LAYOUT_ABSOLUTE` | 0     | No automatic positioning. Children placed at their `local_x`/`local_y`. Recurse only. |
| `LAYOUT_VBOX`     | 1     | Children stacked vertically. Height distributed by flex weight. |
| `LAYOUT_HBOX`     | 2     | Children stacked horizontally. Width distributed by flex weight. |

---

## Alignment

| Enum             | Value | VBOX (horizontal axis)     | HBOX (vertical axis)       |
|------------------|-------|----------------------------|----------------------------|
| `ALIGN_START`    | 0     | Left-aligned               | Top-aligned                |
| `ALIGN_CENTER`   | 1     | Horizontally centered      | Vertically centered        |
| `ALIGN_END`      | 2     | Right-aligned              | Bottom-aligned             |
| `ALIGN_STRETCH`  | 3     | Width fills container      | Height fills container     |

Alignment is set on each child via `node->layout_align`.

---

## VBOX Layout — Detailed Walkthrough

Based on `src/kernel/gui/layout/layout_engine.c:6`:

```
                    ┌──────────────────────────────────────┐
                    │         CONTAINER (VBOX)              │
                    │  padding[3]          padding[1]       │
                    │  ┌────────┬────────────────────┬──┐   │
                    │  │ margin │                    │  │   │
                    │  │ top    │                    │  │   │
                    │  │    ┌──────────────────┐    │  │   │
                    │  │    │  CHILD 0         │    │  │   │
                    │  │    │  fixed height    │    │  │   │
                    │  │    │  align=START     │    │  │   │
                    │  │    └──────────────────┘    │  │   │
                    │  │ margin bottom              │  │   │
                    │  ├─padding[0]──────────────── ├──┤   │
                    │  │ ┌────────────────────────┐│  │   │
                    │  │ │  CHILD 1              ││  │   │
                    │  │ │  flex_weight=1        ││  │   │
                    │  │ │  align=STRETCH        ││  │   │
                    │  │ └────────────────────────┘│  │   │
                    │  │                           │  │   │
                    │  ├─padding[2]──────────────── ├──┤   │
                    │  └────────────────────────────┘  │   │
                    └──────────────────────────────────────┘
                    │                                │
                    └────────────────────────────────┘
                         padding[0] = top padding
                         padding[2] = bottom padding
```

### Pre-pass

```c
int total_flex = 0;
int fixed_h = 0;
for each visible child {
    if child->flex_weight > 0:
        total_flex += child->flex_weight
    else:
        fixed_h += child->height + child->margin[0] + child->margin[2]
}

int available_h = node->height - node->padding[0] - node->padding[2]
int remaining_h = available_h - fixed_h  // space to distribute among flex children
```

### Child positioning loop

```c
int current_y = node->padding[0];

for each visible child {
    // 1. Compute height
    if child->flex_weight > 0:
        ch = (remaining_h * child->flex_weight) / total_flex
        ch -= (child->margin[0] + child->margin[2])
        child->height = ch

    // 2. Compute width and X based on alignment
    switch child->layout_align:
        ALIGN_START:   cx = padding[3] + child->margin[3]
        ALIGN_CENTER:  cx = padding[3] + (available_w - child->width) / 2
        ALIGN_END:     cx = container->width - padding[1] - child->margin[1] - child->width
        ALIGN_STRETCH: child->width = available_w - margins; cx = padding[3] + margin[3]

    // 3. Set Y
    int cy = current_y + child->margin[0]
    node_set_position(child, cx, cy)

    // 4. Advance cursor
    current_y = cy + ch + child->margin[2]

    // 5. Recurse into child's own children
    layout_engine_compute(child)
}
```

---

## HBOX Layout — Detailed Walkthrough

Based on `src/kernel/gui/layout/layout_engine.c:69`:

Mirror of VBOX: children stack horizontally, flex weight distributes width, alignment controls the cross-axis (Y).

### Pre-pass

```c
int total_flex = 0;
int fixed_w = 0;
for each visible child {
    if child->flex_weight > 0:
        total_flex += child->flex_weight
    else:
        fixed_w += child->width + child->margin[1] + child->margin[3]
}

int available_w = container->width - padding[1] - padding[3]
int remaining_w = available_w - fixed_w
```

### Child positioning loop

```c
int current_x = node->padding[3];

for each visible child {
    // 1. Width
    if child->flex_weight > 0:
        cw = (remaining_w * child->flex_weight) / total_flex
        cw -= (child->margin[1] + child->margin[3])
        child->width = cw

    // 2. Height and Y based on alignment
    switch child->layout_align:
        ALIGN_START:   cy = padding[0] + child->margin[0]
        ALIGN_CENTER:  cy = padding[0] + (available_h - child->height) / 2
        ALIGN_END:     cy = container->height - padding[2] - child->margin[2] - child->height
        ALIGN_STRETCH: child->height = available_h - margins; cy = padding[0] + margin[0]

    // 3. Set position
    int cx = current_x + child->margin[3]
    node_set_position(child, cx, cy)

    current_x = cx + cw + child->margin[1]
    layout_engine_compute(child)
}
```

---

## Padding & Margin Arrays

Both stored as `int[4]`: `[top, right, bottom, left]`

| Access      | VBOX Meaning                   | HBOX Meaning                   |
|-------------|--------------------------------|--------------------------------|
| `padding[0]`| Top padding (Y offset start)   | Top padding (Y offset cross-axis) |
| `padding[1]`| Right padding                  | Right padding (end of X axis)  |
| `padding[2]`| Bottom padding                 | Bottom padding                 |
| `padding[3]`| Left padding (X offset)        | Left padding (X offset start)  |
| `margin[0]` | Top margin on each child      | Top margin on each child       |
| `margin[1]` | Right margin on each child     | Right margin on each child     |
| `margin[2]` | Bottom margin on each child   | Bottom margin on each child     |
| `margin[3]` | Left margin on each child      | Left margin on each child      |

Note: Margins are **per-child** in the container's direction. They are added to the child's allocated space.

---

## Flex Weight

| Value | Meaning                                    |
|-------|--------------------------------------------|
| 0     | Fixed size — child keeps its `width`/`height` |
| >0    | Proportional — space distributed as `(child->flex_weight / total_flex) * remaining` |

Flex children are sized **after** fixed children. If all children have `flex_weight=0`, no space is distributed.

---

## Custom Layout Override

If a widget sets `node->vtable->layout`, the engine calls it **before** the built-in layout type dispatch. The custom handler can completely replace layout logic or augment it:

```c
static void my_custom_layout(node_t *self) {
    // Custom positioning logic
    for (uint32_t i = 0; i < self->child_count; i++) {
        node_t *child = self->children[i];
        // position child explicitly
        node_set_position(child, x, y);
    }
}

static const node_vtable_t my_vtable = {
    .layout = my_custom_layout,
    // ...
};
```

The custom layout is called first, then the built-in layout runs. To fully override, the custom function should set `self->layout_type` to `LAYOUT_ABSOLUTE` or handle all children itself.

---

## API

### Public

Defined in `src/kernel/gui/layout/layout_engine.h:14`:

```c
void layout_engine_compute(node_t *node);
```

- Recursively computes layout for `node` and all descendants.
- Respects visibility (skips invisible children).
- Clears `NODE_DIRTY_LAYOUT` after completion.

### Internal (private to `layout_engine.c`)

```c
static void layout_vbox(node_t *node);
static void layout_hbox(node_t *node);
```

---

## Dependencies

| Module         | Header              | Usage                              |
|----------------|---------------------|------------------------------------|
| Node           | `scene/node.h`      | Layout type, alignment, padding/margin fields, `node_set_position` |
| Rect           | `math/rect.h`       | (indirect, via node API)          |

---

## Limitations & Trade-offs

- **Single-pass**: Layout is computed once per call. No constraint solving or "intrinsic size" pass.
- **No wrap**: VBOX/HBOX do not wrap children to new lines/columns. Overflow is not handled.
- **No min/max size**: Children cannot specify minimum or maximum dimensions.
- **No gap shorthand**: Margins serve as gaps but are per-child, not container-level.
- **Integer-only**: All calculations use integer arithmetic. Fractional layout not supported.
- **No automatic layout on change**: Layout is not triggered by property changes automatically; the caller must invoke `layout_engine_compute()`.
- **No baseline alignment**: Text baselines are not aligned across children.

---

## Performance

- **O(n)** per container: one pass per child. Flex weight computation is one division per flex child.
- **No memoization**: Layout is recomputed fully on each call.
- **No cache invalidation**: Caller is responsible for calling `layout_engine_compute()` only when layout properties change.

---

## Future Extensions

- **`LAYOUT_GRID`**: Grid layout with rows/columns and cell spanning.
- **`LAYOUT_FLOW`**: Flow layout with wrapping to next row/column on overflow.
- **Auto-size containers**: Container height computed from children heights (shrink-wrap).
- **Min/max constraints**: `min_width`, `max_height` fields on node.
- **Gap property**: Container-level `gap` shorthand instead of per-child margins.
- **Anchored children**: Pin children to edges (dock/anchoring).
- **Layout transitions**: Animate layout changes (e.g., `node_set_size` triggers animated layout recomputation).

---

## Usage Examples

### VBOX with flex

```c
node_t *container = panel_create("box", 0, 0, 300, 400, 0x00000000);
container->layout_type = LAYOUT_VBOX;
container->padding[0] = 8;  container->padding[1] = 8;
container->padding[2] = 8;  container->padding[3] = 8;

node_t *header = label_create("hdr", 0, 0, "Header", 0xFFFFFFFF);
header->margin[2] = 4;
header->layout_align = ALIGN_CENTER;

node_t *content = panel_create("content", 0, 0, 0, 0, 0x881E293B);
content->flex_weight = 1;         // takes remaining vertical space
content->layout_align = ALIGN_STRETCH;  // fills width
content->margin[2] = 4;

node_t *footer = button_create("ok", 0, 0, 80, 28, "OK");
footer->layout_align = ALIGN_END;

node_add_child(container, header);
node_add_child(container, content);
node_add_child(container, footer);

layout_engine_compute(container);
```

### Absolute layout (manual)

```c
node->layout_type = LAYOUT_ABSOLUTE;
// Children will use their explicit local_x/local_y
// layout_engine_compute just recurses without repositioning
```