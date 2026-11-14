#ifndef GUI_HTML_CSS_H
#define GUI_HTML_CSS_H

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTML_CSS_MAX_RULES      24576
#define HTML_CSS_INITIAL_RULES  128
#define HTML_CSS_MAX_VARS       2048
/* Component-heavy pages (for example GitHub PageLayout) inherit a sizeable
 * root custom-property set and add more variables on each nested region.
 * Keep a larger stylesheet-level token table, but a bounded per-element
 * environment so parser frames do not grow without bound. */
#define HTML_CSS_MAX_SCOPED_VARS 48
#define HTML_CSS_MAX_SEL_CHAIN  5
#define HTML_CSS_MAX_SEL_CLS    4
#define HTML_CSS_MAX_SEL_ATTRS  3
#define HTML_CSS_MAX_NOT_ATTRS   3
#define HTML_CSS_MAX_NODE_ATTRS 12
#define HTML_CSS_MAX_ATTR_NAMES 256
#define HTML_CSS_ATTR_VALUE_SNAPSHOT 64
#define HTML_CSS_INDEX_BUCKETS 1024
#define CSS_PSEUDO_HOVER         0x0001U
#define CSS_PSEUDO_FOCUS         0x0002U
#define CSS_PSEUDO_FOCUS_VISIBLE 0x0004U
#define CSS_PSEUDO_FOCUS_WITHIN  0x0008U
#define CSS_PSEUDO_ACTIVE        0x0010U
#define CSS_PSEUDO_CHECKED       0x0020U
#define CSS_PSEUDO_DISABLED      0x0040U
#define CSS_PSEUDO_ENABLED       0x0080U
#define HTML_CSS_LEN_AUTO       (-32767)
#define HTML_CSS_UNSET          (-32766)

typedef enum {
    CSS_DISP_DEFAULT = 0,
    CSS_DISP_NONE,
    CSS_DISP_BLOCK,
    CSS_DISP_INLINE,
    CSS_DISP_INLINE_BLOCK,
    CSS_DISP_FLEX,
    CSS_DISP_INLINE_FLEX,
    CSS_DISP_GRID,
    CSS_DISP_TABLE,
    CSS_DISP_TABLE_ROW,
    CSS_DISP_TABLE_CELL,
    CSS_DISP_LIST_ITEM
} css_display_t;

typedef enum {
    CSS_VIS_DEFAULT = 0,
    CSS_VIS_VISIBLE,
    CSS_VIS_HIDDEN
} css_vis_t;

typedef enum {
    CSS_ALIGN_INHERIT = 0,
    CSS_ALIGN_LEFT,
    CSS_ALIGN_CENTER,
    CSS_ALIGN_RIGHT
} css_text_align_t;

typedef enum {
    CSS_FLEX_ROW = 0,
    CSS_FLEX_COL
} css_flex_dir_t;

typedef enum {
    CSS_WRAP_NOWRAP = 0,
    CSS_WRAP_WRAP
} css_flex_wrap_t;

typedef enum {
    CSS_JUSTIFY_START = 0,
    CSS_JUSTIFY_CENTER,
    CSS_JUSTIFY_END,
    CSS_JUSTIFY_BETWEEN,
    CSS_JUSTIFY_AROUND
} css_justify_t;

typedef enum {
    CSS_AITEMS_STRETCH = 0,
    CSS_AITEMS_START,
    CSS_AITEMS_CENTER,
    CSS_AITEMS_END
} css_align_items_t;

typedef enum {
    CSS_FLOAT_NONE = 0,
    CSS_FLOAT_LEFT,
    CSS_FLOAT_RIGHT
} css_float_t;

typedef enum {
    CSS_POS_STATIC = 0,
    CSS_POS_RELATIVE,
    CSS_POS_ABSOLUTE,
    CSS_POS_FIXED
} css_pos_t;

#define HTML_CSS_MAX_GRID_COLS 4

typedef struct {
    uint64_t name_hash;
    uint64_t value_hash;
    uint32_t value_off;
    uint16_t value_len;
    uint8_t flags; /* has value, case-insensitive, operator */
} css_attr_selector_t;

typedef struct {
    uint64_t name_hash;
    uint64_t value_hash;
    uint64_t value_hash_ci;
    char value_prefix[HTML_CSS_ATTR_VALUE_SNAPSHOT];
    char value_suffix[HTML_CSS_ATTR_VALUE_SNAPSHOT];
    uint16_t value_len;
    uint8_t value_stored_len;
    bool has_value;
    bool value_truncated;
} css_node_attr_t;

typedef struct {
    uint64_t high;
    uint64_t low;
} css_token_hash_t;

typedef struct {
    char tag[24];
    css_token_hash_t id;
    css_token_hash_t classes[HTML_CSS_MAX_SEL_CLS];
    uint8_t class_count;
    css_token_hash_t not_classes[2];
    uint8_t not_class_count;
    int16_t nth_child_a;
    int16_t nth_child_b;
    bool nth_child_reverse;
    uint8_t child_pseudo; /* 1 first-child, 2 last-child, 3 only-child */
    int16_t nth_type_a;
    int16_t nth_type_b;
    bool nth_type_reverse;
    uint8_t type_pseudo; /* 1 first-of-type, 2 last-of-type, 3 only-of-type */
    css_attr_selector_t attrs[HTML_CSS_MAX_SEL_ATTRS];
    uint8_t attr_count;
    css_attr_selector_t forbidden_attrs[HTML_CSS_MAX_NOT_ATTRS];
    uint8_t forbidden_attr_count;
    uint8_t combinator; /* 0=self, 1=descendant, 2=child, 3=adjacent, 4=general sibling */
    uint16_t require_state;
    uint16_t forbid_state;
    uint8_t forbid_child_pseudo;
    int16_t forbid_nth_child_a;
    int16_t forbid_nth_child_b;
    bool forbid_nth_child_reverse;
    int16_t forbid_nth_type_a;
    int16_t forbid_nth_type_b;
    bool forbid_nth_type_reverse;
    uint8_t forbid_type_pseudo;
    bool require_empty;
    bool forbid_empty;
    bool has_nth_child;
    bool has_forbid_nth_child;
    bool has_nth_type;
    bool has_forbid_nth_type;
    bool has_id;
    bool require_hidden_attr;
    bool forbid_hidden_attr;
} css_simple_sel_t;

typedef struct {
    uint32_t parts_off;
    uint8_t part_count;
    uint16_t specificity;
    uint16_t order;
    uint32_t decl_off;
    uint32_t decl_len;
    uint64_t index_hash;
    uint32_t index_next; /* 1-based rule index; zero terminates */
    uint8_t index_kind;  /* 0 generic, 1 id, 2 class, 3 tag */
} css_rule_t;

typedef struct {
    char name[48];
    char value[96];
} css_var_t;

typedef struct {
    css_rule_t *rules;
    int rule_count;
    int rule_capacity;
    css_simple_sel_t *selector_parts;
    size_t selector_part_count;
    size_t selector_part_capacity;
    bool rules_truncated;
    bool needs_type_counts;
    uint32_t index_heads[3][HTML_CSS_INDEX_BUCKETS];
    uint32_t generic_head;
    uint64_t referenced_attr_hashes[HTML_CSS_MAX_ATTR_NAMES];
    uint64_t referenced_attr_bloom[4];
    uint16_t referenced_attr_count;
    css_var_t vars[HTML_CSS_MAX_VARS];
    int var_count;
    char *arena;
    size_t arena_len;
    size_t arena_cap;
    char *selector_arena;
    size_t selector_arena_len;
    size_t selector_arena_cap;
    int viewport_w;
    int viewport_h;
    bool prefer_dark_scheme;
} html_stylesheet_t;

typedef struct css_node_info {
    char tag[24];
    char id[40];
    char cls[256];
    uint16_t child_index;
    uint16_t sibling_count;
    uint16_t type_index;
    uint16_t type_count;
    css_node_attr_t attrs[HTML_CSS_MAX_NODE_ATTRS];
    uint8_t attr_count;
    uint16_t pseudo_state;
    const struct css_node_info *parent;
    const struct css_node_info *prev_sibling;
    bool hidden_attr;
    bool is_empty;
} css_node_info_t;

typedef struct {
    /* Inherited typography & color */
    bool has_color;
    bool own_color;
    lv_color_t color;
    int16_t font_px;        /* 0 = inherit */
    bool own_font_px;
    bool has_bold;
    bool bold;
    bool has_italic;
    bool italic;
    bool has_underline;
    bool own_underline;
    bool underline;
    bool has_strike;
    bool strike;
    css_text_align_t text_align;
    bool has_preformatted;
    bool preformatted;
    css_vis_t visibility;

    /* CSS custom properties inherit through the element tree. Keep a bounded
     * per-element environment so component-scoped variables don't leak across
     * unrelated selectors or siblings. */
    uint16_t scoped_var_count;

    /* Non-inherited box & layout */
    bool has_bg;
    bool bg_transparent;
    lv_color_t bg;

    bool has_border_color;
    lv_color_t border_color;
    int16_t border_w[4];    /* T, R, B, L (HTML_CSS_UNSET = unset) */
    int16_t pad[4];         /* T, R, B, L (HTML_CSS_UNSET = unset) */
    int16_t margin[4];      /* T, R, B, L (HTML_CSS_UNSET / HTML_CSS_LEN_AUTO) */
    int16_t radius;         /* HTML_CSS_UNSET = unset */
    bool border_box;        /* width/height include padding and borders */

    int16_t width_px;       /* HTML_CSS_UNSET / HTML_CSS_LEN_AUTO / >=0 */
    int16_t width_pct;      /* 0..100 or HTML_CSS_UNSET */
    int16_t max_width_px;
    int16_t max_width_pct;
    int16_t min_width_px;
    int16_t height_px;
    int16_t min_height_px;

    css_display_t display;
    css_flex_dir_t flex_dir;
    css_flex_wrap_t flex_wrap;
    css_justify_t justify_content;
    css_align_items_t align_items;
    int16_t gap;            /* HTML_CSS_UNSET = unset */
    int16_t row_gap;        /* HTML_CSS_UNSET falls back to gap/default */
    int16_t column_gap;     /* HTML_CSS_UNSET falls back to gap/default */
    uint8_t flex_grow;      /* non-inherited integer weight, 0 by default */
    int16_t flex_order;     /* order for flex/grid item placement */
    int16_t flex_basis_pct; /* bounded percentage basis for row flex items */
    bool flex_basis_zero;   /* explicit zero basis overrides width in row flex */
    /* Bounded subset of grid-template-columns: fixed, fractional and calc tracks. */
    uint8_t grid_col_count;
    uint8_t grid_col_kind[HTML_CSS_MAX_GRID_COLS]; /* 1=px, 2=%, 3=fr, 5=calc(% +/- px) */
    int16_t grid_col_value[HTML_CSS_MAX_GRID_COLS];
    int16_t grid_col_offset[HTML_CSS_MAX_GRID_COLS];
    uint8_t grid_col_start; /* one-based CSS grid line; 0 means auto placement */
    uint8_t grid_col_span;  /* bounded column span, defaults to 1 */

    css_float_t float_mode;
    bool clear_both;
    css_pos_t position_mode;
    bool offscreen_hidden;
    bool is_sr_only;
    bool opacity_zero;
    css_var_t scoped_vars[HTML_CSS_MAX_SCOPED_VARS];
} css_style_t;

html_stylesheet_t *html_css_create(int viewport_w);
void html_css_set_viewport_height(html_stylesheet_t *ss, int viewport_h);
void html_css_set_prefer_dark_scheme(html_stylesheet_t *ss, bool prefer_dark);
void html_css_destroy(html_stylesheet_t *ss);

/* Parses a stylesheet chunk (<style> or fetched .css file) and appends rules/vars. */
void html_css_add_stylesheet(html_stylesheet_t *ss, const char *css, size_t len);
bool html_css_reserve_selector_parts(html_stylesheet_t *ss, size_t capacity);

/* Evaluates CSS custom properties (--var) from rules matching ancestors (e.g. html, body). */
void html_css_collect_matched_vars(html_stylesheet_t *ss,
                                   const css_node_info_t *ancestors, int depth);

/* Initializes a style struct to clean defaults (inheriting typography from parent). */
void html_css_style_init(css_style_t *st, const css_style_t *parent);

/* Stores an HTML attribute in the bounded selector-matching snapshot. */
void html_css_node_add_attribute(css_node_info_t *node,
                                const char *name, size_t name_len,
                                const char *value, size_t value_len,
                                bool has_value);
bool html_css_attribute_is_referenced(const html_stylesheet_t *ss,
                                      const char *name, size_t name_len);

/* Computes cascaded CSS style for ancestors[depth - 1], then applies inline style attr. */
void html_css_compute(const html_stylesheet_t *ss,
                      const css_node_info_t *ancestors, int depth,
                      const char *inline_style,
                      const css_style_t *parent_style,
                      css_style_t *out_style);

#ifdef HTML_CSS_HOST_TEST
size_t html_css_host_candidate_count(const html_stylesheet_t *ss,
                                     const css_node_info_t *node);
size_t html_css_host_selector_part_capacity(const html_stylesheet_t *ss);
#endif

/* Parses a single CSS color expression (hex, rgb/rgba, named, var(...)). */
bool html_css_parse_color(const html_stylesheet_t *ss, const char *expr,
                          lv_color_t *out, bool *is_transparent);

#endif /* GUI_HTML_CSS_H */
