#include "module_url.h"

#include <stdbool.h>
#include <string.h>

static int is_http_url(const char *url) {
    return strncmp(url, "https://", 8) == 0 ||
           strncmp(url, "http://", 7) == 0;
}

int qjs_resolve_module_url(const char *base_url, const char *specifier,
                           char *output, size_t output_capacity) {
    if (!base_url || !specifier || !output || output_capacity < 10 ||
        !is_http_url(base_url) || !specifier[0]) return -1;

    size_t base_length = strlen(base_url);
    size_t specifier_length = strlen(specifier);
    const char *scheme_end = strstr(base_url, "://");
    if (!scheme_end) return -1;
    const char *authority = scheme_end + 3;
    const char *authority_end = authority;
    while (*authority_end && *authority_end != '/' && *authority_end != '?' &&
           *authority_end != '#') authority_end++;
    size_t origin_length = (size_t)(authority_end - base_url);
    if (origin_length == (size_t)(scheme_end + 3 - base_url)) return -1;

    char combined[2048];
    size_t combined_length = 0;
    if (strstr(specifier, "://")) {
        if (!is_http_url(specifier) || specifier_length >= output_capacity)
            return -1;
        memcpy(output, specifier, specifier_length + 1);
        return 0;
    }
    if (specifier[0] == '/' && specifier[1] == '/') {
        size_t scheme_length = (size_t)(scheme_end - base_url);
        if (scheme_length + 1 + specifier_length >= output_capacity) return -1;
        memcpy(output, base_url, scheme_length);
        output[scheme_length] = ':';
        memcpy(output + scheme_length + 1, specifier,
               specifier_length + 1);
        return 0;
    }
    if (specifier[0] != '/' && specifier[0] != '?' &&
        specifier[0] != '#' && strncmp(specifier, "./", 2) != 0 &&
        strncmp(specifier, "../", 3) != 0) return -1;

    const char *base_path_end = base_url + base_length;
    for (const char *p = authority_end; p < base_path_end; ++p) {
        if (*p == '?' || *p == '#') {
            base_path_end = p;
            break;
        }
    }
    if (specifier[0] == '/') {
        if (origin_length + specifier_length >= sizeof(combined)) return -1;
        memcpy(combined, base_url, origin_length);
        memcpy(combined + origin_length, specifier, specifier_length);
        combined_length = origin_length + specifier_length;
    } else if (specifier[0] == '?') {
        size_t path_length = (size_t)(base_path_end - base_url);
        if (path_length == origin_length) {
            if (origin_length + 1 + specifier_length >= sizeof(combined))
                return -1;
            memcpy(combined, base_url, origin_length);
            combined[origin_length] = '/';
            memcpy(combined + origin_length + 1, specifier,
                   specifier_length);
            combined_length = origin_length + 1 + specifier_length;
        } else {
            if (path_length + specifier_length >= sizeof(combined)) return -1;
            memcpy(combined, base_url, path_length);
            memcpy(combined + path_length, specifier, specifier_length);
            combined_length = path_length + specifier_length;
        }
    } else if (specifier[0] == '#') {
        if (base_path_end == base_url + origin_length) return -1;
        size_t path_length = (size_t)(base_path_end - base_url);
        if (path_length + specifier_length >= sizeof(combined)) return -1;
        memcpy(combined, base_url, path_length);
        memcpy(combined + path_length, specifier, specifier_length);
        combined_length = path_length + specifier_length;
    } else {
        const char *last_slash = NULL;
        for (const char *p = authority_end; p < base_path_end; ++p)
            if (*p == '/') last_slash = p;
        size_t directory_length = last_slash
            ? (size_t)(last_slash + 1 - base_url) : origin_length + 1;
        if (directory_length + specifier_length >= sizeof(combined)) return -1;
        size_t cursor;
        if (last_slash) {
            memcpy(combined, base_url, directory_length);
            cursor = directory_length;
        } else {
            memcpy(combined, base_url, origin_length);
            cursor = origin_length;
            combined[cursor++] = '/';
        }
        memcpy(combined + cursor, specifier, specifier_length);
        combined_length = cursor + specifier_length;
    }
    if (combined_length >= sizeof(combined)) return -1;
    combined[combined_length] = '\0';

    size_t path_start = origin_length;
    size_t path_end = combined_length;
    for (size_t i = path_start; i < combined_length; ++i) {
        if (combined[i] == '?' || combined[i] == '#') {
            path_end = i;
            break;
        }
    }
    char path[2048];
    size_t path_length = 1;
    size_t segment_starts[1024];
    size_t segment_count = 0;
    path[0] = '/';
    size_t cursor = path_start;
    if (cursor < path_end && combined[cursor] == '/') cursor++;
    while (cursor < path_end) {
        while (cursor < path_end && combined[cursor] == '/') cursor++;
        size_t start = cursor;
        while (cursor < path_end && combined[cursor] != '/') cursor++;
        size_t length = cursor - start;
        if (!length || (length == 1 && combined[start] == '.')) continue;
        if (length == 2 && combined[start] == '.' && combined[start + 1] == '.') {
            if (segment_count) path_length = segment_starts[--segment_count];
            continue;
        }
        if (segment_count >= sizeof(segment_starts) / sizeof(segment_starts[0]) ||
            length > sizeof(path) - path_length - 1) return -1;
        size_t previous_length = path_length;
        if (path_length > 1) path[path_length++] = '/';
        segment_starts[segment_count++] = previous_length;
        memcpy(path + path_length, combined + start, length);
        path_length += length;
    }
    bool trailing_slash = path_end > path_start + 1 &&
        (combined[path_end - 1] == '/' ||
         (path_end >= 2 && combined[path_end - 2] == '.' &&
          combined[path_end - 1] == '.'));
    if (trailing_slash && path_length > 1) path[path_length++] = '/';

    size_t suffix_length = combined_length - path_end;
    if (origin_length + path_length + suffix_length >= output_capacity)
        return -1;
    memcpy(output, combined, origin_length);
    memcpy(output + origin_length, path, path_length);
    memcpy(output + origin_length + path_length, combined + path_end,
           suffix_length);
    output[origin_length + path_length + suffix_length] = '\0';
    return 0;
}

int qjs_resolve_module_url_with_import_map(
    const char *base_url, const char *specifier,
    const qjs_import_map_entry_t *entries, size_t entry_count,
    char *output, size_t output_capacity) {
    if (!specifier || !specifier[0] || (!entries && entry_count)) return -1;

    const char *address = NULL;
    size_t specifier_length = strlen(specifier);
    for (size_t i = 0; i < entry_count; ++i) {
        if (entries[i].specifier && entries[i].address &&
            strcmp(entries[i].specifier, specifier) == 0) {
            address = entries[i].address;
            break;
        }
    }
    if (!address) {
        size_t longest_prefix = 0;
        for (size_t i = 0; i < entry_count; ++i) {
            const char *key = entries[i].specifier;
            const char *target = entries[i].address;
            if (!key || !target) continue;
            size_t key_length = strlen(key);
            size_t target_length = strlen(target);
            if (!key_length || !target_length || key[key_length - 1] != '/' ||
                target[target_length - 1] != '/' ||
                key_length >= specifier_length || key_length <= longest_prefix ||
                strncmp(key, specifier, key_length) != 0) continue;
            address = target;
            longest_prefix = key_length;
        }
        if (address) {
            size_t target_length = strlen(address);
            size_t suffix_length = specifier_length - longest_prefix;
            if (target_length + suffix_length >= output_capacity) return -1;
            char expanded[2048];
            if (target_length + suffix_length >= sizeof(expanded)) return -1;
            memcpy(expanded, address, target_length);
            memcpy(expanded + target_length,
                   specifier + longest_prefix, suffix_length);
            expanded[target_length + suffix_length] = '\0';
            return qjs_resolve_module_url(base_url, expanded, output,
                                          output_capacity);
        }
    }
    return qjs_resolve_module_url(base_url, address ? address : specifier,
                                  output, output_capacity);
}
