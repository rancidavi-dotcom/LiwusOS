#include "gui/html_css.h"
#include "kheap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kmalloc(size_t size) { return malloc(size); }
void *krealloc(void *ptr, size_t size) { return realloc(ptr, size); }
void kfree(void *ptr) { free(ptr); }

int main(void) {
    html_stylesheet_t *reserved = html_css_create(800);
    if (!reserved) return 20;
    if (html_css_reserve_selector_parts(NULL, 8) ||
        !html_css_reserve_selector_parts(reserved, 8) ||
        html_css_host_selector_part_capacity(reserved) < 8 ||
        !html_css_reserve_selector_parts(reserved, 4) ||
        html_css_host_selector_part_capacity(reserved) < 8) {
        html_css_destroy(reserved);
        fprintf(stderr, "CSS selector-part reservation failed\n");
        return 21;
    }
    static const char reserved_css[] = "html body main{font-weight:bold}";
    html_css_add_stylesheet(reserved, reserved_css, sizeof(reserved_css) - 1);
    css_node_info_t reserved_path[3] = {0};
    strcpy(reserved_path[0].tag, "html");
    strcpy(reserved_path[1].tag, "body");
    strcpy(reserved_path[2].tag, "main");
    css_style_t reserved_style;
    html_css_compute(reserved, reserved_path, 3, NULL, NULL, &reserved_style);
    size_t reserved_capacity = html_css_host_selector_part_capacity(reserved);
    html_css_destroy(reserved);
    if (!reserved_style.bold || reserved_capacity < 8) {
        fprintf(stderr, "CSS parse after selector-part reservation failed\n");
        return 22;
    }

    static const char css[] =
        "li:nth-child(odd){display:none}"
        "li:nth-child(even){visibility:hidden}"
        "li:nth-child(-n+3){font-weight:bold}"
        "li:nth-child(3n+2){text-decoration:underline}";
    html_stylesheet_t *sheet = html_css_create(800);
    if (!sheet) return 1;
    html_css_add_stylesheet(sheet, css, sizeof(css) - 1);

    for (uint16_t index = 1; index <= 7; ++index) {
        css_node_info_t ancestors[3] = {0};
        strcpy(ancestors[0].tag, "html");
        strcpy(ancestors[1].tag, "ul");
        strcpy(ancestors[2].tag, "li");
        ancestors[2].child_index = index;
        css_style_t style;
        html_css_compute(sheet, ancestors, 3, NULL, NULL, &style);

        bool odd = (index % 2) != 0;
        bool first_three = index <= 3;
        bool every_third_from_two = index == 2 || index == 5;
        if ((style.display == CSS_DISP_NONE) != odd ||
            (style.visibility == CSS_VIS_HIDDEN) == odd ||
            (style.bold != first_three) ||
            (style.underline != every_third_from_two)) {
            fprintf(stderr, "CSS nth-child mismatch at child %u\n", index);
            html_css_destroy(sheet);
            return 2;
        }
    }

    html_css_destroy(sheet);

    static const char attr_css[] =
        "html:where([data-color-mode=auto][data-light-theme=light]){font-weight:bold}"
        "[aria-hidden=\"TRUE\" i]{font-style:italic}"
        "[hidden]{text-decoration:underline}"
        "html:hover{color:#123456}"
        "html:not(:hover){text-decoration:underline}";
    sheet = html_css_create(800);
    if (!sheet) return 3;
    html_css_add_stylesheet(sheet, attr_css, sizeof(attr_css) - 1);
    css_node_info_t themed = {0};
    strcpy(themed.tag, "html");
    html_css_node_add_attribute(&themed, "data-color-mode", 15,
                                "auto", 4, true);
    html_css_node_add_attribute(&themed, "data-light-theme", 16,
                                "light", 5, true);
    html_css_node_add_attribute(&themed, "aria-hidden", 11,
                                "true", 4, true);
    html_css_node_add_attribute(&themed, "hidden", 6, NULL, 0, false);
    themed.hidden_attr = true;
    css_style_t attr_style;
    html_css_compute(sheet, &themed, 1, NULL, NULL, &attr_style);
    html_css_destroy(sheet);
    if (!attr_style.bold || !attr_style.italic || !attr_style.underline ||
        attr_style.has_color) {
        fprintf(stderr, "CSS where/attribute/pseudo matching failed: bold=%d italic=%d underline=%d color=%d\n",
                attr_style.bold, attr_style.italic, attr_style.underline, attr_style.has_color);
        return 4;
    }

    static const char state_css[] =
        "div:hover{font-weight:bold}"
        "div:not(:hover){font-style:italic}";
    sheet = html_css_create(800);
    if (!sheet) return 13;
    html_css_add_stylesheet(sheet, state_css, sizeof(state_css) - 1);
    css_node_info_t state_node = {0};
    strcpy(state_node.tag, "div");
    css_style_t state_style;
    html_css_compute(sheet, &state_node, 1, NULL, NULL, &state_style);
    if (state_style.bold || !state_style.italic) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS inactive hover state matching failed\n");
        return 14;
    }
    state_node.pseudo_state = CSS_PSEUDO_HOVER;
    html_css_compute(sheet, &state_node, 1, NULL, NULL, &state_style);
    html_css_destroy(sheet);
    if (!state_style.bold || state_style.italic) {
        fprintf(stderr, "CSS active hover state matching failed\n");
        return 15;
    }

    static const char child_css[] =
        "li:first-child{font-weight:bold}"
        "li:last-child{font-style:italic}"
        "li:only-child{visibility:hidden}";
    sheet = html_css_create(800);
    if (!sheet) return 9;
    html_css_add_stylesheet(sheet, child_css, sizeof(child_css) - 1);
    css_node_info_t child = {0};
    strcpy(child.tag, "li");
    child.child_index = 1;
    child.sibling_count = 3;
    css_style_t child_style;
    html_css_compute(sheet, &child, 1, NULL, NULL, &child_style);
    if (!child_style.bold || child_style.italic ||
        child_style.visibility == CSS_VIS_HIDDEN) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS first-child selector matching failed\n");
        return 10;
    }
    child.child_index = 3;
    html_css_compute(sheet, &child, 1, NULL, NULL, &child_style);
    if (child_style.bold || !child_style.italic) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS last-child selector matching failed\n");
        return 11;
    }
    child.child_index = 1;
    child.sibling_count = 1;
    html_css_compute(sheet, &child, 1, NULL, NULL, &child_style);
    html_css_destroy(sheet);
    if (!child_style.bold || !child_style.italic ||
        child_style.visibility != CSS_VIS_HIDDEN) {
        fprintf(stderr, "CSS only-child selector matching failed\n");
        return 12;
    }

    static const char case_css[] =
        ".MiXeD{font-weight:bold}.mixed{font-style:italic}"
        "div:where([data-case=Foo]){text-decoration:underline}"
        "div:where([data-case=foo]){visibility:hidden}";
    sheet = html_css_create(800);
    if (!sheet) return 7;
    html_css_add_stylesheet(sheet, case_css, sizeof(case_css) - 1);
    css_node_info_t case_node = {0};
    strcpy(case_node.tag, "div");
    strcpy(case_node.cls, "MiXeD");
    html_css_node_add_attribute(&case_node, "data-case", 9, "Foo", 3, true);
    css_style_t case_style;
    html_css_compute(sheet, &case_node, 1, NULL, NULL, &case_style);
    html_css_destroy(sheet);
    if (!case_style.bold || case_style.italic || !case_style.underline ||
        case_style.visibility == CSS_VIS_HIDDEN) {
        fprintf(stderr, "CSS class selector case sensitivity failed\n");
        return 8;
    }

    static const char long_class_css[] =
        ".Primer_Brand__FeatureComparisonTable-module__FeatureComparisonTable__itemContent--hasLabel___YTcaZ{font-weight:bold}";
    sheet = html_css_create(800);
    if (!sheet) return 16;
    html_css_add_stylesheet(sheet, long_class_css, sizeof(long_class_css) - 1);
    css_node_info_t long_class_node = {0};
    strcpy(long_class_node.tag, "div");
    strcpy(long_class_node.cls,
           "Primer_Brand__FeatureComparisonTable-module__FeatureComparisonTable__itemContent--hasLabel___YTcaZ");
    css_style_t long_class_style;
    html_css_compute(sheet, &long_class_node, 1, NULL, NULL, &long_class_style);
    html_css_destroy(sheet);
    if (!long_class_style.bold) {
        fprintf(stderr, "CSS selector hash failed for long GitHub class name\n");
        return 17;
    }

    static const char sibling_css[] =
        ".label + .field{font-weight:bold}"
        ".label ~ .button{font-style:italic}"
        ".missing + .field{text-decoration:underline}"
        ".label + + .field{font-size:9px}"
        ".row > .label + .field{visibility:hidden}";
    sheet = html_css_create(800);
    if (!sheet) return 20;
    html_css_add_stylesheet(sheet, sibling_css, sizeof(sibling_css) - 1);
    css_node_info_t row = {0}, label = {0}, field = {0};
    strcpy(row.tag, "div");
    strcpy(row.cls, "row");
    strcpy(label.tag, "span");
    strcpy(label.cls, "label");
    label.parent = &row;
    strcpy(field.tag, "input");
    strcpy(field.cls, "field");
    field.parent = &row;
    field.prev_sibling = &label;
    css_node_info_t field_chain[] = {row, label, field};
    css_style_t sibling_style;
    html_css_compute(sheet, field_chain, 3, NULL, NULL, &sibling_style);
    if (!sibling_style.bold || sibling_style.underline ||
        sibling_style.font_px == 9 ||
        sibling_style.visibility != CSS_VIS_HIDDEN) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS adjacent sibling / chained selector matching failed\n");
        return 21;
    }

    css_node_info_t skipped = {0}, button = {0};
    strcpy(skipped.tag, "span");
    strcpy(button.tag, "button");
    strcpy(button.cls, "button");
    skipped.parent = &row;
    skipped.prev_sibling = &label;
    button.parent = &row;
    button.prev_sibling = &skipped;
    css_node_info_t button_chain[] = {row, button};
    html_css_compute(sheet, button_chain, 2, NULL, NULL, &sibling_style);
    if (!sibling_style.italic || sibling_style.bold || sibling_style.underline) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS general sibling or adjacent negative matching failed\n");
        return 22;
    }
    html_css_destroy(sheet);

    static const char of_type_css[] =
        "p:nth-of-type(2n){font-weight:bold}"
        "p:not(:nth-of-type(odd)){font-style:italic}"
        "span:first-of-type{text-decoration:underline}"
        "span:last-of-type{visibility:hidden}"
        "em:only-of-type{font-weight:bold}"
        "em:empty{font-style:italic}"
        "em:not(:empty){text-decoration:underline}";
    sheet = html_css_create(800);
    if (!sheet) return 23;
    html_css_add_stylesheet(sheet, of_type_css, sizeof(of_type_css) - 1);
    css_node_info_t p_second = {0};
    strcpy(p_second.tag, "p");
    p_second.child_index = 3;
    p_second.sibling_count = 4;
    p_second.type_index = 2;
    p_second.type_count = 2;
    css_style_t type_style;
    html_css_compute(sheet, &p_second, 1, NULL, NULL, &type_style);
    if (!type_style.bold || !type_style.italic) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS nth-of-type/:not matching failed\n");
        return 24;
    }
    css_node_info_t span_first = {0};
    strcpy(span_first.tag, "span");
    span_first.type_index = 1;
    span_first.type_count = 2;
    html_css_compute(sheet, &span_first, 1, NULL, NULL, &type_style);
    if (!type_style.underline || type_style.visibility == CSS_VIS_HIDDEN) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS first-of-type matching failed\n");
        return 25;
    }
    css_node_info_t span_last = span_first;
    span_last.type_index = 2;
    html_css_compute(sheet, &span_last, 1, NULL, NULL, &type_style);
    if (type_style.underline || type_style.visibility != CSS_VIS_HIDDEN) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS last-of-type matching failed\n");
        return 26;
    }
    css_node_info_t only_em = {0};
    strcpy(only_em.tag, "em");
    only_em.type_index = only_em.type_count = 1;
    only_em.is_empty = true;
    html_css_compute(sheet, &only_em, 1, NULL, NULL, &type_style);
    if (!type_style.bold || !type_style.italic || type_style.underline) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS :empty matching failed\n");
        return 27;
    }
    only_em.is_empty = false;
    html_css_compute(sheet, &only_em, 1, NULL, NULL, &type_style);
    html_css_destroy(sheet);
    if (!type_style.bold || type_style.italic || !type_style.underline) {
        fprintf(stderr, "CSS :not(:empty) matching failed\n");
        return 28;
    }

    static const char nth_last_css[] =
        "li:nth-last-child(odd){font-weight:bold}"
        "li:nth-last-of-type(3){font-style:italic}"
        "li:not(:nth-last-child(even)){text-decoration:underline}";
    sheet = html_css_create(800);
    if (!sheet) return 29;
    html_css_add_stylesheet(sheet, nth_last_css, sizeof(nth_last_css) - 1);
    css_node_info_t from_end = {0};
    strcpy(from_end.tag, "li");
    from_end.child_index = 3;
    from_end.sibling_count = 5;
    from_end.type_index = 2;
    from_end.type_count = 4;
    css_style_t from_end_style;
    html_css_compute(sheet, &from_end, 1, NULL, NULL, &from_end_style);
    if (!from_end_style.bold || !from_end_style.italic || !from_end_style.underline) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS nth-last-child/of-type matching failed\n");
        return 30;
    }
    from_end.child_index = 4;
    html_css_compute(sheet, &from_end, 1, NULL, NULL, &from_end_style);
    html_css_destroy(sheet);
    if (from_end_style.bold || from_end_style.underline) {
        fprintf(stderr, "CSS :not(:nth-last-child()) exclusion failed\n");
        return 31;
    }

    static const char not_css[] =
        ".item:not([disabled]){font-weight:bold}"
        ".item:not(:first-child){font-style:italic}"
        ".item:not(:nth-child(odd)){text-decoration:underline}";
    sheet = html_css_create(800);
    if (!sheet) return 18;
    html_css_add_stylesheet(sheet, not_css, sizeof(not_css) - 1);
    css_node_info_t not_node = {0};
    strcpy(not_node.tag, "div");
    strcpy(not_node.cls, "item");
    not_node.child_index = 2;
    not_node.sibling_count = 3;
    css_style_t not_style;
    html_css_compute(sheet, &not_node, 1, NULL, NULL, &not_style);
    if (!not_style.bold || !not_style.italic || !not_style.underline) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS :not attribute/first-child matching failed\n");
        return 19;
    }
    html_css_node_add_attribute(&not_node, "disabled", 8, NULL, 0, false);
    html_css_compute(sheet, &not_node, 1, NULL, NULL, &not_style);
    html_css_destroy(sheet);
    if (not_style.bold || !not_style.italic || !not_style.underline) {
        fprintf(stderr, "CSS :not attribute exclusion failed\n");
        return 20;
    }

    /* GitHub's current public repository page contains enough CSS rules to
     * exceed the former fixed 6,144-rule ceiling. Exercise dynamic growth. */
    const size_t repeated_rules = 16500;
    const char rule[] = ".growth{width:10px;}";
    const char final_rule[] = ".growth{width:20px;font-weight:bold;}";
    size_t source_capacity = repeated_rules * 40U + sizeof(rule) +
                             sizeof(final_rule);
    char *large_css = malloc(source_capacity);
    if (!large_css) return 5;
    size_t used = 0;
    for (size_t i = 0; i < repeated_rules; ++i) {
        char noise_rule[64];
        int noise_len = snprintf(noise_rule, sizeof(noise_rule),
                                 ".noise%05zu{width:10px;}", i);
        if (noise_len <= 0 || (size_t)noise_len >= sizeof(noise_rule) ||
            used + (size_t)noise_len >= source_capacity) {
            free(large_css);
            return 10;
        }
        memcpy(large_css + used, noise_rule, (size_t)noise_len);
        used += (size_t)noise_len;
    }
    memcpy(large_css + used, rule, sizeof(rule) - 1);
    used += sizeof(rule) - 1;
    memcpy(large_css + used, final_rule, sizeof(final_rule));
    used += sizeof(final_rule) - 1;

    sheet = html_css_create(800);
    if (!sheet) { free(large_css); return 6; }
    html_css_add_stylesheet(sheet, large_css, used);
    free(large_css);
    css_node_info_t target = {0};
    strcpy(target.tag, "div");
    strcpy(target.cls, "growth");
    css_style_t grown_style;
    html_css_compute(sheet, &target, 1, NULL, NULL, &grown_style);
    size_t candidate_count = html_css_host_candidate_count(sheet, &target);
    int large_rule_count = sheet->rule_count;
    bool growth_truncated = sheet->rules_truncated;
    html_css_destroy(sheet);
    if (!grown_style.bold || grown_style.width_px != 20 || growth_truncated ||
        candidate_count != 2) {
        fprintf(stderr, "CSS rule indexing/order failed after 16,384 rules (width=%d candidates=%zu)\n",
                grown_style.width_px, candidate_count);
        return 7;
    }

    static const char attr_ops_css[] =
        "[data-token~=Beta]{font-weight:bold}"
        "[lang|=en]{text-decoration:underline}"
        "[data-prefix^=pre]{font-style:italic}"
        "[href$=tail]{visibility:hidden}"
        "[data-sub*=midd]{display:none}"
        "[data-icon$=\\#gh-light-mode-only]{width:33px}"
        "[aria-label*=bar i]{text-decoration:line-through}";
    sheet = html_css_create(800);
    if (!sheet) return 8;
    html_css_add_stylesheet(sheet, attr_ops_css, sizeof(attr_ops_css) - 1);
    css_node_info_t attrs_node = {0};
    strcpy(attrs_node.tag, "a");
    html_css_node_add_attribute(&attrs_node, "data-token", 10,
                                "Alpha Beta Gamma", 16, true);
    html_css_node_add_attribute(&attrs_node, "lang", 4, "en-US", 5, true);
    html_css_node_add_attribute(&attrs_node, "data-prefix", 11,
                                "prefix-value", 12, true);
    html_css_node_add_attribute(&attrs_node, "href", 4,
                                "https://example.test/tail", 25, true);
    html_css_node_add_attribute(&attrs_node, "data-sub", 8,
                                "a-long-middle-value", 19, true);
    html_css_node_add_attribute(&attrs_node, "data-icon", 9,
                                "https://example.test/#gh-light-mode-only", 40, true);
    html_css_node_add_attribute(&attrs_node, "aria-label", 10,
                                "Open BAR menu", 13, true);
    css_style_t attr_ops_style;
    html_css_compute(sheet, &attrs_node, 1, NULL, NULL, &attr_ops_style);
    if (!attr_ops_style.bold || !attr_ops_style.underline ||
        !attr_ops_style.italic || attr_ops_style.visibility != CSS_VIS_HIDDEN ||
        attr_ops_style.display != CSS_DISP_NONE || !attr_ops_style.strike ||
        attr_ops_style.width_px != 33) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS attribute operator matching failed\n");
        return 9;
    }
    css_node_info_t near_miss_node = {0};
    strcpy(near_miss_node.tag, "a");
    html_css_node_add_attribute(&near_miss_node, "data-token", 10,
                                "Alpha BetaMax", 13, true);
    css_style_t near_miss_style;
    html_css_compute(sheet, &near_miss_node, 1, NULL, NULL, &near_miss_style);
    if (near_miss_style.bold) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS attribute word operator accepted a partial token\n");
        return 14;
    }
    html_css_destroy(sheet);

    const size_t dense_rule_count = 120;
    const char early_declaration[] = ".dense{width:33px;}";
    const char later_declaration[] = ".dense{font-weight:bold;}";
    size_t dense_capacity = sizeof(early_declaration) +
        dense_rule_count * (sizeof(later_declaration) - 1);
    char *dense_css = malloc(dense_capacity);
    if (!dense_css) return 11;
    size_t dense_len = 0;
    memcpy(dense_css + dense_len, early_declaration, sizeof(early_declaration) - 1);
    dense_len += sizeof(early_declaration) - 1;
    for (size_t i = 0; i < dense_rule_count; ++i) {
        memcpy(dense_css + dense_len, later_declaration,
               sizeof(later_declaration) - 1);
        dense_len += sizeof(later_declaration) - 1;
    }
    sheet = html_css_create(800);
    if (!sheet) { free(dense_css); return 12; }
    html_css_add_stylesheet(sheet, dense_css, dense_len);
    free(dense_css);
    css_node_info_t dense_node = {0};
    strcpy(dense_node.tag, "div");
    strcpy(dense_node.cls, "dense");
    css_style_t dense_style;
    html_css_compute(sheet, &dense_node, 1, NULL, NULL, &dense_style);
    html_css_destroy(sheet);
    if (dense_style.width_px != 33 || !dense_style.bold) {
        fprintf(stderr, "CSS dense matching discarded earlier declarations\n");
        return 13;
    }

    static const char flex_css[] =
        ".grow{flex-grow:2}"
        "button{flex:1 1 auto}"
        ".full-row{flex:100%}"
        ".basis-row{flex:1 1 75%}"
        ".zero-basis{flex:1 1 0;width:100%;min-width:1px}"
        ".single-grow{flex:1}"
        ".no-shrink-auto{flex:1 0 auto}"
        ".no-shrink-default{flex:1 0}"
        ".reset-basis{flex:100%;flex:1 1 auto}"
        ".reset{flex:none}"
        "input{width:100%;max-width:240px}";
    sheet = html_css_create(800);
    if (!sheet) return 14;
    html_css_add_stylesheet(sheet, flex_css, sizeof(flex_css) - 1);
    css_node_info_t flex_node = {0};
    strcpy(flex_node.tag, "div");
    strcpy(flex_node.cls, "grow");
    css_style_t flex_style;
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (flex_style.flex_grow != 2) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS flex-grow property mismatch: %u\n", flex_style.flex_grow);
        return 15;
    }
    memset(&flex_node, 0, sizeof(flex_node));
    strcpy(flex_node.tag, "button");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (flex_style.flex_grow != 1) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS flex shorthand grow mismatch: %u\n", flex_style.flex_grow);
        return 16;
    }
    memset(&flex_node, 0, sizeof(flex_node));
    strcpy(flex_node.tag, "div");
    strcpy(flex_node.cls, "full-row");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (flex_style.flex_grow != 1 || flex_style.flex_basis_pct != 100) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS flex:100%% basis mismatch: grow=%u basis=%d\n",
                flex_style.flex_grow, flex_style.flex_basis_pct);
        return 23;
    }
    memset(&flex_node, 0, sizeof(flex_node));
    strcpy(flex_node.tag, "div");
    strcpy(flex_node.cls, "basis-row");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (flex_style.flex_grow != 1 || flex_style.flex_basis_pct != 75) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS flex percentage basis mismatch: grow=%u basis=%d\n",
                flex_style.flex_grow, flex_style.flex_basis_pct);
        return 24;
    }
    memset(&flex_node, 0, sizeof(flex_node));
    strcpy(flex_node.tag, "div");
    strcpy(flex_node.cls, "zero-basis");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (!flex_style.flex_basis_zero || flex_style.width_pct != 100 ||
        flex_style.min_width_px != 1) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS zero flex basis lost precedence over width/min-width\n");
        return 26;
    }
    memset(&flex_node, 0, sizeof(flex_node));
    strcpy(flex_node.tag, "div");
    strcpy(flex_node.cls, "single-grow");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (!flex_style.flex_basis_zero || flex_style.flex_grow != 1) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS single-value flex shorthand basis mismatch\n");
        return 27;
    }
    memset(&flex_node, 0, sizeof(flex_node));
    strcpy(flex_node.tag, "div");
    strcpy(flex_node.cls, "reset-basis");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (flex_style.flex_grow != 1 || flex_style.flex_basis_pct != HTML_CSS_UNSET ||
        flex_style.flex_basis_zero) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS flex shorthand failed to reset its previous basis\n");
        return 25;
    }
    strcpy(flex_node.cls, "no-shrink-auto");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (flex_style.flex_basis_zero || flex_style.flex_basis_pct != HTML_CSS_UNSET) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS flex shrink factor was mistaken for auto basis\n");
        return 28;
    }
    strcpy(flex_node.cls, "no-shrink-default");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (!flex_style.flex_basis_zero) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS flex omitted basis should be zero\n");
        return 29;
    }
    memset(&flex_node, 0, sizeof(flex_node));
    strcpy(flex_node.tag, "div");
    strcpy(flex_node.cls, "reset");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (flex_style.flex_grow != 0) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS flex:none did not reset grow\n");
        return 17;
    }
    memset(&flex_node, 0, sizeof(flex_node));
    strcpy(flex_node.tag, "input");
    html_css_compute(sheet, &flex_node, 1, NULL, NULL, &flex_style);
    if (flex_style.width_pct != 100 || flex_style.max_width_px != 240) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS input width/max-width cascade mismatch\n");
        return 18;
    }
    html_css_destroy(sheet);

    static const char scoped_var_css[] =
        ".scope-a{--pane:160px;--spacing:var(--pane)}"
        ".scope-b{--pane:480px}"
        ".scope-override{--pane:80px}"
        ".scope-override{--pane:72px!important}"
        ".scoped-child{width:var(--pane);padding-left:var(--spacing, 3px)}"
        ".multi{--a:4px;--b:8px;padding:var(--a) var(--b) var(--missing, 12px);margin-inline:2px 6px;box-sizing:border-box}";
    sheet = html_css_create(822);
    if (!sheet) return 18;
    html_css_add_stylesheet(sheet, scoped_var_css, sizeof(scoped_var_css) - 1);
    css_node_info_t scope_a[2];
    memset(scope_a, 0, sizeof(scope_a));
    strcpy(scope_a[0].tag, "div");
    strcpy(scope_a[0].cls, "scope-a");
    strcpy(scope_a[1].tag, "div");
    strcpy(scope_a[1].cls, "scoped-child");
    css_style_t scope_a_style, child_a_style;
    html_css_compute(sheet, &scope_a[0], 1, NULL, NULL, &scope_a_style);
    html_css_compute(sheet, scope_a, 2, NULL, &scope_a_style, &child_a_style);
    if (child_a_style.width_px != 160 || child_a_style.pad[3] != 160) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS custom property inheritance/alias failed: width=%d pad-left=%d\n",
                child_a_style.width_px, child_a_style.pad[3]);
        return 19;
    }

    css_node_info_t scope_b[2];
    memset(scope_b, 0, sizeof(scope_b));
    strcpy(scope_b[0].tag, "div");
    strcpy(scope_b[0].cls, "scope-b");
    strcpy(scope_b[1].tag, "div");
    strcpy(scope_b[1].cls, "scoped-child");
    css_style_t scope_b_style, child_b_style;
    html_css_compute(sheet, &scope_b[0], 1, NULL, NULL, &scope_b_style);
    html_css_compute(sheet, scope_b, 2, NULL, &scope_b_style, &child_b_style);
    if (child_b_style.width_px != 480 || child_b_style.pad[3] != 3) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS custom property scope leaked between siblings: width=%d pad-left=%d\n",
                child_b_style.width_px, child_b_style.pad[3]);
        return 20;
    }

    css_node_info_t override_node = {0};
    strcpy(override_node.tag, "div");
    strcpy(override_node.cls, "scope-override scoped-child");
    css_style_t override_style;
    html_css_compute(sheet, &override_node, 1, NULL, NULL, &override_style);
    if (override_style.width_px != 72) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS !important custom property cascade failed: width=%d\n",
                override_style.width_px);
        return 21;
    }

    css_node_info_t inline_node = {0};
    strcpy(inline_node.tag, "div");
    strcpy(inline_node.cls, "scoped-child");
    css_style_t inline_style;
    html_css_compute(sheet, &inline_node, 1, "--pane:64px", NULL, &inline_style);
    if (inline_style.width_px != 64) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS inline custom property was not applied: width=%d\n",
                inline_style.width_px);
        return 22;
    }
    strcpy(inline_node.cls, "multi");
    html_css_compute(sheet, &inline_node, 1, NULL, NULL, &inline_style);
    if (inline_style.pad[0] != 4 || inline_style.pad[1] != 8 ||
        inline_style.pad[2] != 12 || inline_style.pad[3] != 8 ||
        inline_style.margin[3] != 2 || inline_style.margin[1] != 6 || !inline_style.border_box) {
        fprintf(stderr, "CSS multiple variables or logical edges failed: %d %d %d %d\n", inline_style.pad[0], inline_style.pad[1], inline_style.pad[2], inline_style.pad[3]);
        html_css_destroy(sheet);
        return 30;
    }
    html_css_destroy(sheet);

    static const char grid_css[] =
        ".repo-grid{display:grid;grid-template-columns:minmax(0, 1fr) 296px;gap:16px}";
    sheet = html_css_create(800);
    if (!sheet) return 19;
    html_css_add_stylesheet(sheet, grid_css, sizeof(grid_css) - 1);
    css_node_info_t grid_node = {0};
    strcpy(grid_node.tag, "div");
    strcpy(grid_node.cls, "repo-grid");
    css_style_t grid_style;
    html_css_compute(sheet, &grid_node, 1, NULL, NULL, &grid_style);
    html_css_destroy(sheet);
    if (grid_style.display != CSS_DISP_GRID || grid_style.grid_col_count != 2 ||
        grid_style.grid_col_kind[0] != 3 || grid_style.grid_col_kind[1] != 1 ||
        grid_style.grid_col_value[0] != 1 || grid_style.grid_col_value[1] != 296 ||
        grid_style.gap != 16) {
        fprintf(stderr, "CSS grid track parse mismatch: display=%d count=%u kinds=%u,%u values=%u,%u gap=%d\n",
                grid_style.display, grid_style.grid_col_count,
                grid_style.grid_col_kind[0], grid_style.grid_col_kind[1],
                grid_style.grid_col_value[0], grid_style.grid_col_value[1],
                grid_style.gap);
        return 20;
    }

    static const char gap_axes_css[] =
        ".gap-axes{display:flex;gap:8px 16px;row-gap:12px;column-gap:20px}"
        ".gap-order{display:grid;row-gap:2px;column-gap:3px;gap:8px 16px}";
    sheet = html_css_create(800);
    if (!sheet) return 38;
    html_css_add_stylesheet(sheet, gap_axes_css, sizeof(gap_axes_css) - 1);
    css_node_info_t gap_node = {0};
    strcpy(gap_node.tag, "div");
    strcpy(gap_node.cls, "gap-axes");
    css_style_t gap_style;
    html_css_compute(sheet, &gap_node, 1, NULL, NULL, &gap_style);
    if (gap_style.gap != 8 || gap_style.row_gap != 12 ||
        gap_style.column_gap != 20) {
        html_css_destroy(sheet);
        fprintf(stderr, "CSS row/column gap or longhand cascade mismatch\n");
        return 39;
    }
    strcpy(gap_node.cls, "gap-order");
    html_css_compute(sheet, &gap_node, 1, NULL, NULL, &gap_style);
    html_css_destroy(sheet);
    if (gap_style.gap != 8 || gap_style.row_gap != 8 ||
        gap_style.column_gap != 16) {
        fprintf(stderr, "CSS two-value gap shorthand cascade order mismatch\n");
        return 40;
    }

    static const char oversized_length_css[] =
        ".oversized{width:999999999999999999999999999999999999999999999999px}";
    sheet = html_css_create(800);
    if (!sheet) return 25;
    html_css_add_stylesheet(sheet, oversized_length_css,
                            sizeof(oversized_length_css) - 1);
    css_node_info_t oversized_node = {0};
    strcpy(oversized_node.tag, "div");
    strcpy(oversized_node.cls, "oversized");
    css_style_t oversized_style;
    html_css_compute(sheet, &oversized_node, 1, NULL, NULL, &oversized_style);
    html_css_destroy(sheet);
    if (oversized_style.width_px != INT16_MAX) {
        fprintf(stderr, "CSS oversized length did not saturate safely: %d\n",
                oversized_style.width_px);
        return 26;
    }
    lv_color_t oversized_alpha_color;
    bool oversized_alpha_transparent = false;
    if (!html_css_parse_color(NULL,
            "rgba(1,2,3,999999999999999999999999999999999999999999999999%)",
            &oversized_alpha_color, &oversized_alpha_transparent) ||
        oversized_alpha_transparent) {
        fprintf(stderr, "CSS oversized alpha value was not safely clamped\n");
        return 27;
    }

    static const char viewport_height_css[] = ".viewport-height{height:100vh}";
    sheet = html_css_create(800);
    if (!sheet) return 28;
    html_css_set_viewport_height(sheet, 768);
    html_css_add_stylesheet(sheet, viewport_height_css,
                            sizeof(viewport_height_css) - 1);
    css_node_info_t viewport_node = {0};
    strcpy(viewport_node.tag, "div");
    strcpy(viewport_node.cls, "viewport-height");
    css_style_t viewport_style;
    html_css_compute(sheet, &viewport_node, 1, NULL, NULL, &viewport_style);
    html_css_destroy(sheet);
    if (viewport_style.height_px != 768) {
        fprintf(stderr, "CSS vh unit ignored actual viewport height: %d\n",
                viewport_style.height_px);
        return 29;
    }

    static const char height_media_css[] =
        ".height-media{height:20px}"
        "@media (min-height:700px) and (max-height:800px){"
        ".height-media{height:42px}}";
    sheet = html_css_create(800);
    if (!sheet) return 30;
    html_css_set_viewport_height(sheet, 768);
    html_css_add_stylesheet(sheet, height_media_css, sizeof(height_media_css) - 1);
    css_node_info_t media_node = {0};
    strcpy(media_node.tag, "div");
    strcpy(media_node.cls, "height-media");
    css_style_t media_style;
    html_css_compute(sheet, &media_node, 1, NULL, NULL, &media_style);
    html_css_destroy(sheet);
    if (media_style.height_px != 42) {
        fprintf(stderr, "CSS min/max-height media query did not match: %d\n",
                media_style.height_px);
        return 31;
    }
    sheet = html_css_create(800);
    if (!sheet) return 32;
    html_css_set_viewport_height(sheet, 600);
    html_css_add_stylesheet(sheet, height_media_css, sizeof(height_media_css) - 1);
    html_css_compute(sheet, &media_node, 1, NULL, NULL, &media_style);
    html_css_destroy(sheet);
    if (media_style.height_px != 20) {
        fprintf(stderr, "CSS max-height media query ignored the viewport: %d\n",
                media_style.height_px);
        return 33;
    }

    static const char min_height_css[] =
        ".min-height-box{min-height:100vh;height:40px}";
    sheet = html_css_create(800);
    if (!sheet) return 34;
    html_css_set_viewport_height(sheet, 768);
    html_css_add_stylesheet(sheet, min_height_css, sizeof(min_height_css) - 1);
    css_node_info_t min_height_node = {0};
    strcpy(min_height_node.tag, "div");
    strcpy(min_height_node.cls, "min-height-box");
    css_style_t min_height_style;
    html_css_compute(sheet, &min_height_node, 1, NULL, NULL, &min_height_style);
    html_css_destroy(sheet);
    if (min_height_style.min_height_px != 768 || min_height_style.height_px != 40) {
        fprintf(stderr, "CSS min-height/height cascade failed: min=%d height=%d\n",
                min_height_style.min_height_px, min_height_style.height_px);
        return 34;
    }

    static const char calc_min_height_css[] =
        ".calc-min-height{min-height:calc(100vh - 64px)}";
    sheet = html_css_create(800);
    if (!sheet) return 35;
    html_css_set_viewport_height(sheet, 768);
    html_css_add_stylesheet(sheet, calc_min_height_css,
                            sizeof(calc_min_height_css) - 1);
    css_node_info_t calc_node = {0};
    strcpy(calc_node.tag, "div");
    strcpy(calc_node.cls, "calc-min-height");
    css_style_t calc_style;
    html_css_compute(sheet, &calc_node, 1, NULL, NULL, &calc_style);
    html_css_destroy(sheet);
    if (calc_style.min_height_px != 704) {
        fprintf(stderr, "CSS viewport calc() expected 704px, got %d\n",
                calc_style.min_height_px);
        return 35;
    }

    static const char range_media_css[] =
        ".range-media{height:20px}"
        "@media screen and (width>=34rem) and (width<=47.99rem){"
        ".range-media{height:42px}}";
    sheet = html_css_create(750);
    if (!sheet) return 34;
    html_css_add_stylesheet(sheet, range_media_css, sizeof(range_media_css) - 1);
    css_node_info_t range_node = {0};
    strcpy(range_node.tag, "div");
    strcpy(range_node.cls, "range-media");
    css_style_t range_style;
    html_css_compute(sheet, &range_node, 1, NULL, NULL, &range_style);
    html_css_destroy(sheet);
    if (range_style.height_px != 42) {
        fprintf(stderr, "CSS range media query with rem bounds failed: %d\n",
                range_style.height_px);
        return 35;
    }
    sheet = html_css_create(800);
    if (!sheet) return 36;
    html_css_add_stylesheet(sheet, range_media_css, sizeof(range_media_css) - 1);
    html_css_compute(sheet, &range_node, 1, NULL, NULL, &range_style);
    html_css_destroy(sheet);
    if (range_style.height_px != 20) {
        fprintf(stderr, "CSS range media query ignored its upper bound: %d\n",
                range_style.height_px);
        return 37;
    }

    static const char height_range_css[] =
        ".height-range{height:20px}"
        "@media (height>=40rem) and (height<50rem){"
        ".height-range{height:42px}}";
    sheet = html_css_create(800);
    if (!sheet) return 41;
    html_css_set_viewport_height(sheet, 768);
    html_css_add_stylesheet(sheet, height_range_css, sizeof(height_range_css) - 1);
    css_node_info_t height_range_node = {0};
    strcpy(height_range_node.tag, "div");
    strcpy(height_range_node.cls, "height-range");
    html_css_compute(sheet, &height_range_node, 1, NULL, NULL, &range_style);
    html_css_destroy(sheet);
    if (range_style.height_px != 42) {
        fprintf(stderr, "CSS range-style height query failed: %d\n",
                range_style.height_px);
        return 42;
    }

    static const char grid_percent_css[] =
        ".percent-grid{display:grid;grid-template-columns:40% 60%}";
    sheet = html_css_create(800);
    if (!sheet) return 23;
    html_css_add_stylesheet(sheet, grid_percent_css, sizeof(grid_percent_css) - 1);
    memset(&grid_node, 0, sizeof(grid_node));
    strcpy(grid_node.tag, "div");
    strcpy(grid_node.cls, "percent-grid");
    html_css_compute(sheet, &grid_node, 1, NULL, NULL, &grid_style);
    html_css_destroy(sheet);
    if (grid_style.grid_col_count != 2 || grid_style.grid_col_kind[0] != 2 ||
        grid_style.grid_col_kind[1] != 2 || grid_style.grid_col_value[0] != 40 ||
        grid_style.grid_col_value[1] != 60) {
        fprintf(stderr, "CSS percentage grid track parsing failed\n");
        return 24;
    }

    static const char grid_reset_css[] =
        ".reset-grid{display:grid;grid-template-columns:1fr 296px;grid-template-columns:none}";
    sheet = html_css_create(800);
    if (!sheet) return 21;
    html_css_add_stylesheet(sheet, grid_reset_css, sizeof(grid_reset_css) - 1);
    memset(&grid_node, 0, sizeof(grid_node));
    strcpy(grid_node.tag, "div");
    strcpy(grid_node.cls, "reset-grid");
    html_css_compute(sheet, &grid_node, 1, NULL, NULL, &grid_style);
    html_css_destroy(sheet);
    if (grid_style.grid_col_count != 0) {
        fprintf(stderr, "CSS grid-template-columns:none failed to reset tracks: count=%u kind0=%u value0=%d\n", grid_style.grid_col_count, grid_style.grid_col_kind[0], grid_style.grid_col_value[0]);
        return 22;
    }

    printf("HTML CSS PASS: selector index visited %zu/%d candidates; attribute operators and full cascade order verified\n",
           candidate_count, large_rule_count);
    return 0;
}
