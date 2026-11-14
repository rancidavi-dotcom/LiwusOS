#ifndef LIWUS_QJS_MODULE_URL_H
#define LIWUS_QJS_MODULE_URL_H

#include <stddef.h>

typedef struct {
    const char *specifier;
    const char *address;
} qjs_import_map_entry_t;

/* Resolves an ES module specifier against an absolute HTTP(S) module URL.
 * Returns 0 on success and -1 for unsupported or overlong URLs. */
int qjs_resolve_module_url(const char *base_url, const char *specifier,
                           char *output, size_t output_capacity);
int qjs_resolve_module_url_with_import_map(
    const char *base_url, const char *specifier,
    const qjs_import_map_entry_t *entries, size_t entry_count,
    char *output, size_t output_capacity);

#endif
