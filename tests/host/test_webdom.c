#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "liwus/webdom.h"

static int has_text(const liw_web_document_t *document, const char *expected) {
    size_t expected_length = strlen(expected);
    size_t node_count = liw_webdom_node_count(document);
    for (liw_web_node_id_t id = 0; id < node_count; ++id) {
        if (liw_webdom_node_type(document, id) != LIW_WEBDOM_TEXT) continue;
        liw_web_slice_t value = liw_webdom_value(document, id);
        if (value.length == expected_length &&
            memcmp(value.data, expected, expected_length) == 0)
            return 1;
    }
    return 0;
}

int main(void) {
    char html[] = "<html><body>visible text<script>console.log(\"inline\");"
                  "</script></body></html>";
    liw_web_document_t *document = NULL;
    if (liw_webdom_parse(html, sizeof(html) - 1, &document) != LIW_WEBDOM_OK) {
        fputs("WEBDOM host test FAIL: parse failed\n", stderr);
        return 1;
    }
    memset(html, 'x', sizeof(html) - 1);
    int pass = has_text(document, "visible text") &&
               has_text(document, "console.log(\"inline\");");
    liw_webdom_destroy(document);
    if (!pass) {
        fputs("WEBDOM host test FAIL: source text was not preserved\n", stderr);
        return 1;
    }

    /* A large raw-text node forces the DOM string arena to realloc while the
     * parser is still reading its input. This is the shape of fetched inline
     * JavaScript on real pages; the parser input must not alias that arena. */
    const size_t large_script_length = 431609;
    const char script_open[] = "<html><body><script>";
    const char script_close[] = "</script></body></html>";
    size_t large_html_length = sizeof(script_open) - 1 +
                               large_script_length +
                               sizeof(script_close) - 1;
    char *large_html = (char *)malloc(large_html_length);
    if (!large_html) return 1;
    size_t large_offset = 0;
    memcpy(large_html + large_offset, script_open, sizeof(script_open) - 1);
    large_offset += sizeof(script_open) - 1;
    memset(large_html + large_offset, 'J', large_script_length);
    large_offset += large_script_length;
    memcpy(large_html + large_offset, script_close, sizeof(script_close) - 1);
    liw_web_document_t *large_document = NULL;
    int large_rc = liw_webdom_parse(large_html, large_html_length,
                                    &large_document);
    free(large_html);
    if (large_rc != LIW_WEBDOM_OK || !large_document) {
        fputs("WEBDOM host test FAIL: large raw-text parse failed\n", stderr);
        return 1;
    }
    int large_script_ok = 0;
    for (liw_web_node_id_t id = 0;
         id < liw_webdom_node_count(large_document); ++id) {
        if (liw_webdom_node_type(large_document, id) != LIW_WEBDOM_TEXT)
            continue;
        liw_web_slice_t value = liw_webdom_value(large_document, id);
        if (value.length == large_script_length &&
            value.data[0] == 'J' &&
            value.data[large_script_length / 2] == 'J' &&
            value.data[large_script_length - 1] == 'J') {
            large_script_ok = 1;
            break;
        }
    }
    liw_webdom_destroy(large_document);
    if (!large_script_ok) {
        fputs("WEBDOM host test FAIL: large raw text corrupted on arena growth\n",
              stderr);
        return 1;
    }

    /* Expanded browser pages store both the complete source and parsed raw
     * script text. Exercise an input whose two copies exceed the old 4 MiB
     * arena while each individual source remains below the 4 MiB source cap. */
    const size_t expanded_script_length = 2U * 1024U * 1024U + 512U * 1024U;
    size_t expanded_html_length = sizeof(script_open) - 1 +
        expanded_script_length + sizeof(script_close) - 1;
    char *expanded_html = (char *)malloc(expanded_html_length);
    if (!expanded_html) return 1;
    size_t expanded_offset = 0;
    memcpy(expanded_html + expanded_offset, script_open, sizeof(script_open) - 1);
    expanded_offset += sizeof(script_open) - 1;
    memset(expanded_html + expanded_offset, 'E', expanded_script_length);
    expanded_offset += expanded_script_length;
    memcpy(expanded_html + expanded_offset, script_close, sizeof(script_close) - 1);
    liw_web_document_t *expanded_document = NULL;
    int expanded_rc = liw_webdom_parse(expanded_html, expanded_html_length,
                                       &expanded_document);
    free(expanded_html);
    if (expanded_rc != LIW_WEBDOM_OK || !expanded_document) {
        if (expanded_document) liw_webdom_destroy(expanded_document);
        fputs("WEBDOM host test FAIL: bounded expanded page arena exhausted\n",
              stderr);
        return 1;
    }
    liw_webdom_destroy(expanded_document);

    /* Public repository pages can exceed the former 16,384-node cap even
     * when their source is well below the 4 MiB document limit. */
    const size_t dense_element_count = 20000;
    const char dense_open[] = "<body>";
    const char dense_element[] = "<i></i>";
    const char dense_close[] = "</body>";
    size_t dense_html_length = sizeof(dense_open) - 1 +
        dense_element_count * (sizeof(dense_element) - 1) +
        sizeof(dense_close) - 1;
    char *dense_html = (char *)malloc(dense_html_length);
    if (!dense_html) return 1;
    size_t dense_offset = 0;
    memcpy(dense_html + dense_offset, dense_open, sizeof(dense_open) - 1);
    dense_offset += sizeof(dense_open) - 1;
    for (size_t i = 0; i < dense_element_count; ++i) {
        memcpy(dense_html + dense_offset, dense_element,
               sizeof(dense_element) - 1);
        dense_offset += sizeof(dense_element) - 1;
    }
    memcpy(dense_html + dense_offset, dense_close, sizeof(dense_close) - 1);
    liw_web_document_t *dense_document = NULL;
    int dense_rc = liw_webdom_parse(dense_html, dense_html_length,
                                    &dense_document);
    free(dense_html);
    if (dense_rc != LIW_WEBDOM_OK || !dense_document ||
        liw_webdom_node_count(dense_document) <= 16384U) {
        if (dense_document) liw_webdom_destroy(dense_document);
        fputs("WEBDOM host test FAIL: bounded high-node-count page failed\n",
              stderr);
        return 1;
    }
    liw_webdom_destroy(dense_document);

    const size_t nested_element_count = 200;
    const char nested_open[] = "<x>";
    const char nested_close[] = "</x>";
    size_t nested_html_length = nested_element_count *
        ((sizeof(nested_open) - 1) + (sizeof(nested_close) - 1));
    char *nested_html = (char *)malloc(nested_html_length);
    if (!nested_html) return 1;
    size_t nested_offset = 0;
    for (size_t i = 0; i < nested_element_count; ++i) {
        memcpy(nested_html + nested_offset, nested_open, sizeof(nested_open) - 1);
        nested_offset += sizeof(nested_open) - 1;
    }
    for (size_t i = 0; i < nested_element_count; ++i) {
        memcpy(nested_html + nested_offset, nested_close,
               sizeof(nested_close) - 1);
        nested_offset += sizeof(nested_close) - 1;
    }
    liw_web_document_t *nested_document = NULL;
    int nested_rc = liw_webdom_parse(nested_html, nested_html_length,
                                     &nested_document);
    free(nested_html);
    if (nested_rc != LIW_WEBDOM_OK || !nested_document) {
        if (nested_document) liw_webdom_destroy(nested_document);
        fputs("WEBDOM host test FAIL: bounded deep page failed\n", stderr);
        return 1;
    }
    liw_webdom_destroy(nested_document);

    static const char mutation_page[] =
        "<html><body><p id=\"status\" title=\"A &amp; B\">"
        "old &amp; text</p></body></html>";
    liw_web_document_t *mutated = NULL;
    if (liw_webdom_parse(mutation_page, sizeof(mutation_page) - 1,
                         &mutated) != LIW_WEBDOM_OK) return 1;
    size_t original_length = 0;
    if (liw_webdom_serialize(mutated, NULL, 0,
                             &original_length) != LIW_WEBDOM_OK) return 1;
    char *original = (char *)malloc(original_length + 1);
    if (!original || liw_webdom_serialize(mutated, original,
            original_length + 1, &original_length) != LIW_WEBDOM_OK ||
        !strstr(original, "title=\"A &amp; B\"") ||
        !strstr(original, "old &amp; text")) return 1;
    free(original);
    liw_web_node_id_t status = liw_webdom_get_element_by_id(mutated, "status");
    liw_web_node_id_t source_text = liw_webdom_first_child(mutated, status);
    if (source_text == LIW_WEBDOM_NODE_NONE ||
        !liw_webdom_text_needs_entity_decode(mutated, source_text) ||
        !liw_webdom_attribute_needs_entity_decode(mutated, status, "title"))
        return 1;
    const char unsafe_text[] = "new <state> & \"ready\"";
    if (status == LIW_WEBDOM_NODE_NONE ||
        liw_webdom_set_text(mutated, status, unsafe_text,
                            sizeof(unsafe_text) - 1) != LIW_WEBDOM_OK ||
        liw_webdom_set_attribute(mutated, status, "title", 5,
                                 "A & B", 5) != LIW_WEBDOM_OK) return 1;
    liw_web_node_id_t mutated_text = liw_webdom_first_child(mutated, status);
    if (mutated_text == LIW_WEBDOM_NODE_NONE ||
        liw_webdom_text_needs_entity_decode(mutated, mutated_text) ||
        liw_webdom_attribute_needs_entity_decode(mutated, status, "title"))
        return 1;
    size_t serialized_length = 0;
    if (liw_webdom_serialize(mutated, NULL, 0,
                             &serialized_length) != LIW_WEBDOM_OK ||
        serialized_length == 0) return 1;
    char *serialized = (char *)malloc(serialized_length + 1);
    if (!serialized ||
        liw_webdom_serialize(mutated, serialized, serialized_length + 1,
                             &serialized_length) != LIW_WEBDOM_OK ||
        !strstr(serialized, "new &lt;state&gt; &amp; \"ready\"") ||
        !strstr(serialized, "title=\"A &amp; B\"")) return 1;
    liw_web_document_t *roundtrip = NULL;
    if (liw_webdom_parse(serialized, serialized_length,
                         &roundtrip) != LIW_WEBDOM_OK ||
        !has_text(roundtrip, "new &lt;state&gt; &amp; \"ready\"")) return 1;
    liw_webdom_destroy(roundtrip);
    liw_webdom_destroy(mutated);
    free(serialized);
    puts("WEBDOM host PASS: source ownership and escaped DOM round trip");
    return 0;
}
