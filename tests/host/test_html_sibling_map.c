#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kmalloc(size_t size) { return malloc(size); }
void *krealloc(void *ptr, size_t size) { return realloc(ptr, size); }
void kfree(void *ptr) { free(ptr); }

#define HTML_VIEW_HOST_TEST 1
#include "../../src/kernel/gui/html_view.c"

int main(void) {
    if (html_type_count_capacity(4096U) != 8192U ||
        html_type_count_capacity(HTML_MAX_SIBLING_MAP) != HTML_TYPE_COUNT_CAP) {
        fprintf(stderr, "HTML sibling map: type-count table was not sized to input\n");
        return 16;
    }

    static const char html[] =
        "<div><p id='first'>one</p><span>middle</span>"
        "<p id='second'>two</p><p id='third'>three</p></div>";
    const char *first = strstr(html, "<p id='first'");
    const char *span = strstr(html, "<span>");
    const char *second = strstr(html, "<p id='second'");
    const char *third = strstr(html, "<p id='third'");
    if (!first || !span || !second || !third) return 1;

    uint16_t sibling_count = 0, type_index = 0, type_count = 0;
    bool is_empty = false;
    if (!html_view_test_sibling_metadata(html, sizeof(html) - 1, true,
                                         (size_t)(second - html),
                                         &sibling_count, &type_index, &type_count,
                                         &is_empty) ||
        sibling_count != 4 || type_index != 2 || type_count != 3 || is_empty) {
        fprintf(stderr, "HTML sibling map: second p metadata mismatch (%u,%u,%u)\n",
                sibling_count, type_index, type_count);
        return 2;
    }

    if (!html_view_test_sibling_metadata(html, sizeof(html) - 1, true,
                                         (size_t)(span - html),
                                         &sibling_count, &type_index, &type_count,
                                         &is_empty) ||
        sibling_count != 4 || type_index != 1 || type_count != 1 || is_empty) {
        fprintf(stderr, "HTML sibling map: unique span metadata mismatch (%u,%u,%u)\n",
                sibling_count, type_index, type_count);
        return 3;
    }

    if (!html_view_test_sibling_metadata(html, sizeof(html) - 1, true,
                                         (size_t)(third - html),
                                         &sibling_count, &type_index, &type_count,
                                         &is_empty) ||
        type_index != 3 || type_count != 3 || is_empty) {
        fprintf(stderr, "HTML sibling map: last p metadata mismatch (%u,%u,%u)\n",
                sibling_count, type_index, type_count);
        return 4;
    }

    static const char nested[] =
        "<section><p>only</p></section>"
        "<section><p>one</p><p>two</p></section>";
    const char *nested_first = strstr(nested, "<p>only");
    const char *nested_second = strstr(nested, "<p>one");
    const char *nested_third = strstr(nested, "<p>two");
    if (!nested_first || !nested_second || !nested_third) return 5;
    if (!html_view_test_sibling_metadata(nested, sizeof(nested) - 1, true,
                                         (size_t)(nested_first - nested),
                                         &sibling_count, &type_index, &type_count,
                                         &is_empty) ||
        type_index != 1 || type_count != 1 || sibling_count != 1 || is_empty) {
        fprintf(stderr, "HTML sibling map: nested parent isolation failed\n");
        return 6;
    }
    if (!html_view_test_sibling_metadata(nested, sizeof(nested) - 1, true,
                                         (size_t)(nested_third - nested),
                                         &sibling_count, &type_index, &type_count,
                                         &is_empty) ||
        type_index != 2 || type_count != 2 || sibling_count != 2 || is_empty) {
        fprintf(stderr, "HTML sibling map: nested same-tag counts failed\n");
        return 7;
    }

    if (!html_view_test_sibling_metadata(html, sizeof(html) - 1, false,
                                         (size_t)(second - html),
                                         &sibling_count, &type_index, &type_count,
                                         &is_empty) ||
        sibling_count != 4 || type_index != 0 || type_count != 0) {
        fprintf(stderr, "HTML sibling map: non-type fast path mismatch\n");
        return 8;
    }

    static const char empties[] =
        "<section><em></em><i> </i><b><!-- comment --></b></section>";
    const char *empty_em = strstr(empties, "<em>");
    const char *space_i = strstr(empties, "<i>");
    const char *comment_b = strstr(empties, "<b>");
    if (!empty_em || !space_i || !comment_b) return 9;
    if (!html_view_test_sibling_metadata(empties, sizeof(empties) - 1, false,
                                         (size_t)(empty_em - empties),
                                         NULL, NULL, NULL, &is_empty) || !is_empty) {
        fprintf(stderr, "HTML sibling map: empty element was not detected\n");
        return 10;
    }
    if (!html_view_test_sibling_metadata(empties, sizeof(empties) - 1, false,
                                         (size_t)(space_i - empties),
                                         NULL, NULL, NULL, &is_empty) || is_empty) {
        fprintf(stderr, "HTML sibling map: whitespace text incorrectly treated as empty\n");
        return 11;
    }
    if (!html_view_test_sibling_metadata(empties, sizeof(empties) - 1, false,
                                         (size_t)(comment_b - empties),
                                         NULL, NULL, NULL, &is_empty) || !is_empty) {
        fprintf(stderr, "HTML sibling map: comments incorrectly treated as content\n");
        return 12;
    }

    char many[8192];
    size_t used = 0;
    memcpy(many + used, "<div>", 5);
    used += 5;
    size_t last_p_offset = 0;
    for (unsigned i = 0; i < 300; ++i) {
        last_p_offset = used;
        memcpy(many + used, "<p></p>", 7);
        used += 7;
    }
    memcpy(many + used, "</div>", 6);
    used += 6;
    if (!html_view_test_sibling_metadata(many, used, true, last_p_offset,
                                         &sibling_count, &type_index, &type_count,
                                         &is_empty) ||
        sibling_count != 300 || type_index != 300 || type_count != 300 || !is_empty) {
        fprintf(stderr, "HTML sibling map: reallocated metadata mismatch (%u,%u,%u)\n",
                sibling_count, type_index, type_count);
        return 13;
    }

    html_stylesheet_t *sheet = html_css_create(800);
    if (!sheet) return 14;
    static const char referenced_css[] =
        ".ranked[data-late=target]{font-weight:bold}";
    html_css_add_stylesheet(sheet, referenced_css, sizeof(referenced_css) - 1);
    css_node_info_t ranked_node = {0};
    strcpy(ranked_node.tag, "div");
    strcpy(ranked_node.cls, "ranked");
    static const char many_attrs[] =
        "a0='0' a1='1' a2='2' a3='3' a4='4' a5='5' "
        "a6='6' a7='7' a8='8' a9='9' a10='10' a11='11' "
        "a12='12' data-late='target'";
    capture_css_attributes(sheet, &ranked_node, many_attrs,
                           sizeof(many_attrs) - 1);
    css_style_t ranked_style;
    html_css_compute(sheet, &ranked_node, 1, NULL, NULL, &ranked_style);
    html_css_destroy(sheet);
    if (!ranked_style.bold || ranked_node.attr_count != HTML_CSS_MAX_NODE_ATTRS) {
        fprintf(stderr, "HTML sibling map: CSS-referenced late attribute was dropped\n");
        return 15;
    }

    puts("HTML sibling map PASS: sibling metadata and prioritized CSS attributes");
    return 0;
}
