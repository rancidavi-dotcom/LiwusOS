#include "../../programs/qjs/module_url.h"

#include <stdio.h>
#include <string.h>

static int expect(const char *base, const char *specifier, const char *wanted) {
    char output[512];
    if (qjs_resolve_module_url(base, specifier, output, sizeof(output)) != 0) {
        fprintf(stderr, "resolve failed: base=%s specifier=%s\n", base,
                specifier);
        return 1;
    }
    if (strcmp(output, wanted) != 0) {
        fprintf(stderr, "resolve mismatch: %s + %s -> %s (wanted %s)\n",
                base, specifier, output, wanted);
        return 1;
    }
    return 0;
}

static int rejects(const char *base, const char *specifier) {
    char output[512];
    if (qjs_resolve_module_url(base, specifier, output, sizeof(output)) == 0) {
        fprintf(stderr, "unexpected module URL acceptance: %s\n", output);
        return 1;
    }
    return 0;
}

int main(void) {
    int failed = 0;
    const char *base = "https://example.test/a/b/main.js?cache=1#part";
    const qjs_import_map_entry_t imports[] = {
        {"react", "https://assets.example/react.js"},
        {"react-dom/", "https://assets.example/react-dom/"},
        {"react-dom/client", "https://assets.example/client.js"},
    };
    failed |= expect(base, "./value.js", "https://example.test/a/b/value.js");
    failed |= expect(base, "../shared/value.js",
                     "https://example.test/a/shared/value.js");
    failed |= expect(base, "/assets/value.js",
                     "https://example.test/assets/value.js");
    failed |= expect(base, "//cdn.example.test/pkg.js",
                     "https://cdn.example.test/pkg.js");
    failed |= expect(base, "?v=2", "https://example.test/a/b/main.js?v=2");
    failed |= expect(base, "./value.js#fragment",
                     "https://example.test/a/b/value.js#fragment");
    failed |= expect("http://example.test", "./entry.js",
                     "http://example.test/entry.js");
    failed |= expect("https://example.test/a/b/main.js", "../../../../x.js",
                     "https://example.test/x.js");
    char mapped[512] = {0};
    if (qjs_resolve_module_url_with_import_map(base, "react", imports, 3,
            mapped, sizeof(mapped)) != 0 ||
        strcmp(mapped, "https://assets.example/react.js") != 0) {
        fprintf(stderr, "exact import map mismatch: %s\n", mapped);
        failed = 1;
    }
    if (qjs_resolve_module_url_with_import_map(base, "react-dom/client",
            imports, 3, mapped, sizeof(mapped)) != 0 ||
        strcmp(mapped, "https://assets.example/client.js") != 0) {
        fprintf(stderr, "exact entry did not override prefix: %s\n", mapped);
        failed = 1;
    }
    if (qjs_resolve_module_url_with_import_map(base, "react-dom/server.js",
            imports, 3, mapped, sizeof(mapped)) != 0 ||
        strcmp(mapped, "https://assets.example/react-dom/server.js") != 0) {
        fprintf(stderr, "prefix import map mismatch: %s\n", mapped);
        failed = 1;
    }
    failed |= rejects(base, "react");
    failed |= rejects(base, "@scope/package/module.js");
    failed |= rejects(base, "data:text/javascript,export%20{} ");
    failed |= rejects("file:///tmp/main.js", "./module.js");
    if (failed) return 1;
    puts("QuickJS module URL PASS: relative, root, query, protocol and bounds");
    return 0;
}
