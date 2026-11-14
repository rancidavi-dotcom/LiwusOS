#include "gui/html_view.h"
#include "kheap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kmalloc(size_t size) { return malloc(size); }
void *krealloc(void *ptr, size_t size) { return realloc(ptr, size); }
void kfree(void *ptr) { free(ptr); }

static bool fetch_script(const char *url, uint8_t **out_data, size_t *out_len,
                         bool *out_caller_free, void *user_data) {
    (void)user_data;
    const char *source = NULL;
    if (strcmp(url, "https://example.test/module.js") == 0)
        source = "export const answer = 42;";
    else if (strcmp(url, "https://example.test/classic.js") == 0)
        source = "globalThis.answer = 42;";
    else
        return false;
    *out_len = strlen(source);
    *out_data = malloc(*out_len);
    if (!*out_data) return false;
    memcpy(*out_data, source, *out_len);
    *out_caller_free = true;
    return true;
}

int main(void) {
    const char html[] =
        "<script type=\"module\" src=\"/module.js\"></script>"
        "<script src=\"/classic.js\"></script>";
    uint8_t *expanded = NULL;
    size_t expanded_len = 0;
    int rc = html_view_inline_external_scripts(
        html, sizeof(html) - 1, "https://example.test/", fetch_script, NULL,
        &expanded, &expanded_len);
    if (rc != 1 || !expanded ||
        !strstr((const char *)expanded,
            "<script type=\"module\" data-liwus-module-url=\"https://example.test/module.js\">export const answer = 42;</script>") ||
        !strstr((const char *)expanded, "<script>globalThis.answer = 42;</script>")) {
        fprintf(stderr, "external script expansion lost script type (rc=%d len=%zu)\n",
                rc, expanded_len);
        free(expanded);
        return 1;
    }
    free(expanded);
    puts("HTML external scripts PASS: module type preserved; classic remains classic");
    return 0;
}
