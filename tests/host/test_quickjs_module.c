#include <quickjs.h>
#include "../../programs/qjs/module_url.h"

#include <stdio.h>
#include <string.h>

static unsigned loaded_modules;
static const qjs_import_map_entry_t test_imports[] = {
    {"react", "https://example.test/js/value.js"},
};

static char *normalize_test_module(JSContext *context, const char *base_name,
                                   const char *module_name, void *opaque) {
    (void)opaque;
    char resolved[512];
    if (qjs_resolve_module_url_with_import_map(base_name, module_name,
            test_imports, sizeof(test_imports) / sizeof(test_imports[0]),
            resolved, sizeof(resolved)) != 0) {
        JS_ThrowTypeError(context, "cannot resolve test module");
        return NULL;
    }
    size_t length = strlen(resolved);
    char *copy = js_malloc(context, length + 1);
    if (copy) memcpy(copy, resolved, length + 1);
    return copy;
}

static JSModuleDef *load_test_module(JSContext *context, const char *module_name,
                                     void *opaque) {
    (void)opaque;
    static const char source[] = "export const value = 42;";
    if (strcmp(module_name, "https://example.test/js/value.js") != 0) {
        JS_ThrowReferenceError(context, "unexpected module URL");
        return NULL;
    }
    loaded_modules++;
    JSValue compiled = JS_Eval(context, source, sizeof(source) - 1,
                               module_name,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) return NULL;
    JSModuleDef *module = (JSModuleDef *)JS_VALUE_GET_PTR(compiled);
    JS_FreeValue(context, compiled);
    return module;
}

static int evaluate(JSContext *context, const char *source, int eval_type,
                    const char *filename) {
    JSValue result = JS_Eval(context, source, strlen(source), filename,
                             eval_type);
    if (!JS_IsException(result)) {
        JS_FreeValue(context, result);
        return 0;
    }
    JSValue exception = JS_GetException(context);
    const char *message = JS_ToCString(context, exception);
    fprintf(stderr, "QuickJS eval failed (%s): %s\n", filename,
            message ? message : "unknown exception");
    if (message) JS_FreeCString(context, message);
    JS_FreeValue(context, exception);
    JS_FreeValue(context, result);
    return 1;
}

int main(void) {
    JSRuntime *runtime = JS_NewRuntime();
    if (!runtime) return 1;
    JS_SetMemoryLimit(runtime, 16U * 1024U * 1024U);
    JS_SetMaxStackSize(runtime, 1024U * 1024U);
    JSContext *context = JS_NewContext(runtime);
    if (!context) {
        JS_FreeRuntime(runtime);
        return 1;
    }

    JS_SetModuleLoaderFunc(runtime, normalize_test_module,
                           load_test_module, NULL);
    int failed = evaluate(context, "globalThis.classicReady = true;",
                          JS_EVAL_TYPE_GLOBAL, "<classic-script>") ||
                 evaluate(context, "export const answer = 42;",
                          JS_EVAL_TYPE_MODULE, "https://example.test/module.js") ||
                 evaluate(context,
                     "import { value } from 'react';\n"
                     "import { value as valueAgain } from 'react';\n"
                     "globalThis.imported = value + valueAgain;",
                     JS_EVAL_TYPE_MODULE,
                     "https://example.test/js/main.js");
    JSContext *job_context = context;
    for (;;) {
        JSContext *context_out = NULL;
        int job = JS_ExecutePendingJob(runtime, &context_out);
        if (job == 0) break;
        if (job < 0) {
            JSValue exception = JS_GetException(
                context_out ? context_out : job_context);
            const char *message = JS_ToCString(
                context_out ? context_out : job_context, exception);
            fprintf(stderr, "QuickJS module job failed: %s\n",
                    message ? message : "unknown exception");
            if (message) JS_FreeCString(context_out ? context_out : job_context,
                                        message);
            JS_FreeValue(context_out ? context_out : job_context, exception);
            failed = 1;
            break;
        }
    }
    JSValue global = JS_GetGlobalObject(context);
    JSValue imported = JS_GetPropertyStr(context, global, "imported");
    int32_t imported_value = 0;
    if (JS_ToInt32(context, &imported_value, imported) < 0 ||
        imported_value != 84 || loaded_modules != 1) {
        fprintf(stderr, "QuickJS module import mismatch: value=%d loads=%u\n",
                imported_value, loaded_modules);
        failed = 1;
    }
    JS_FreeValue(context, imported);
    JS_FreeValue(context, global);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    if (failed) return 2;
    puts("QuickJS module PASS: standalone module and relative import graph evaluated");
    return 0;
}
