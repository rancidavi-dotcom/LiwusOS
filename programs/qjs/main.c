#include <liwus/fcntl.h>
#include <liwus/liwus.h>
#include <liwus/stdio.h>
#include <liwus/stdlib.h>
#include <liwus/string.h>
#include <stdbool.h>
#include <quickjs.h>
#include "module_url.h"
#include <liwus/webdom.h>

#define QJS_MAX_SOURCE_BYTES (8u * 1024u * 1024u)
#define QJS_MEMORY_LIMIT_BYTES (64u * 1024u * 1024u)
#define QJS_STACK_LIMIT_BYTES (384u * 1024u)
#define QJS_INTERRUPT_CALLBACK_LIMIT 100000u
#define QJS_MAX_DOM_TEXT_BYTES LIW_WEBDOM_MAX_SOURCE
#define QJS_DOM_MAX_EVENT_LISTENERS 1024u
#define QJS_DOM_MAX_EVENT_TYPE 127u
#define QJS_MICROTASK_CHECKPOINT_LIMIT 1024u
#define QJS_MAX_TIMERS 256u
#define QJS_TIMER_CALLBACK_LIMIT 128u
#define QJS_TIMER_BOOTSTRAP_WINDOW_TICKS 50u
#define QJS_MAX_PENDING_FETCHES 32u
#define QJS_MAX_FETCH_BYTES (4u * 1024u * 1024u)
#define QJS_FETCH_URL_BYTES 2048u
#define QJS_FETCH_METHOD_BYTES 16u
#define QJS_FETCH_HEADERS_BYTES 2048u
#define QJS_FETCH_BODY_BYTES 2048u
#define QJS_MAX_MODULES 64u
#define QJS_MAX_MODULE_BYTES (4u * 1024u * 1024u)
#define QJS_MODULE_FETCH_TIMEOUT_TICKS 2000u
#define QJS_MODULE_FETCH_ID_BASE 0x40000000u
#define QJS_FETCH_RESPONSE_LINE_BYTES 1536u
#define QJS_DEFERRED_FETCH_RESPONSES 64u
#define QJS_IMPORT_MAP_MAX_ENTRIES 32u
#define QJS_IMPORT_MAP_SPECIFIER_BYTES 128u
#define QJS_IMPORT_MAP_SOURCE_BYTES (64u * 1024u)

typedef union {
    size_t size;
    long double alignment_ld;
    void *alignment_ptr;
} qjs_alloc_header_t;

typedef struct {
    size_t interrupt_callbacks_left;
} qjs_runtime_state_t;

static qjs_runtime_state_t *qjs_active_runtime_state;

static void qjs_reset_execution_budget(void) {
    if (qjs_active_runtime_state)
        qjs_active_runtime_state->interrupt_callbacks_left =
            QJS_INTERRUPT_CALLBACK_LIMIT;
}

typedef struct {
    uint32_t id;
    uint64_t due_ticks;
    uint64_t interval_ticks;
    JSValue callback;
    int active;
} qjs_timer_t;

typedef struct {
    uint32_t id;
    JSValue resolve;
    JSValue reject;
    int active;
    int sent;
    char url[QJS_FETCH_URL_BYTES];
    char method[QJS_FETCH_METHOD_BYTES];
    char headers[QJS_FETCH_HEADERS_BYTES];
    char body[QJS_FETCH_BODY_BYTES];
    size_t body_length;
} qjs_fetch_t;

typedef struct {
    liw_web_document_t *document;
    liw_web_node_id_t node;
    int is_static;
} qjs_dom_node_t;

typedef struct {
    char type[QJS_DOM_MAX_EVENT_TYPE + 1];
    liw_web_node_id_t target;
    liw_web_node_id_t current_target;
    int bubbles;
    int cancelable;
    int default_prevented;
    int propagation_stopped;
    int immediate_stopped;
    int event_phase;
    int dispatching;
} qjs_dom_event_t;

typedef struct {
    liw_web_node_id_t node;
    char type[QJS_DOM_MAX_EVENT_TYPE + 1];
    JSValue callback;
    JSValue signal;
    int capture;
    int once;
    int object_listener;
    int active;
} qjs_dom_listener_t;

static JSClassID qjs_dom_node_class;
static JSClassID qjs_dom_event_class;
static liw_web_document_t *qjs_dom_document;
static qjs_dom_node_t qjs_dom_document_node;
static JSValue qjs_dom_node_cache[LIW_WEBDOM_MAX_NODES];
static uint8_t qjs_dom_node_cache_valid[LIW_WEBDOM_MAX_NODES];
static JSValue qjs_dom_document_object;
static int qjs_dom_document_object_valid;
static bool qjs_dump_html;
static bool qjs_persistent_page;
static qjs_dom_listener_t qjs_dom_listeners[QJS_DOM_MAX_EVENT_LISTENERS];
static size_t qjs_dom_listener_count;
static qjs_timer_t qjs_timers[QJS_MAX_TIMERS];
static uint32_t qjs_next_timer_id = 1;
static qjs_fetch_t qjs_fetches[QJS_MAX_PENDING_FETCHES];
static uint32_t qjs_next_fetch_id = 1;
static uint32_t qjs_fetch_generation;
static int qjs_fetch_request_fd = -1;
static int qjs_fetch_response_fd = -1;
static char qjs_fetch_response_buffer[4096];
static size_t qjs_fetch_response_length;
static char qjs_deferred_fetch_responses[QJS_DEFERRED_FETCH_RESPONSES]
                                        [QJS_FETCH_RESPONSE_LINE_BYTES];
static size_t qjs_deferred_fetch_head;
static size_t qjs_deferred_fetch_count;
static uint32_t qjs_next_module_fetch_id = QJS_MODULE_FETCH_ID_BASE;
static size_t qjs_module_count;
static size_t qjs_module_source_bytes;
static uint32_t qjs_module_wait_ticks;
static char qjs_import_map_specifiers[QJS_IMPORT_MAP_MAX_ENTRIES]
                                     [QJS_IMPORT_MAP_SPECIFIER_BYTES];
static char qjs_import_map_addresses[QJS_IMPORT_MAP_MAX_ENTRIES]
                                    [QJS_FETCH_URL_BYTES];
static size_t qjs_import_map_count;
static char qjs_location_href[QJS_FETCH_URL_BYTES];
static JSValue qjs_dom_node_text(JSContext *ctx, JSValueConst this_value);
static JSValue qjs_dom_set_node_text(JSContext *ctx, JSValueConst this_value,
                                     JSValueConst value);
static void print_exception(JSContext *ctx);
static int qjs_dom_space(char value);
static int qjs_slice_equal(const char *left, size_t left_length,
                           const char *right, size_t right_length,
                           int case_insensitive);
static int qjs_dispatch_native_event(JSContext *ctx, const char *element_id,
                                     const char *type, const char *value);
static char *read_bounded_file(const char *path, size_t maximum,
                               size_t *length_out);

static JSValue qjs_fetch(JSContext *ctx, JSValueConst this_value, int argc,
                         JSValueConst *argv) {
    (void)this_value;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "fetch requires a URL");
    if (qjs_fetch_request_fd < 0) {
        JSValue resolving[2];
        JSValue promise = JS_NewPromiseCapability(ctx, resolving);
        if (JS_IsException(promise)) return promise;
        JSValue err = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, err, "message",
                          JS_NewString(ctx, "fetch network bridge unavailable"));
        JS_Call(ctx, resolving[1], JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, err);
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        return promise;
    }
    const char *url = JS_ToCString(ctx, argv[0]);
    if (!url) return JS_EXCEPTION;
    size_t url_length = strlen(url);
    if (!url_length || url_length >= QJS_FETCH_URL_BYTES ||
        strchr(url, '\n') || strchr(url, '\r')) {
        JS_FreeCString(ctx, url);
        return JS_ThrowRangeError(ctx, "fetch URL is invalid or too long");
    }

    size_t slot = QJS_MAX_PENDING_FETCHES;
    for (size_t i = 0; i < QJS_MAX_PENDING_FETCHES; ++i) {
        if (!qjs_fetches[i].active) { slot = i; break; }
    }
    if (slot == QJS_MAX_PENDING_FETCHES) {
        JS_FreeCString(ctx, url);
        return JS_ThrowRangeError(ctx, "too many pending fetch requests");
    }

    JSValue resolving[2] = {JS_UNDEFINED, JS_UNDEFINED};
    JSValue promise = JS_NewPromiseCapability(ctx, resolving);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, resolving[0]);
        JS_FreeValue(ctx, resolving[1]);
        JS_FreeCString(ctx, url);
        return promise;
    }

    qjs_fetch_t *fetch = &qjs_fetches[slot];
    fetch->id = qjs_next_fetch_id++;
    if (fetch->id == 0) fetch->id = qjs_next_fetch_id++;
    fetch->resolve = resolving[0];
    fetch->reject = resolving[1];
    fetch->active = 1;
    fetch->sent = 0;
    memcpy(fetch->url, url, url_length + 1);
    strcpy(fetch->method, "GET");
    fetch->headers[0] = '\0';
    fetch->body_length = 0;
    fetch->body[0] = '\0';
    if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        const char *method = JS_ToCString(ctx, argv[1]);
        if (!method) {
            JS_FreeCString(ctx, url);
            fetch->active = 0;
            JS_FreeValue(ctx, resolving[0]);
            JS_FreeValue(ctx, resolving[1]);
            JS_FreeValue(ctx, promise);
            return JS_EXCEPTION;
        }
        size_t mlen = strlen(method);
        if (mlen == 0 || mlen >= sizeof(fetch->method)) {
            JS_FreeCString(ctx, method);
            JS_FreeCString(ctx, url);
            fetch->active = 0;
            JS_FreeValue(ctx, resolving[0]);
            JS_FreeValue(ctx, resolving[1]);
            JS_FreeValue(ctx, promise);
            return JS_ThrowRangeError(ctx, "fetch method is invalid or too long");
        }
        for (size_t i = 0; i < mlen; ++i) {
            char c = method[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
            if (c <= ' ' || c == '\t' || c == '\r' || c == '\n') {
                JS_FreeCString(ctx, method);
                JS_FreeCString(ctx, url);
                fetch->active = 0;
                JS_FreeValue(ctx, resolving[0]);
                JS_FreeValue(ctx, resolving[1]);
                JS_FreeValue(ctx, promise);
                return JS_ThrowRangeError(ctx, "fetch method contains control characters");
            }
            fetch->method[i] = c;
        }
        fetch->method[mlen] = '\0';
        JS_FreeCString(ctx, method);
    }
    if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2])) {
        const char *headers = JS_ToCString(ctx, argv[2]);
        if (!headers) {
            JS_FreeCString(ctx, url);
            fetch->active = 0;
            JS_FreeValue(ctx, resolving[0]);
            JS_FreeValue(ctx, resolving[1]);
            JS_FreeValue(ctx, promise);
            return JS_EXCEPTION;
        }
        size_t hlen = strlen(headers);
        if (hlen >= sizeof(fetch->headers)) {
            JS_FreeCString(ctx, headers);
            JS_FreeCString(ctx, url);
            fetch->active = 0;
            JS_FreeValue(ctx, resolving[0]);
            JS_FreeValue(ctx, resolving[1]);
            JS_FreeValue(ctx, promise);
            return JS_ThrowRangeError(ctx, "fetch headers are too long");
        }
        for (size_t i = 0; i < hlen; ++i) {
            unsigned char c = (unsigned char)headers[i];
            if (c == '\r' || c == '\n' || c == '\t') c = ' ';
            if (c == '|') c = ' ';
            if (c < 0x20U || c == 0x7fU) c = ' ';
            fetch->headers[i] = (char)c;
        }
        fetch->headers[hlen] = '\0';
        JS_FreeCString(ctx, headers);
    }
    if (argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        size_t blen = 0;
        const char *body = JS_ToCStringLen(ctx, &blen, argv[3]);
        if (!body) {
            JS_FreeCString(ctx, url);
            fetch->active = 0;
            JS_FreeValue(ctx, resolving[0]);
            JS_FreeValue(ctx, resolving[1]);
            JS_FreeValue(ctx, promise);
            return JS_EXCEPTION;
        }
        if (blen > sizeof(fetch->body)) {
            JS_FreeCString(ctx, body);
            JS_FreeCString(ctx, url);
            fetch->active = 0;
            JS_FreeValue(ctx, resolving[0]);
            JS_FreeValue(ctx, resolving[1]);
            JS_FreeValue(ctx, promise);
            return JS_ThrowRangeError(ctx, "fetch body exceeds bounded request limit");
        }
        memcpy(fetch->body, body, blen);
        fetch->body[blen] = '\0';
        fetch->body_length = blen;
        JS_FreeCString(ctx, body);
    }
    JS_FreeCString(ctx, url);
    return promise;
}

static int qjs_install_fetch(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue function = JS_NewCFunction(ctx, qjs_fetch, "fetch", 1);
    if (JS_IsException(function) ||
        JS_SetPropertyStr(ctx, global, "fetch", function) < 0) {
        JS_FreeValue(ctx, global);
        return -1;
    }
    static const char response_factory[] =
        "globalThis.__liwusMakeResponse=function(status,body,url,headers){"
        "if(globalThis.Response)return new globalThis.Response(body,{status:status,url:url||'',headers:headers||{}});"
        "return {status:status,ok:status>=200&&status<300,url:url||'',body:body,headers:new Headers(headers||{}),"
        "text:function(){return Promise.resolve(body)},"
        "json:function(){return Promise.resolve(JSON.parse(body))}}};";
    JSValue result = JS_Eval(ctx, response_factory,
                             sizeof(response_factory) - 1,
                             "<liwus-fetch>", JS_EVAL_TYPE_GLOBAL);
    JS_FreeValue(ctx, global);
    if (JS_IsException(result)) return -1;
    JS_FreeValue(ctx, result);
    return 0;
}

static int qjs_set_string(JSContext *ctx, JSValueConst object,
                          const char *name, const char *value) {
    JSValue string = JS_NewString(ctx, value ? value : "");
    if (JS_IsException(string)) return -1;
    return JS_SetPropertyStr(ctx, object, name, string);
}

static const char qjs_web_env_bootstrap[] =
    "(function(){\n"
    "  var g = globalThis;\n"
    "  g.window = g;\n"
    "  g.self = g;\n"
    "  g.top = g;\n"
    "  g.parent = g;\n"
    "\n"
    "  class EventTarget {\n"
    "    constructor() { this._l = {}; }\n"
    "    addEventListener(t, cb, opts) {\n"
    "      if (!cb || (typeof cb !== 'function' && typeof cb.handleEvent !== 'function')) return;\n"
    "      t = String(t); if (!this._l[t]) this._l[t] = [];\n"
    "      opts = (opts && typeof opts === 'object') ? opts : {};\n"
    "      if (opts.signal && opts.signal.aborted) return;\n"
    "      var entry = { cb: cb, once: Boolean(opts.once), capture: Boolean(opts.capture), passive: Boolean(opts.passive), removed: false };\n"
    "      if (opts.signal && opts.signal.addEventListener) {\n"
    "        var self = this; entry.signal = opts.signal; entry.abort = function() { entry.removed = true; self._l[t] = (self._l[t] || []).filter(function(x) { return x !== entry; }); };\n"
    "        opts.signal.addEventListener('abort', entry.abort, { once: true });\n"
    "      }\n"
    "      this._l[t].push(entry);\n"
    "    }\n"
    "    removeEventListener(t, cb, opts) {\n"
    "      t = String(t); if (!this._l[t]) return;\n"
    "      var capture = Boolean(opts && typeof opts === 'object' ? opts.capture : opts);\n"
    "      this._l[t] = this._l[t].filter(function(x) { if (x.cb === cb && x.capture === capture) { x.removed = true; return false; } return true; });\n"
    "    }\n"
    "    dispatchEvent(e) {\n"
    "      if (!e || !e.type) return true;\n"
    "      var type = String(e.type), list = (this._l[type] || []).slice();\n"
    "      try { if (!e.target) e.target = this; } catch (err) {}\n"
    "      try { e.currentTarget = this; } catch (err) {}\n"
    "      for (var i = 0; i < list.length; i++) {\n"
    "        var entry = list[i]; if (entry.removed) continue;\n"
    "        if (entry.once) this.removeEventListener(type, entry.cb, { capture: entry.capture });\n"
    "        try { if (typeof entry.cb === 'function') entry.cb.call(this, e); else entry.cb.handleEvent(e); } catch (err) { console.error(err); }\n"
    "      }\n"
    "      return !e.defaultPrevented;\n"
    "    }\n"
    "  }\n"
    "  g.EventTarget = EventTarget;\n"
    "\n"
    "  var winEv = new EventTarget();\n"
    "  g.addEventListener = winEv.addEventListener.bind(winEv);\n"
    "  g.removeEventListener = winEv.removeEventListener.bind(winEv);\n"
    "  g.dispatchEvent = winEv.dispatchEvent.bind(winEv);\n"
    "\n"
    "  class DOMException extends Error {\n"
    "    constructor(message, name) {\n"
    "      super(message);\n"
    "      this.name = name || 'Error';\n"
    "      this.code = 0;\n"
    "    }\n"
    "  }\n"
    "  g.DOMException = DOMException;\n"
    "\n"
    "  class AbortSignal extends EventTarget {\n"
    "    constructor() {\n"
    "      super();\n"
    "      this.aborted = false;\n"
    "      this.reason = undefined;\n"
    "      this.onabort = null;\n"
    "    }\n"
    "    static abort(reason) {\n"
    "      var s = new AbortSignal();\n"
    "      s.aborted = true;\n"
    "      s.reason = reason || new Error('This operation was aborted');\n"
    "      return s;\n"
    "    }\n"
    "    static timeout(ms) {\n"
    "      var s = new AbortSignal();\n"
    "      setTimeout(function() {\n"
    "        if (s.aborted) return;\n"
    "        s.aborted = true;\n"
    "        s.reason = new DOMException('TimeoutError', 'TimeoutError');\n"
    "        s.dispatchEvent(new Event('abort'));\n"
    "        if (s.onabort) s.onabort();\n"
    "      }, ms);\n"
    "      return s;\n"
    "    }\n"
    "    static any(signals) {\n"
    "      var out = new AbortSignal();\n"
    "      var list = Array.from(signals || []);\n"
    "      function adopt(s) {\n"
    "        if (!s || out.aborted) return;\n"
    "        if (s.aborted) { out.aborted = true; out.reason = s.reason; out.dispatchEvent(new Event('abort')); if (out.onabort) out.onabort(); return; }\n"
    "        if (s.addEventListener) s.addEventListener('abort', function() {\n"
    "          if (out.aborted) return;\n"
    "          out.aborted = true; out.reason = s.reason;\n"
    "          out.dispatchEvent(new Event('abort')); if (out.onabort) out.onabort();\n"
    "        }, { once: true });\n"
    "      }\n"
    "      for (var i = 0; i < list.length; i++) adopt(list[i]);\n"
    "      return out;\n"
    "    }\n"
    "    throwIfAborted() { if (this.aborted) throw (this.reason || new DOMException('The operation was aborted', 'AbortError')); }\n"
    "  }\n"
    "  g.AbortSignal = AbortSignal;\n"
    "\n"
    "  class AbortController {\n"
    "    constructor() { this.signal = new AbortSignal(); }\n"
    "    abort(reason) {\n"
    "      if (this.signal.aborted) return;\n"
    "      this.signal.aborted = true;\n"
    "      this.signal.reason = reason || new Error('This operation was aborted');\n"
    "      this.signal.dispatchEvent(new Event('abort'));\n"
    "      if (this.signal.onabort) this.signal.onabort();\n"
    "    }\n"
    "  }\n"
    "  g.AbortController = AbortController;\n"
    "\n"
    "  /* Typed DOM events used by component libraries and form/event adapters. */\n"
    "  if (!g.CustomEvent) g.CustomEvent = class CustomEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.detail = init.detail === undefined ? null : init.detail; }\n"
    "  };\n"
    "  if (g.Event && g.Event.prototype) {\n"
    "    if (!g.Event.prototype.composedPath) g.Event.prototype.composedPath = function() { var path = [], node = this.target; while (node) { path.push(node); node = node.parentNode || null; } if (g.window) path.push(g.window); return path; };\n"
    "    if (!Object.getOwnPropertyDescriptor(g.Event.prototype, 'isTrusted')) Object.defineProperty(g.Event.prototype, 'isTrusted', { get: function() { return false; }, configurable: true });\n"
    "  }\n"
    "  if (!g.UIEvent) g.UIEvent = class UIEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.view = init.view || g; this.detail = Number(init.detail || 0); }\n"
    "  };\n"
    "  if (!g.FocusEvent) g.FocusEvent = class FocusEvent extends UIEvent {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.relatedTarget = init.relatedTarget || null; }\n"
    "  };\n"
    "  if (!g.MouseEvent) g.MouseEvent = class MouseEvent extends UIEvent {\n"
    "    constructor(type, init) { init = init || {}; super(type, init);\n"
    "      this.screenX = Number(init.screenX || 0); this.screenY = Number(init.screenY || 0);\n"
    "      this.clientX = Number(init.clientX || 0); this.clientY = Number(init.clientY || 0);\n"
    "      this.ctrlKey = Boolean(init.ctrlKey); this.shiftKey = Boolean(init.shiftKey);\n"
    "      this.altKey = Boolean(init.altKey); this.metaKey = Boolean(init.metaKey);\n"
    "      this.button = Number(init.button || 0); this.buttons = Number(init.buttons || 0);\n"
    "      this.relatedTarget = init.relatedTarget || null; this.movementX = Number(init.movementX || 0); this.movementY = Number(init.movementY || 0);\n"
    "    }\n"
    "    getModifierState(key) { key = String(key); if (key === 'Control') return this.ctrlKey; if (key === 'Shift') return this.shiftKey; if (key === 'Alt') return this.altKey; if (key === 'Meta') return this.metaKey; return false; }\n"
    "  };\n"
    "  if (!g.PointerEvent) g.PointerEvent = function(type, init) { init = init || {}; var e = new g.MouseEvent(type, init); e.pointerId = Number(init.pointerId || 0); e.width = Number(init.width || 1); e.height = Number(init.height || 1); e.pressure = Number(init.pressure || 0); e.tangentialPressure = Number(init.tangentialPressure || 0); e.tiltX = Number(init.tiltX || 0); e.tiltY = Number(init.tiltY || 0); e.twist = Number(init.twist || 0); e.pointerType = init.pointerType || ''; e.isPrimary = Boolean(init.isPrimary); return e; };\n"
    "  if (!g.KeyboardEvent) g.KeyboardEvent = class KeyboardEvent extends UIEvent {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.key = init.key || ''; this.code = init.code || ''; this.location = Number(init.location || 0); this.ctrlKey = Boolean(init.ctrlKey); this.shiftKey = Boolean(init.shiftKey); this.altKey = Boolean(init.altKey); this.metaKey = Boolean(init.metaKey); this.repeat = Boolean(init.repeat); this.isComposing = Boolean(init.isComposing); this.charCode = Number(init.charCode || 0); this.keyCode = Number(init.keyCode || 0); this.which = Number(init.which || this.keyCode || 0); }\n"
    "    getModifierState(key) { key = String(key); if (key === 'Control') return this.ctrlKey; if (key === 'Shift') return this.shiftKey; if (key === 'Alt') return this.altKey; if (key === 'Meta') return this.metaKey; return false; }\n"
    "  };\n"
    "  if (!g.InputEvent) g.InputEvent = class InputEvent extends UIEvent {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.data = init.data == null ? null : String(init.data); this.inputType = init.inputType || ''; this.isComposing = Boolean(init.isComposing); this.dataTransfer = init.dataTransfer || null; }\n"
    "  };\n"
    "  if (!g.SubmitEvent) g.SubmitEvent = class SubmitEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.submitter = init.submitter || null; }\n"
    "  };\n"
    "  if (!g.MessageEvent) g.MessageEvent = class MessageEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.data = init.data === undefined ? null : init.data; this.origin = init.origin || ''; this.lastEventId = init.lastEventId || ''; this.source = init.source || null; this.ports = Array.isArray(init.ports) ? init.ports : []; }\n"
    "  };\n"
    "  if (!g.DataTransfer) g.DataTransfer = class DataTransfer {\n"
    "    constructor() { this._data = new Map(); this.dropEffect = 'none'; this.effectAllowed = 'uninitialized'; this.files = []; this.items = []; }\n"
    "    setData(type, value) { type = String(type).toLowerCase(); this._data.set(type, String(value)); this.items = Array.from(this._data.keys()).map(function(k) { var v = this._data.get(k); return { kind: 'string', type: k, getAsString: function(cb) { if (cb) cb(v); } }; }, this); }\n"
    "    getData(type) { return this._data.get(String(type).toLowerCase()) || ''; }\n"
    "    clearData(type) { if (type === undefined) this._data.clear(); else this._data.delete(String(type).toLowerCase()); }\n"
    "    setDragImage() {}\n"
    "    get types() { return Array.from(this._data.keys()); }\n"
    "  };\n"
    "  if (!g.ClipboardEvent) g.ClipboardEvent = class ClipboardEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.clipboardData = init.clipboardData || new g.DataTransfer(); }\n"
    "  };\n"
    "  if (!g.ProgressEvent) g.ProgressEvent = class ProgressEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.lengthComputable = Boolean(init.lengthComputable); this.loaded = Number(init.loaded || 0); this.total = Number(init.total || 0); }\n"
    "  };\n"
    "  if (!g.ErrorEvent) g.ErrorEvent = class ErrorEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.message = init.message || ''; this.filename = init.filename || ''; this.lineno = Number(init.lineno || 0); this.colno = Number(init.colno || 0); this.error = init.error || null; }\n"
    "  };\n"
    "  if (!g.CloseEvent) g.CloseEvent = class CloseEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.code = Number(init.code || 0); this.reason = init.reason || ''; this.wasClean = Boolean(init.wasClean); }\n"
    "  };\n"
    "  if (!g.PopStateEvent) g.PopStateEvent = class PopStateEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.state = init.state === undefined ? null : init.state; }\n"
    "  };\n"
    "  if (!g.HashChangeEvent) g.HashChangeEvent = class HashChangeEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.oldURL = init.oldURL || ''; this.newURL = init.newURL || ''; }\n"
    "  };\n"
    "\n"
    "  /* Geometry primitives used by layout libraries and intersection code. */\n"
    "  if (!g.DOMRectReadOnly) g.DOMRectReadOnly = class DOMRectReadOnly {\n"
    "    constructor(x, y, width, height) {\n"
    "      this.x = Number(x || 0); this.y = Number(y || 0);\n"
    "      this.width = Number(width || 0); this.height = Number(height || 0);\n"
    "    }\n"
    "    get top() { return Math.min(this.y, this.y + this.height); }\n"
    "    get right() { return Math.max(this.x, this.x + this.width); }\n"
    "    get bottom() { return Math.max(this.y, this.y + this.height); }\n"
    "    get left() { return Math.min(this.x, this.x + this.width); }\n"
    "    toJSON() { return { x: this.x, y: this.y, width: this.width, height: this.height, top: this.top, right: this.right, bottom: this.bottom, left: this.left }; }\n"
    "  };\n"
    "  if (!g.DOMRect) g.DOMRect = class DOMRect extends g.DOMRectReadOnly {\n"
    "    constructor(x, y, width, height) { super(x, y, width, height); }\n"
    "    static fromRect(other) { other = other || {}; return new g.DOMRect(other.x, other.y, other.width, other.height); }\n"
    "  };\n"
    "  if (!g.DOMPoint) g.DOMPoint = class DOMPoint {\n"
    "    constructor(x, y, z, w) { this.x = Number(x || 0); this.y = Number(y || 0); this.z = Number(z || 0); this.w = w === undefined ? 1 : Number(w); }\n"
    "    static fromPoint(other) { other = other || {}; return new g.DOMPoint(other.x, other.y, other.z, other.w); }\n"
    "  };\n"
    "  if (!g.DOMPointReadOnly) g.DOMPointReadOnly = g.DOMPoint;\n"
    "  if (!g.DOMMatrix) g.DOMMatrix = class DOMMatrix {\n"
    "    constructor(values) {\n"
    "      this.a = 1; this.b = 0; this.c = 0; this.d = 1; this.e = 0; this.f = 0;\n"
    "      this.m11 = 1; this.m12 = 0; this.m21 = 0; this.m22 = 1; this.m41 = 0; this.m42 = 0;\n"
    "      if (Array.isArray(values) && values.length >= 6) { this.a = Number(values[0]); this.b = Number(values[1]); this.c = Number(values[2]); this.d = Number(values[3]); this.e = Number(values[4]); this.f = Number(values[5]); this.m11 = this.a; this.m12 = this.b; this.m21 = this.c; this.m22 = this.d; this.m41 = this.e; this.m42 = this.f; }\n"
    "    }\n"
    "    translate(tx, ty) { var out = new g.DOMMatrix([this.a, this.b, this.c, this.d, this.e + Number(tx || 0), this.f + Number(ty || 0)]); return out; }\n"
    "    scale(sx, sy) { sy = sy === undefined ? sx : sy; return new g.DOMMatrix([this.a * Number(sx || 0), this.b, this.c, this.d * Number(sy || 0), this.e, this.f]); }\n"
    "    rotate(angle) { var r = Number(angle || 0) * Math.PI / 180, c = Math.cos(r), s = Math.sin(r); return this.multiply(new g.DOMMatrix([c, s, -s, c, 0, 0])); }\n"
    "    multiply(other) { other = other || new g.DOMMatrix(); return new g.DOMMatrix([this.a * other.a + this.c * other.b, this.b * other.a + this.d * other.b, this.a * other.c + this.c * other.d, this.b * other.c + this.d * other.d, this.a * other.e + this.c * other.f + this.e, this.b * other.e + this.d * other.f + this.f]); }\n"
    "    inverse() { var det = this.a * this.d - this.b * this.c; if (!det) throw new DOMException('Matrix is not invertible', 'InvalidStateError'); return new g.DOMMatrix([this.d / det, -this.b / det, -this.c / det, this.a / det, (this.c * this.f - this.d * this.e) / det, (this.b * this.e - this.a * this.f) / det]); }\n"
    "    get is2D() { return true; }\n"
    "    toFloat32Array() { return new Float32Array([this.a, this.b, this.c, this.d, this.e, this.f]); }\n"
    "    toFloat64Array() { return new Float64Array([this.a, this.b, this.c, this.d, this.e, this.f]); }\n"
    "    toString() { return 'matrix(' + [this.a, this.b, this.c, this.d, this.e, this.f].join(', ') + ')'; }\n"
    "  };\n"
    "  if (!g.DOMMatrixReadOnly) g.DOMMatrixReadOnly = g.DOMMatrix;\n"
    "\n"
    "  class CustomElementRegistry {\n"
    "    constructor() {\n"
    "      this._registry = new Map();\n"
    "      this._whenDefined = new Map();\n"
    "    }\n"
    "    define(name, ctor) {\n"
    "      name = String(name).toLowerCase();\n"
    "      if (this._registry.has(name)) {\n"
    "        throw new DOMException(\"Custom element '\" + name + \"' already defined.\", 'NotSupportedError');\n"
    "      }\n"
    "      this._registry.set(name, { ctor: ctor });\n"
    "      if (this._whenDefined.has(name)) {\n"
    "        var resolves = this._whenDefined.get(name);\n"
    "        for (var i = 0; i < resolves.length; i++) { try { resolves[i](ctor); } catch (e) {} }\n"
    "        this._whenDefined.delete(name);\n"
    "      }\n"
    "      if (typeof document !== 'undefined' && document.querySelectorAll) {\n"
    "        try {\n"
    "          var els = document.querySelectorAll(name);\n"
    "          for (var i = 0; i < els.length; i++) this.upgrade(els[i]);\n"
    "        } catch (e) {}\n"
    "      }\n"
    "    }\n"
    "    get(name) {\n"
    "      var entry = this._registry.get(String(name).toLowerCase());\n"
    "      return entry ? entry.ctor : undefined;\n"
    "    }\n"
    "    upgrade(el) {\n"
    "      if (!el || !el.tagName) return;\n"
    "      var name = el.tagName.toLowerCase();\n"
    "      var entry = this._registry.get(name);\n"
    "      if (!entry || el._upgraded) return;\n"
    "      el._upgraded = true;\n"
    "      el._customElementDefinition = entry;\n"
    "      try {\n"
    "        Object.setPrototypeOf(el, entry.ctor.prototype);\n"
    "        var parent = el.parentNode, connected = false;\n"
    "        while (parent) { if (parent === document) { connected = true; break; } parent = parent.parentNode; }\n"
    "        if (connected && typeof el.connectedCallback === 'function') { el._customElementConnected = true; el.connectedCallback(); }\n"
    "      } catch (err) {\n"
    "        console.warn('Custom element upgrade error:', err);\n"
    "      }\n"
    "    }\n"
    "    whenDefined(name) {\n"
    "      name = String(name).toLowerCase();\n"
    "      var entry = this._registry.get(name);\n"
    "      if (entry) return Promise.resolve(entry.ctor);\n"
    "      var self = this;\n"
    "      return new Promise(function(resolve) {\n"
    "        if (!self._whenDefined.has(name)) self._whenDefined.set(name, []);\n"
    "        self._whenDefined.get(name).push(resolve);\n"
    "      });\n"
    "    }\n"
    "  }\n"
    "  g.customElements = new CustomElementRegistry();\n"
    "  function __liwusWalkCustom(node, cb) { if (!node) return; if (node._customElementDefinition) cb(node); var child = node.firstChild; while (child) { __liwusWalkCustom(child, cb); child = child.nextSibling; } }\n"
    "  function __liwusConnectCustom(node) { __liwusWalkCustom(node, function(el) { if (!el._customElementConnected && typeof el.connectedCallback === 'function') { el._customElementConnected = true; try { el.connectedCallback(); } catch (e) { console.warn('Custom element connectedCallback error:', e); } } }); }\n"
    "  function __liwusDisconnectCustom(node) { __liwusWalkCustom(node, function(el) { if (el._customElementConnected) { el._customElementConnected = false; if (typeof el.disconnectedCallback === 'function') try { el.disconnectedCallback(); } catch (e) { console.warn('Custom element disconnectedCallback error:', e); } } }); }\n"
    "\n"
    "  var proto = null;\n"
    "  if (typeof document !== 'undefined') {\n"
    "    if (document.body) proto = Object.getPrototypeOf(document.body);\n"
    "    if (!proto) proto = Object.getPrototypeOf(document);\n"
    "  }\n"
    "\n"
    "  /* The native DOM bridge returns bounded snapshot arrays. Give those\n"
    "   * snapshots the collection methods and brands expected by web code,\n"
    "   * while deliberately keeping them non-live and memory bounded. */\n"
    "  if (!g.NodeList) {\n"
    "    g.NodeList = class NodeList extends Array {};\n"
    "    g.NodeList.prototype.item = function(index) { index = Number(index); return Number.isInteger(index) && index >= 0 && index < this.length ? this[index] : null; };\n"
    "    g.NodeList.prototype.forEach = function(cb, thisArg) { return Array.prototype.forEach.call(this, cb, thisArg); };\n"
    "    g.NodeList.prototype.entries = function() { return Array.prototype.entries.call(this); };\n"
    "    g.NodeList.prototype.keys = function() { return Array.prototype.keys.call(this); };\n"
    "    g.NodeList.prototype.values = function() { return Array.prototype.values.call(this); };\n"
    "    g.NodeList.prototype[Symbol.iterator] = Array.prototype[Symbol.iterator];\n"
    "  }\n"
    "  if (!g.HTMLCollection) {\n"
    "    g.HTMLCollection = class HTMLCollection extends Array {};\n"
    "    g.HTMLCollection.prototype.item = function(index) { index = Number(index); return Number.isInteger(index) && index >= 0 && index < this.length ? this[index] : null; };\n"
    "    g.HTMLCollection.prototype.namedItem = function(name) { name = String(name); for (var i = 0; i < this.length; i++) { var el = this[i]; if (el && (el.id === name || el.getAttribute && el.getAttribute('name') === name)) return el; } return null; };\n"
    "    g.HTMLCollection.prototype.forEach = function(cb, thisArg) { return Array.prototype.forEach.call(this, cb, thisArg); };\n"
    "    g.HTMLCollection.prototype.entries = function() { return Array.prototype.entries.call(this); };\n"
    "    g.HTMLCollection.prototype.keys = function() { return Array.prototype.keys.call(this); };\n"
    "    g.HTMLCollection.prototype.values = function() { return Array.prototype.values.call(this); };\n"
    "    g.HTMLCollection.prototype[Symbol.iterator] = Array.prototype[Symbol.iterator];\n"
    "  }\n"
    "  if (!g.HTMLFormControlsCollection) g.HTMLFormControlsCollection = g.HTMLCollection;\n"
    "  if (!g.NamedNodeMap) {\n"
    "    g.NamedNodeMap = class NamedNodeMap extends Array {};\n"
    "    g.NamedNodeMap.prototype.item = function(index) { index = Number(index); return Number.isInteger(index) && index >= 0 && index < this.length ? this[index] : null; };\n"
    "    g.NamedNodeMap.prototype.getNamedItem = function(name) { name = String(name).toLowerCase(); for (var i = 0; i < this.length; i++) if (String(this[i].name).toLowerCase() === name) return this[i]; return null; };\n"
    "    g.NamedNodeMap.prototype.getNamedItemNS = function(ns, name) { return this.getNamedItem(name); };\n"
    "  }\n"
    "  function __liwusAttributeMap(el) {\n"
    "    var out = new g.NamedNodeMap(), names = el && el.getAttributeNames ? el.getAttributeNames() : [];\n"
    "    for (var i = 0; i < names.length; i++) { var name = String(names[i]), value = el.getAttribute(name); out.push({ name: name, nodeName: name, localName: name, value: value == null ? '' : String(value), nodeValue: value == null ? '' : String(value), namespaceURI: null, ownerElement: el }); }\n"
    "    out.setNamedItem = function(attr) { if (!attr || !attr.name) throw new TypeError('attribute node is required'); var old = out.getNamedItem(attr.name); el.setAttribute(attr.name, attr.value == null ? '' : attr.value); return old; };\n"
    "    out.setNamedItemNS = out.setNamedItem;\n"
    "    out.removeNamedItem = function(name) { var old = out.getNamedItem(name); if (!old) throw new DOMException('Attribute not found', 'NotFoundError'); el.removeAttribute(name); return old; };\n"
    "    out.removeNamedItemNS = out.removeNamedItem;\n"
    "    return out;\n"
    "  }\n"
    "  function __liwusCollection(value, ctor) {\n"
    "    if (!Array.isArray(value)) return value;\n"
    "    try { Object.setPrototypeOf(value, ctor.prototype); } catch (e) {}\n"
    "    return value;\n"
    "  }\n"
    "  function __liwusWrapCollectionMethod(obj, name, ctor, original) {\n"
    "    var wrapped = function() { return __liwusCollection(original.apply(this, arguments), ctor); };\n"
    "    wrapped.__liwusCollectionWrapped = true;\n"
    "    obj[name] = wrapped;\n"
    "  }\n"
    "  function __liwusWrapCollectionMethods(obj, names, ctor) {\n"
    "    if (!obj) return;\n"
    "    for (var i = 0; i < names.length; i++) {\n"
    "      var name = names[i], original = obj[name];\n"
    "      if (typeof original !== 'function' || original.__liwusCollectionWrapped) continue;\n"
    "      __liwusWrapCollectionMethod(obj, name, ctor, original);\n"
    "    }\n"
    "  }\n"
    "  if (typeof document !== 'undefined') {\n"
    "    __liwusWrapCollectionMethods(document, ['querySelectorAll'], g.NodeList);\n"
    "    __liwusWrapCollectionMethods(document, ['getElementsByTagName', 'getElementsByClassName'], g.HTMLCollection);\n"
    "  }\n"
    "  __liwusWrapCollectionMethods(proto, ['querySelectorAll'], g.NodeList);\n"
    "  __liwusWrapCollectionMethods(proto, ['getElementsByTagName', 'getElementsByClassName'], g.HTMLCollection);\n"
    "\n"
    "  if (typeof document !== 'undefined') {\n"
    "    document.defaultView = g;\n"
    "    if (!document.readyState) document.readyState = 'complete';\n"
    "    if (!document.compatMode) document.compatMode = 'CSS1Compat';\n"
    "    if (!document.doctype) document.doctype = { name: 'html' };\n"
    "    if (!document.scripts) {\n"
    "      Object.defineProperty(document, 'scripts', {\n"
    "        get: function() { return document.getElementsByTagName('script'); },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "    if (!document.forms) {\n"
    "      Object.defineProperty(document, 'forms', {\n"
    "        get: function() { return document.getElementsByTagName('form'); },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "    if (!document.images) {\n"
    "      Object.defineProperty(document, 'images', {\n"
    "        get: function() { return document.getElementsByTagName('img'); },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "    if (!document.links) {\n"
    "      Object.defineProperty(document, 'links', {\n"
    "        get: function() { return document.getElementsByTagName('a'); },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "    if (!document.getElementsByName) {\n"
    "      document.getElementsByName = function(name) {\n"
    "        return Array.from(document.querySelectorAll('[name=\"' + name + '\"]'));\n"
    "      };\n"
    "    }\n"
    "    if (!document.createRange) {\n"
    "      document.createRange = function() {\n"
    "        return {\n"
    "          setStart: function() {},\n"
    "          setEnd: function() {},\n"
    "          collapse: function() {},\n"
    "          selectNode: function() {},\n"
    "          selectNodeContents: function() {},\n"
    "          createContextualFragment: function(html) {\n"
    "            var f = document.createDocumentFragment();\n"
    "            var d = document.createElement('div');\n"
    "            d.innerHTML = html;\n"
    "            while (d.firstChild) f.appendChild(d.firstChild);\n"
    "            return f;\n"
    "          }\n"
    "        };\n"
    "      };\n"
    "    }\n"
    "    if (!document.createElementNS) {\n"
    "      document.createElementNS = function(ns, tag) {\n"
    "        ns = ns == null ? null : String(ns); tag = String(tag);\n"
    "        var el = document.createElement(tag), lower = tag.toLowerCase();\n"
    "        if (el) {\n"
    "          el.namespaceURI = ns; el.localName = lower; el.prefix = null;\n"
    "          if (ns === 'http://www.w3.org/2000/svg') {\n"
    "            var svgCtor = lower === 'svg' ? g.SVGSVGElement : g.SVGElement;\n"
    "            if (svgCtor && svgCtor.prototype) { try { Object.setPrototypeOf(el, svgCtor.prototype); } catch (e) {} }\n"
    "          }\n"
    "        }\n"
    "        return el;\n"
    "      };\n"
    "    }\n"
    "    if (document.createElement) {\n"
    "      var origCreateEl = document.createElement;\n"
    "      document.createElement = function(tag) {\n"
    "        var el = origCreateEl.call(document, tag);\n"
    "        if (el && el.namespaceURI === undefined) { el.namespaceURI = 'http://www.w3.org/1999/xhtml'; el.localName = String(tag).toLowerCase(); el.prefix = null; }\n"
    "        if (el && g.customElements) g.customElements.upgrade(el);\n"
    "        return el;\n"
    "      };\n"
    "    }\n"
    "    if (!document.createEvent) {\n"
    "      document.createEvent = function(type) {\n"
    "        var e = new Event(type || 'event', { bubbles: true, cancelable: true });\n"
    "        e.initEvent = function(t, b, c) {\n"
    "          e.type = t; e.bubbles = Boolean(b); e.cancelable = Boolean(c);\n"
    "        };\n"
    "        return e;\n"
    "      };\n"
    "    }\n"
    "    if (!document.importNode) {\n"
    "      document.importNode = function(node, deep) {\n"
    "        return node && node.cloneNode ? node.cloneNode(deep !== false) : null;\n"
    "      };\n"
    "    }\n"
    "    if (!document.adoptNode) {\n"
    "      document.adoptNode = function(node) {\n"
    "        if (node && node.parentNode) node.parentNode.removeChild(node);\n"
    "        return node;\n"
    "      };\n"
    "    }\n"
    "    if (!document.adoptedStyleSheets) document.adoptedStyleSheets = [];\n"
    "    if (proto && !proto.attachShadow) {\n"
    "      proto.attachShadow = function(init) {\n"
    "        if (this.shadowRoot) throw new DOMException('Shadow root already attached', 'NotSupportedError');\n"
    "        init = init || {};\n"
    "        var root = document.createDocumentFragment();\n"
    "        root.host = this; root.mode = init.mode === 'closed' ? 'closed' : 'open';\n"
    "        root.delegatesFocus = Boolean(init.delegatesFocus);\n"
    "        root.adoptedStyleSheets = [];\n"
    "        root.constructor = g.ShadowRoot;\n"
    "        Object.defineProperty(this, 'shadowRoot', { value: root.mode === 'open' ? root : null, writable: false, configurable: true });\n"
    "        return root;\n"
    "      };\n"
    "    }\n"
    "    if (proto && !Object.getOwnPropertyDescriptor(proto, 'content')) {\n"
    "      Object.defineProperty(proto, 'content', {\n"
    "        get: function() {\n"
    "          if (String(this.tagName || '').toLowerCase() !== 'template') return undefined;\n"
    "          if (!this.__liwusTemplateContent) {\n"
    "            var f = document.createDocumentFragment(), nodes = this.childNodes || [];\n"
    "            for (var i = 0; i < nodes.length; i++) f.appendChild(nodes[i].cloneNode ? nodes[i].cloneNode(true) : nodes[i]);\n"
    "            this.__liwusTemplateContent = f;\n"
    "          }\n"
    "          return this.__liwusTemplateContent;\n"
    "        }, configurable: true\n"
    "      });\n"
    "    }\n"
    "    var __liwusActiveElement = document.body || document.documentElement;\n"
    "    document.visibilityState = 'visible';\n"
    "    document.hidden = false;\n"
    "    if (!document.activeElement) {\n"
    "      Object.defineProperty(document, 'activeElement', {\n"
    "        get: function() { return __liwusActiveElement || document.body || document.documentElement; },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "    if (!document.hasFocus) document.hasFocus = function() { return true; };\n"
    "    if (!document.elementFromPoint) document.elementFromPoint = function() { return document.body; };\n"
    "    if (!document.elementsFromPoint) document.elementsFromPoint = function() { return [document.body]; };\n"
    "    var _cookies = {};\n"
    "    Object.defineProperty(document, 'cookie', {\n"
    "      get: function() {\n"
    "        var res = [];\n"
    "        for (var k in _cookies) res.push(k + '=' + _cookies[k]);\n"
    "        return res.join('; ');\n"
    "      },\n"
    "      set: function(val) {\n"
    "        if (typeof val !== 'string') return;\n"
    "        var pair = val.split(';')[0];\n"
    "        var eq = pair.indexOf('=');\n"
    "        if (eq > 0) {\n"
    "          var k = pair.substring(0, eq).trim();\n"
    "          var v = pair.substring(eq + 1).trim();\n"
    "          _cookies[k] = v;\n"
    "        }\n"
    "      },\n"
    "      configurable: true\n"
    "    });\n"
    "    if (!g.FontFace) {\n"
    "      g.FontFace = class FontFace {\n"
    "        constructor(family, source, descriptors) { this.family = String(family || ''); this.source = source == null ? '' : String(source); this.status = 'unloaded'; this.style = (descriptors && descriptors.style) || 'normal'; this.weight = (descriptors && descriptors.weight) || 'normal'; this.stretch = (descriptors && descriptors.stretch) || 'normal'; this.unicodeRange = (descriptors && descriptors.unicodeRange) || ''; }\n"
    "        load() { this.status = 'loaded'; return Promise.resolve(this); }\n"
    "      };\n"
    "    }\n"
    "    if (!document.fonts) {\n"
    "      var __liwusFonts = [];\n"
    "      document.fonts = { status: 'loaded', ready: Promise.resolve(), onloading: null, onloadingdone: null, onloadingerror: null,\n"
    "        check: function() { return true; },\n"
    "        load: function() { return Promise.resolve(__liwusFonts.slice()); },\n"
    "        add: function(face) { if (face && __liwusFonts.indexOf(face) < 0) __liwusFonts.push(face); return this; },\n"
    "        delete: function(face) { var i = __liwusFonts.indexOf(face); if (i >= 0) __liwusFonts.splice(i, 1); return i >= 0; },\n"
    "        has: function(face) { return __liwusFonts.indexOf(face) >= 0; },\n"
    "        clear: function() { __liwusFonts.length = 0; },\n"
    "        forEach: function(cb, self) { __liwusFonts.slice().forEach(function(face) { cb.call(self, face, face, document.fonts); }); },\n"
    "        addEventListener: function() {}, removeEventListener: function() {}\n"
    "      };\n"
    "    }\n"
    "  }\n"
    "  if (!g.Image) g.Image = function(width, height) { var image = document.createElement('img'); if (width !== undefined) image.width = width; if (height !== undefined) image.height = height; return image; };\n"
    "  if (!g.Audio) g.Audio = function(src) { var audio = document.createElement('audio'); if (src !== undefined) audio.src = src; audio.play = function() { return Promise.resolve(); }; audio.pause = function() {}; return audio; };\n"
    "\n"
    "  class HTMLElement {\n"
    "    constructor() {\n"
    "      var tag = 'div';\n"
    "      if (new.target && g.customElements) {\n"
    "        for (var pair of g.customElements._registry.entries()) {\n"
    "          if (pair[1].ctor === new.target) { tag = pair[0]; break; }\n"
    "        }\n"
    "      }\n"
    "      var el = document.createElement(tag);\n"
    "      Object.setPrototypeOf(el, new.target ? new.target.prototype : HTMLElement.prototype);\n"
    "      return el;\n"
    "    }\n"
    "  }\n"
    "  if (proto) Object.setPrototypeOf(HTMLElement.prototype, proto);\n"
    "  g.HTMLElement = HTMLElement;\n"
    "  function makeCtor(name) {\n"
    "    var ctor = class extends HTMLElement {};\n"
    "    Object.defineProperty(ctor, 'name', { value: name });\n"
    "    return ctor;\n"
    "  }\n"
    "  var elemTags = [\n"
    "    'HTMLUnknownElement', 'HTMLParagraphElement', 'HTMLHeadingElement',\n"
    "    'HTMLUListElement', 'HTMLOListElement', 'HTMLLIElement', 'HTMLTableElement',\n"
    "    'HTMLTableSectionElement', 'HTMLTableRowElement', 'HTMLTableCellElement',\n"
    "    'HTMLFormElement', 'HTMLSelectElement', 'HTMLOptionElement', 'HTMLTextAreaElement',\n"
    "    'HTMLLabelElement', 'HTMLTemplateElement', 'HTMLSlotElement', 'HTMLDetailsElement',\n"
    "    'HTMLDialogElement', 'SVGSVGElement', 'SVGElement', 'HTMLMetaElement', 'HTMLLinkElement',\n"
    "    'HTMLTitleElement', 'HTMLBaseElement', 'HTMLBodyElement', 'HTMLHtmlElement',\n"
    "    'HTMLDivElement', 'HTMLSpanElement', 'HTMLInputElement', 'HTMLButtonElement',\n"
    "    'HTMLAnchorElement', 'HTMLImageElement', 'HTMLScriptElement', 'HTMLStyleElement',\n"
    "    'Node', 'Element', 'DocumentFragment'\n"
    "  ];\n"
    "  for (var i = 0; i < elemTags.length; i++) {\n"
    "    g[elemTags[i]] = makeCtor(elemTags[i]);\n"
    "  }\n"
    "  Node.ELEMENT_NODE = 1; Node.ATTRIBUTE_NODE = 2; Node.TEXT_NODE = 3; Node.COMMENT_NODE = 8; Node.DOCUMENT_NODE = 9; Node.DOCUMENT_FRAGMENT_NODE = 11;\n"
    "\n"
    "  if (proto) {\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'style')) {\n"
    "      Object.defineProperty(proto, 'style', {\n"
    "        get: function() {\n"
    "          if (!this._sp) {\n"
    "            var el = this;\n"
    "            function parseS() {\n"
    "              var s = el.getAttribute('style') || '';\n"
    "              var m = {}, parts = s.split(';');\n"
    "              for (var i = 0; i < parts.length; i++) {\n"
    "                var p = parts[i].trim();\n"
    "                if (!p) continue;\n"
    "                var c = p.indexOf(':');\n"
    "                if (c > 0) m[p.substring(0, c).trim().toLowerCase()] = p.substring(c + 1).trim();\n"
    "              }\n"
    "              return m;\n"
    "            }\n"
    "            function serS(m) {\n"
    "              var r = [];\n"
    "              for (var k in m) if (m[k] != null && m[k] !== '') r.push(k + ': ' + m[k]);\n"
    "              el.setAttribute('style', r.join('; '));\n"
    "            }\n"
    "            function toKebab(s) {\n"
    "              var o = '';\n"
    "              for (var j = 0; j < s.length; j++) {\n"
    "                var ch = s.charAt(j);\n"
    "                if (ch >= 'A' && ch <= 'Z') o += '-' + ch.toLowerCase();\n"
    "                else o += ch;\n"
    "              }\n"
    "              return o;\n"
    "            }\n"
    "            function propName(n) { return toKebab(String(n)).toLowerCase(); }\n"
    "            var target = {\n"
    "              getPropertyValue: function(n) { return parseS()[propName(n)] || ''; },\n"
    "              getPropertyPriority: function(n) { var v = parseS()[propName(n)] || ''; return /\\s*!important\\s*$/i.test(v) ? 'important' : ''; },\n"
    "              setProperty: function(n, v, priority) { var m = parseS(), value = String(v); if (String(priority || '').toLowerCase() === 'important' && value && !/\\s*!important\\s*$/i.test(value)) value += ' !important'; m[propName(n)] = value; serS(m); },\n"
    "              removeProperty: function(n) { var m = parseS(), key = propName(n), prev = m[key] || ''; delete m[key]; serS(m); return prev; },\n"
    "              item: function(index) { var keys = Object.keys(parseS()), i = Number(index); return Number.isInteger(i) && i >= 0 && i < keys.length ? keys[i] : ''; }\n"
    "            };\n"
    "            Object.defineProperty(target, 'length', { get: function() { return Object.keys(parseS()).length; } });\n"
    "            Object.defineProperty(target, 'cssText', {\n"
    "              get: function() { return el.getAttribute('style') || ''; },\n"
    "              set: function(v) { el.setAttribute('style', String(v)); }\n"
    "            });\n"
    "            this._sp = new Proxy(target, {\n"
    "              get: function(t, p) {\n"
    "                if (p in t || typeof p === 'symbol') return t[p];\n"
    "                if (typeof p === 'string') return t.getPropertyValue(toKebab(p));\n"
    "                return undefined;\n"
    "              },\n"
    "              set: function(t, p, v) {\n"
    "                if (p === 'cssText') { t.cssText = v; return true; }\n"
    "                if (typeof p === 'string') { t.setProperty(toKebab(p), v); return true; }\n"
    "                return false;\n"
    "              }\n"
    "            });\n"
    "          }\n"
    "          return this._sp;\n"
    "        },\n"
    "        configurable: true,\n"
    "        enumerable: true\n"
    "      });\n"
    "    }\n"
    "\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'dataset')) {\n"
    "      Object.defineProperty(proto, 'dataset', {\n"
    "        get: function() {\n"
    "          if (!this._dp) {\n"
    "            var el = this;\n"
    "            function toDataKey(s) {\n"
    "              var o = 'data-';\n"
    "              for (var j = 0; j < s.length; j++) {\n"
    "                var ch = s.charAt(j);\n"
    "                if (ch >= 'A' && ch <= 'Z') o += '-' + ch.toLowerCase();\n"
    "                else o += ch;\n"
    "              }\n"
    "              return o;\n"
    "            }\n"
    "            this._dp = new Proxy({}, {\n"
    "              get: function(t, p) {\n"
    "                if (typeof p === 'string') return el.getAttribute(toDataKey(p)) || undefined;\n"
    "                return undefined;\n"
    "              },\n"
    "              set: function(t, p, v) {\n"
    "                if (typeof p === 'string') { el.setAttribute(toDataKey(p), String(v)); return true; }\n"
    "                return false;\n"
    "              },\n"
    "              deleteProperty: function(t, p) {\n"
    "                if (typeof p === 'string') { el.removeAttribute(toDataKey(p)); return true; }\n"
    "                return false;\n"
    "              }\n"
    "            });\n"
    "          }\n"
    "          return this._dp;\n"
    "        },\n"
    "        configurable: true,\n"
    "        enumerable: true\n"
    "      });\n"
    "    }\n"
    "\n"
    "    /* High-use DOM traversal and mutation APIs used by modern component code. */\n"
    "    if (!proto.matches) {\n"
    "      proto.matches = function(selector) {\n"
    "        selector = String(selector);\n"
    "        var root = this.parentNode || document;\n"
    "        if (!root || !root.querySelectorAll) return false;\n"
    "        var list = root.querySelectorAll(selector);\n"
    "        for (var i = 0; i < list.length; i++) if (list[i] === this) return true;\n"
    "        return false;\n"
    "      };\n"
    "    }\n"
    "    if (!proto.closest) {\n"
    "      proto.closest = function(selector) {\n"
    "        var node = this;\n"
    "        while (node) {\n"
    "          if (node.matches && node.matches(selector)) return node;\n"
    "          node = node.parentElement || node.parentNode;\n"
    "        }\n"
    "        return null;\n"
    "      };\n"
    "    }\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'parentElement')) {\n"
    "      Object.defineProperty(proto, 'parentElement', { get: function() { var p = this.parentNode; return p && p.nodeType === 1 ? p : null; }, configurable: true });\n"
    "    }\n"
    "    var __liwusChildrenDesc = Object.getOwnPropertyDescriptor(proto, 'children');\n"
    "    if (__liwusChildrenDesc && __liwusChildrenDesc.get && !__liwusChildrenDesc.get.__liwusCollectionWrapped) {\n"
    "      var __liwusChildrenGet = __liwusChildrenDesc.get;\n"
    "      var __liwusWrappedChildrenGet = function() { return __liwusCollection(__liwusChildrenGet.call(this), g.HTMLCollection); };\n"
    "      __liwusWrappedChildrenGet.__liwusCollectionWrapped = true;\n"
    "      Object.defineProperty(proto, 'children', { get: __liwusWrappedChildrenGet, configurable: true });\n"
    "    }\n"
    "    var __liwusChildNodesDesc = Object.getOwnPropertyDescriptor(proto, 'childNodes');\n"
    "    if (__liwusChildNodesDesc && __liwusChildNodesDesc.get && !__liwusChildNodesDesc.get.__liwusCollectionWrapped) {\n"
    "      var __liwusChildNodesGet = __liwusChildNodesDesc.get;\n"
    "      var __liwusWrappedChildNodesGet = function() { return __liwusCollection(__liwusChildNodesGet.call(this), g.NodeList); };\n"
    "      __liwusWrappedChildNodesGet.__liwusCollectionWrapped = true;\n"
    "      Object.defineProperty(proto, 'childNodes', { get: __liwusWrappedChildNodesGet, configurable: true });\n"
    "    }\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'children')) {\n"
    "      Object.defineProperty(proto, 'children', {\n"
    "        get: function() { var out = [], nodes = this.childNodes || []; for (var i = 0; i < nodes.length; i++) if (nodes[i] && nodes[i].nodeType === 1) out.push(nodes[i]); return out; },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'childElementCount')) Object.defineProperty(proto, 'childElementCount', { get: function() { return this.children.length; }, configurable: true });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'firstElementChild')) Object.defineProperty(proto, 'firstElementChild', { get: function() { var c = this.children; return c.length ? c[0] : null; }, configurable: true });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'lastElementChild')) Object.defineProperty(proto, 'lastElementChild', { get: function() { var c = this.children; return c.length ? c[c.length - 1] : null; }, configurable: true });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'nextElementSibling')) Object.defineProperty(proto, 'nextElementSibling', { get: function() { var n = this.nextSibling; while (n && n.nodeType !== 1) n = n.nextSibling; return n || null; }, configurable: true });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'previousElementSibling')) Object.defineProperty(proto, 'previousElementSibling', { get: function() { var n = this.previousSibling; while (n && n.nodeType !== 1) n = n.previousSibling; return n || null; }, configurable: true });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'className')) Object.defineProperty(proto, 'className', { get: function() { return this.getAttribute('class') || ''; }, set: function(v) { this.setAttribute('class', String(v)); }, configurable: true });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'attributes')) Object.defineProperty(proto, 'attributes', { get: function() { return __liwusAttributeMap(this); }, configurable: true });\n"
    "    if (!proto.getAttributeNode) proto.getAttributeNode = function(name) { var attrs = this.attributes; return attrs && attrs.getNamedItem ? attrs.getNamedItem(name) : null; };\n"
    "    if (!proto.getAttributeNodeNS) proto.getAttributeNodeNS = function(ns, name) { return this.getAttributeNode(name); };\n"
    "    if (!proto.setAttributeNode) proto.setAttributeNode = function(attr) { if (!attr || !attr.name) throw new TypeError('attribute node is required'); var old = this.getAttributeNode(attr.name); this.setAttribute(attr.name, attr.value == null ? '' : attr.value); return old; };\n"
    "    if (!proto.setAttributeNodeNS) proto.setAttributeNodeNS = function(attr) { return this.setAttributeNode(attr); };\n"
    "    if (!proto.getAttributeNames) proto.getAttributeNames = function() { var names = [], attrs = this.attributes || []; for (var i = 0; i < attrs.length; i++) if (attrs[i] && attrs[i].name) names.push(String(attrs[i].name)); return names; };\n"
    "    if (!proto.toggleAttribute) proto.toggleAttribute = function(name, force) { name = String(name); var present = this.hasAttribute(name); var next = force === undefined ? !present : Boolean(force); if (next) this.setAttribute(name, ''); else this.removeAttribute(name); return next; };\n"
    "    if (!proto.replaceChildren) proto.replaceChildren = function() { while (this.firstChild) this.removeChild(this.firstChild); for (var i = 0; i < arguments.length; i++) { var child = arguments[i]; if (typeof child === 'string') child = document.createTextNode(child); if (child) this.appendChild(child); } };\n"
    "    if (!proto.remove) proto.remove = function() { if (this.parentNode) this.parentNode.removeChild(this); };\n"
    "    if (!proto.before) proto.before = function() { if (!this.parentNode) return; for (var i = 0; i < arguments.length; i++) { var n = arguments[i]; if (typeof n === 'string') n = document.createTextNode(n); if (n) this.parentNode.insertBefore(n, this); } };\n"
    "    if (!proto.after) proto.after = function() { if (!this.parentNode) return; var ref = this.nextSibling; for (var i = 0; i < arguments.length; i++) { var n = arguments[i]; if (typeof n === 'string') n = document.createTextNode(n); if (n) this.parentNode.insertBefore(n, ref); } };\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'innerText')) Object.defineProperty(proto, 'innerText', { get: function() { return this.textContent || ''; }, set: function(v) { this.textContent = String(v); }, configurable: true });\n"
    "    if (!proto.submit) proto.submit = function() { if (String(this.tagName || '').toUpperCase() !== 'FORM') return; this.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true })); };\n"
    "    if (!proto.requestSubmit) proto.requestSubmit = function(submitter) { if (submitter && submitter.form && submitter.form !== this) throw new DOMException('Not a form control', 'NotFoundError'); this.submit(); };\n"
    "    if (!proto.checkValidity) proto.checkValidity = function() { return true; };\n"
    "    if (!proto.reportValidity) proto.reportValidity = function() { return this.checkValidity(); };\n"
    "    if (!proto.animate) proto.animate = function(keyframes, options) {\n"
    "      var animation = { id: '', effect: { target: this, getKeyframes: function() { return keyframes || []; } }, playState: 'finished', currentTime: 0, pending: false, onfinish: null, oncancel: null,\n"
    "        play: function() { this.playState = 'running'; this.playState = 'finished'; if (this.onfinish) this.onfinish(new Event('finish')); },\n"
    "        pause: function() { this.playState = 'paused'; }, cancel: function() { this.playState = 'idle'; if (this.oncancel) this.oncancel(new Event('cancel')); },\n"
    "        finish: function() { this.playState = 'finished'; if (this.onfinish) this.onfinish(new Event('finish')); }, reverse: function() {}, commitStyles: function() {}, persist: function() {}, finished: Promise.resolve(), ready: Promise.resolve() };\n"
    "      if (options && options.autoplay === false) animation.playState = 'idle'; else animation.play(); return animation;\n"
    "    };\n"
    "\n"
    "    var origCLDesc = Object.getOwnPropertyDescriptor(proto, 'classList');\n"
    "    if (origCLDesc && origCLDesc.get) {\n"
    "      var origGetCL = origCLDesc.get;\n"
    "      Object.defineProperty(proto, 'classList', {\n"
    "        get: function() {\n"
    "          var cl = origGetCL.call(this);\n"
    "          if (cl && !cl._augmented) {\n"
    "            cl._augmented = true;\n"
    "            var el = this;\n"
    "            function getTokens() {\n"
    "              var s = el.getAttribute('class') || '';\n"
    "              return s.trim() ? s.trim().split(/\\s+/) : [];\n"
    "            }\n"
    "            cl.replace = function(oldC, newC) {\n"
    "              if (!cl.contains(oldC)) return false;\n"
    "              cl.remove(oldC); cl.add(newC); return true;\n"
    "            };\n"
    "            cl.item = function(i) {\n"
    "              var t = getTokens(); return (i >= 0 && i < t.length) ? t[i] : null;\n"
    "            };\n"
    "            cl.toString = function() { return el.getAttribute('class') || ''; };\n"
    "            Object.defineProperty(cl, 'length', {\n"
    "              get: function() { return getTokens().length; }, configurable: true\n"
    "            });\n"
    "            Object.defineProperty(cl, 'value', {\n"
    "              get: function() { return el.getAttribute('class') || ''; },\n"
    "              set: function(v) { el.setAttribute('class', String(v)); }, configurable: true\n"
    "            });\n"
    "            cl.forEach = function(cb) { getTokens().forEach(cb); };\n"
    "            cl[Symbol.iterator] = function*() {\n"
    "              var t = getTokens();\n"
    "              for (var j = 0; j < t.length; j++) yield t[j];\n"
    "            };\n"
    "          }\n"
    "          return cl;\n"
    "        },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "\n"
    "    var attrsMap = {\n"
    "      src: 'src', name: 'name', type: 'type', title: 'title',\n"
    "      alt: 'alt', placeholder: 'placeholder', target: 'target',\n"
    "      rel: 'rel', slot: 'slot', role: 'role'\n"
    "    };\n"
    "    Object.keys(attrsMap).forEach(function(prop) {\n"
    "      var attr = attrsMap[prop];\n"
    "      if (!Object.getOwnPropertyDescriptor(proto, prop)) {\n"
    "        Object.defineProperty(proto, prop, {\n"
    "          get: function() { return this.getAttribute(attr) || ''; },\n"
    "          set: function(v) { this.setAttribute(attr, String(v)); },\n"
    "          configurable: true\n"
    "        });\n"
    "      }\n"
    "    });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'form')) Object.defineProperty(proto, 'form', { get: function() { var node = this.parentNode; while (node) { if (String(node.tagName || '').toUpperCase() === 'FORM') return node; node = node.parentNode; } return null; }, configurable: true });\n"
    "\n"
    "    var boolAttrs = ['disabled', 'checked', 'hidden', 'required', 'readOnly', 'multiple', 'selected', 'open'];\n"
    "    boolAttrs.forEach(function(prop) {\n"
    "      var attr = prop.toLowerCase();\n"
    "      if (!Object.getOwnPropertyDescriptor(proto, prop)) {\n"
    "        Object.defineProperty(proto, prop, {\n"
    "          get: function() { return this.hasAttribute(attr); },\n"
    "          set: function(v) {\n"
    "            if (v) this.setAttribute(attr, '');\n"
    "            else this.removeAttribute(attr);\n"
    "          },\n"
    "          configurable: true\n"
    "        });\n"
    "      }\n"
    "    });\n"
    "\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'tabIndex')) {\n"
    "      Object.defineProperty(proto, 'tabIndex', {\n"
    "        get: function() { var v = this.getAttribute('tabindex'); return v ? parseInt(v, 10) : 0; },\n"
    "        set: function(v) { this.setAttribute('tabindex', String(v)); },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "\n"
    "    if (!proto.attachShadow) {\n"
    "      proto.attachShadow = function(init) {\n"
    "        var sr = document.createDocumentFragment();\n"
    "        sr.mode = (init && init.mode) || 'open';\n"
    "        sr.host = this;\n"
    "        this._shadowRoot = sr;\n"
    "        return sr;\n"
    "      };\n"
    "    }\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'shadowRoot')) {\n"
    "      Object.defineProperty(proto, 'shadowRoot', {\n"
    "        get: function() { return this._shadowRoot || null; },\n"
    "        set: function(v) { this._shadowRoot = v; },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'content')) {\n"
    "      Object.defineProperty(proto, 'content', {\n"
    "        get: function() {\n"
    "          if (this.tagName === 'TEMPLATE') {\n"
    "            if (!this._frag) {\n"
    "              var f = document.createDocumentFragment();\n"
    "              while (this.firstChild) f.appendChild(this.firstChild);\n"
    "              this._frag = f;\n"
    "            }\n"
    "            return this._frag;\n"
    "          }\n"
    "          return undefined;\n"
    "        },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "    if (!proto.showModal) proto.showModal = function() { this.open = true; };\n"
    "    if (!proto.close) proto.close = function() { this.open = false; };\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'elements')) {\n"
    "      Object.defineProperty(proto, 'elements', {\n"
    "        get: function() {\n"
    "          if (this.tagName === 'FORM') {\n"
    "            return __liwusCollection(this.querySelectorAll('input, button, select, textarea'), g.HTMLFormControlsCollection);\n"
    "          }\n"
    "          return undefined;\n"
    "        },\n"
    "        configurable: true\n"
    "      });\n"
    "    }\n"
    "\n"
    "    if (!proto.getBoundingClientRect) {\n"
    "      proto.getBoundingClientRect = function() {\n"
    "        return new g.DOMRect(0, 0, 800, 600);\n"
    "      };\n"
    "    }\n"
    "    if (!proto.getClientRects) {\n"
    "      proto.getClientRects = function() { return [this.getBoundingClientRect()]; };\n"
    "    }\n"
    "    if (!proto.scrollIntoView) { proto.scrollIntoView = function() {}; }\n"
    "    if (!proto.focus) { proto.focus = function() { __liwusActiveElement = this; if (this.dispatchEvent) this.dispatchEvent(new Event('focus', { bubbles: false })); }; }\n"
    "    if (!proto.blur) { proto.blur = function() { if (__liwusActiveElement === this) __liwusActiveElement = document.body || document.documentElement; if (this.dispatchEvent) this.dispatchEvent(new Event('blur', { bubbles: false })); }; }\n"
    "    if (!proto.click) { proto.click = function() { if (this.dispatchEvent) this.dispatchEvent(new Event('click', { bubbles: true, cancelable: true })); }; }\n"
    "    if (!proto.play) { proto.play = function() { this.paused = false; if (this.dispatchEvent) this.dispatchEvent(new Event('play')); return Promise.resolve(); }; }\n"
    "    if (!proto.pause) { proto.pause = function() { this.paused = true; if (this.dispatchEvent) this.dispatchEvent(new Event('pause')); }; }\n"
    "    if (!proto.load) { proto.load = function() { this.readyState = 1; if (this.dispatchEvent) this.dispatchEvent(new Event('loadstart')); }; }\n"
    "    if (!proto.canPlayType) { proto.canPlayType = function(type) { type = String(type || '').toLowerCase(); return /^(audio\\/|video\\/|application\\/ogg)/.test(type) ? 'maybe' : ''; }; }\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'paused')) Object.defineProperty(proto, 'paused', { get: function() { return this._liwusPaused !== false; }, set: function(v) { this._liwusPaused = Boolean(v); }, configurable: true });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'currentTime')) Object.defineProperty(proto, 'currentTime', { get: function() { return Number(this._liwusCurrentTime || 0); }, set: function(v) { this._liwusCurrentTime = Number(v) || 0; }, configurable: true });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'duration')) Object.defineProperty(proto, 'duration', { get: function() { return Number(this._liwusDuration || 0); }, configurable: true });\n"
    "    if (!Object.getOwnPropertyDescriptor(proto, 'readyState')) Object.defineProperty(proto, 'readyState', { get: function() { return Number(this._liwusReadyState || 0); }, set: function(v) { this._liwusReadyState = Number(v) || 0; }, configurable: true });\n"
    "    function __liwusInstallHandler(obj, property, type) {\n"
    "      if (!obj || Object.getOwnPropertyDescriptor(obj, property)) return;\n"
    "      var slot = '__liwus_' + property;\n"
    "      Object.defineProperty(obj, property, { configurable: true, get: function() { return this[slot] || null; }, set: function(fn) { var old = this[slot]; if (old && this.removeEventListener) this.removeEventListener(type, old); this[slot] = typeof fn === 'function' ? fn : null; if (this[slot] && this.addEventListener) this.addEventListener(type, this[slot]); } });\n"
    "    }\n"
    "    var __liwusHandlerNames = ['onclick','oninput','onchange','onsubmit','onload','onerror','onfocus','onblur','onkeydown','onkeyup','onkeypress','onpointerdown','onpointerup','oncopy','onpaste'];\n"
    "    for (var __hi = 0; __hi < __liwusHandlerNames.length; __hi++) __liwusInstallHandler(proto, __liwusHandlerNames[__hi], __liwusHandlerNames[__hi].substring(2));\n"
    "    if (!proto.insertAdjacentHTML) {\n"
    "      proto.insertAdjacentHTML = function(pos, html) {\n"
    "        var d = document.createElement('div');\n"
    "        d.innerHTML = html;\n"
    "        var nodes = Array.from(d.childNodes);\n"
    "        pos = String(pos).toLowerCase();\n"
    "        if (pos === 'beforebegin' && this.parentNode) {\n"
    "          for (var i = 0; i < nodes.length; i++) this.parentNode.insertBefore(nodes[i], this);\n"
    "        } else if (pos === 'afterbegin') {\n"
    "          for (var i = nodes.length - 1; i >= 0; i--) this.insertBefore(nodes[i], this.firstChild);\n"
    "        } else if (pos === 'beforeend') {\n"
    "          for (var i = 0; i < nodes.length; i++) this.appendChild(nodes[i]);\n"
    "        } else if (pos === 'afterend' && this.parentNode) {\n"
    "          var nxt = this.nextSibling;\n"
    "          for (var i = 0; i < nodes.length; i++) this.parentNode.insertBefore(nodes[i], nxt);\n"
    "        }\n"
    "      };\n"
    "    }\n"
    "    if (!proto.insertAdjacentElement) {\n"
    "      proto.insertAdjacentElement = function(pos, el) {\n"
    "        pos = String(pos).toLowerCase();\n"
    "        if (pos === 'beforebegin' && this.parentNode) this.parentNode.insertBefore(el, this);\n"
    "        else if (pos === 'afterbegin') this.insertBefore(el, this.firstChild);\n"
    "        else if (pos === 'beforeend') this.appendChild(el);\n"
    "        else if (pos === 'afterend' && this.parentNode) this.parentNode.insertBefore(el, this.nextSibling);\n"
    "        return el;\n"
    "      };\n"
    "    }\n"
    "    if (!proto.append) {\n"
    "      proto.append = function() {\n"
    "        for (var i = 0; i < arguments.length; i++) {\n"
    "          var a = arguments[i];\n"
    "          if (typeof a === 'string') a = document.createTextNode(a);\n"
    "          this.appendChild(a);\n"
    "        }\n"
    "      };\n"
    "    }\n"
    "    if (!proto.prepend) {\n"
    "      proto.prepend = function() {\n"
    "        for (var i = arguments.length - 1; i >= 0; i--) {\n"
    "          var a = arguments[i];\n"
    "          if (typeof a === 'string') a = document.createTextNode(a);\n"
    "          this.insertBefore(a, this.firstChild);\n"
    "        }\n"
    "      };\n"
    "    }\n"
    "    if (!proto.__liwusMutationWrapped) {\n"
    "      proto.__liwusMutationWrapped = true;\n"
    "      var __liwusAppendChild = proto.appendChild;\n"
    "      var __liwusInsertBefore = proto.insertBefore;\n"
    "      var __liwusRemoveChild = proto.removeChild;\n"
    "      var __liwusReplaceChild = proto.replaceChild;\n"
    "      var __liwusSetAttribute = proto.setAttribute;\n"
    "      var __liwusRemoveAttribute = proto.removeAttribute;\n"
    "      proto.appendChild = function(child) { var r = __liwusAppendChild.call(this, child); __liwusMutationNotify(this, { type: 'childList', target: this, addedNodes: [child], removedNodes: [], previousSibling: null, nextSibling: null }); return r; };\n"
    "      proto.insertBefore = function(child, ref) { var r = __liwusInsertBefore.call(this, child, ref); __liwusMutationNotify(this, { type: 'childList', target: this, addedNodes: [child], removedNodes: [], previousSibling: null, nextSibling: ref || null }); return r; };\n"
    "      proto.removeChild = function(child) { var r = __liwusRemoveChild.call(this, child); __liwusMutationNotify(this, { type: 'childList', target: this, addedNodes: [], removedNodes: [child], previousSibling: null, nextSibling: null }); return r; };\n"
    "      proto.replaceChild = function(child, old) { var r = __liwusReplaceChild.call(this, child, old); __liwusMutationNotify(this, { type: 'childList', target: this, addedNodes: [child], removedNodes: [old], previousSibling: null, nextSibling: null }); return r; };\n"
    "      proto.setAttribute = function(name, value) { var old = this.getAttribute(name); var r = __liwusSetAttribute.call(this, name, value); __liwusMutationNotify(this, { type: 'attributes', target: this, attributeName: String(name), oldValue: old }); return r; };\n"
    "      proto.removeAttribute = function(name) { var old = this.getAttribute(name); var r = __liwusRemoveAttribute.call(this, name); __liwusMutationNotify(this, { type: 'attributes', target: this, attributeName: String(name), oldValue: old }); return r; };\n"
    "      var __liwusTextDesc = Object.getOwnPropertyDescriptor(proto, 'textContent');\n"
    "      if (__liwusTextDesc && __liwusTextDesc.set && !proto.__liwusTextWrapped) {\n"
    "        proto.__liwusTextWrapped = true;\n"
    "        Object.defineProperty(proto, 'textContent', { configurable: true, get: __liwusTextDesc.get, set: function(value) { var old = __liwusTextDesc.get ? __liwusTextDesc.get.call(this) : ''; __liwusTextDesc.set.call(this, value); __liwusMutationNotify(this, { type: 'characterData', target: this, oldValue: old }); } });\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "\n"
    "  g.screen = { width: 1280, height: 1024, availWidth: 1280, availHeight: 1024, colorDepth: 24, pixelDepth: 24, orientation: { type: 'landscape-primary', angle: 0 } };\n"
    "  g.innerWidth = 1280; g.innerHeight = 960; g.outerWidth = 1280; g.outerHeight = 1024;\n"
    "  g.devicePixelRatio = 1; g.scrollX = 0; g.scrollY = 0; g.pageXOffset = 0; g.pageYOffset = 0;\n"
    "  if (!g.visualViewport) { g.visualViewport = new EventTarget(); g.visualViewport.width = g.innerWidth; g.visualViewport.height = g.innerHeight; g.visualViewport.scale = 1; g.visualViewport.offsetLeft = 0; g.visualViewport.offsetTop = 0; g.visualViewport.pageLeft = 0; g.visualViewport.pageTop = 0; g.visualViewport.onresize = null; g.visualViewport.onscroll = null; }\n"
    "  g.scrollTo = function() {}; g.scrollBy = function() {}; g.scroll = function() {};\n"
    "  g.requestIdleCallback = function(cb, opts) { var id = setTimeout(function() { if (typeof cb === 'function') cb({ didTimeout: false, timeRemaining: function() { return 8; } }); }, (opts && opts.timeout) || 1); return id; };\n"
    "  g.cancelIdleCallback = function(id) { clearTimeout(id); };\n"
    "\n"
    "  g.history = (function() {\n"
    "    var entries = [{ state: null, url: (typeof location !== 'undefined' && location.href) || 'about:blank' }];\n"
    "    var index = 0;\n"
    "    function apply(entry, emit) {\n"
    "      if (typeof location !== 'undefined' && entry.url) {\n"
    "        try { var u = new URL(entry.url, location.href); location.href = u.href; location.pathname = u.pathname; location.search = u.search; location.hash = u.hash; } catch (e) { location.href = entry.url; }\n"
    "      }\n"
    "      api.state = entry.state;\n"
    "      if (emit && typeof g.dispatchEvent === 'function') g.dispatchEvent(new Event('popstate'));\n"
    "    }\n"
    "    var api = {\n"
    "      get length() { return entries.length; },\n"
    "      get state() { return entries[index].state; },\n"
    "      scrollRestoration: 'auto',\n"
    "      back: function() { return this.go(-1); },\n"
    "      forward: function() { return this.go(1); },\n"
    "      go: function(delta) { var next = index + (Number(delta) || 0); if (next < 0) next = 0; if (next >= entries.length) next = entries.length - 1; if (next !== index) { index = next; apply(entries[index], true); } },\n"
    "      pushState: function(st, title, url) {\n"
    "        var resolved = url == null ? ((typeof location !== 'undefined' && location.href) || entries[index].url) : String(url);\n"
    "        try { if (typeof location !== 'undefined') resolved = new URL(resolved, location.href).href; } catch (e) {}\n"
    "        entries = entries.slice(0, index + 1); entries.push({ state: st, url: resolved }); index++; apply(entries[index], false);\n"
    "      },\n"
    "      replaceState: function(st, title, url) {\n"
    "        var resolved = url == null ? entries[index].url : String(url);\n"
    "        try { if (typeof location !== 'undefined') resolved = new URL(resolved, location.href).href; } catch (e) {}\n"
    "        entries[index] = { state: st, url: resolved }; apply(entries[index], false);\n"
    "      }\n"
    "    };\n"
    "    return api;\n"
    "  })();\n"
    "\n"
    "  if (typeof location !== 'undefined') {\n"
    "    if (!location.assign) location.assign = function(u) { location.href = u; };\n"
    "    if (!location.replace) location.replace = function(u) { location.href = u; };\n"
    "    if (!location.reload) location.reload = function() {};\n"
    "    if (!location.toString) location.toString = function() { return location.href; };\n"
    "  }\n"
    "\n"
    "  if (typeof navigator !== 'undefined') {\n"
    "    var __liwusClipboardText = '';\n"
    "    navigator.clipboard = {\n"
    "      writeText: function(text) { __liwusClipboardText = String(text == null ? '' : text); return Promise.resolve(); },\n"
    "      readText: function() { return Promise.resolve(__liwusClipboardText); },\n"
    "      write: function(items) { return Promise.resolve(items || []); },\n"
    "      read: function() { return Promise.resolve([]); }\n"
    "    };\n"
    "    navigator.sendBeacon = function(url, data) {\n"
    "      try { fetch(String(url), { method: 'POST', body: data }); return true; } catch (e) { return false; }\n"
    "    };\n"
    "    navigator.serviceWorker = {\n"
    "      register: function() { return Promise.reject(new Error('Service workers not supported in sandbox')); },\n"
    "      controller: null,\n"
    "      ready: Promise.reject(new Error('Service workers not supported in sandbox')),\n"
    "      addEventListener: function() {},\n"
    "      removeEventListener: function() {},\n"
    "      dispatchEvent: function() { return true; }\n"
    "    };\n"
    "    navigator.userAgentData = {\n"
    "      brands: [\n"
    "        { brand: 'LiwusWeb', version: '1' },\n"
    "        { brand: 'Chromium', version: '120' }\n"
    "      ],\n"
    "      mobile: false,\n"
    "      platform: 'Linux x86_64',\n"
    "      getHighEntropyValues: function() { return Promise.resolve({}); }\n"
    "    };\n"
    "    navigator.cookieEnabled = true;\n"
    "    navigator.hardwareConcurrency = 4;\n"
    "    navigator.maxTouchPoints = 0;\n"
    "    navigator.languages = ['en-US', 'en'];\n"
    "    navigator.permissions = {\n"
    "      query: function() { return Promise.resolve({ state: 'granted', onchange: null }); }\n"
    "    };\n"
    "    navigator.storage = {\n"
    "      estimate: function() { return Promise.resolve({ quota: 1073741824, usage: 0 }); },\n"
    "      persisted: function() { return Promise.resolve(false); }\n"
    "    };\n"
    "  }\n"
    "\n"
    "  g.getComputedStyle = function(el) {\n"
    "    var inl = (el && el.style) ? el.style : {};\n"
    "    var defs = {\n"
    "      display: 'block', visibility: 'visible', opacity: '1',\n"
    "      color: 'rgb(0, 0, 0)', backgroundColor: 'rgba(0, 0, 0, 0)',\n"
    "      fontSize: '14px', fontFamily: 'sans-serif', fontWeight: 'normal',\n"
    "      width: '800px', height: '600px', position: 'static',\n"
    "      top: 'auto', left: 'auto', right: 'auto', bottom: 'auto', zIndex: 'auto',\n"
    "      margin: '0px', padding: '0px', border: 'none', boxSizing: 'content-box',\n"
    "      overflow: 'visible', cursor: 'auto', transform: 'none', transition: 'none', pointerEvents: 'auto'\n"
    "    };\n"
    "    return new Proxy(defs, {\n"
    "      get: function(t, p) {\n"
    "        if (p === 'length') return Object.keys(t).length;\n"
    "        if (p === 'item') return function(index) { var keys = Object.keys(t), i = Number(index); return Number.isInteger(i) && i >= 0 && i < keys.length ? keys[i] : ''; };\n"
    "        if (p === 'getPropertyPriority') return function() { return ''; };\n"
    "        if (p === 'getPropertyValue') {\n"
    "          return function(n) {\n"
    "            var key = String(n), camel = key.replace(/-([a-z])/g, function(_, c) { return c.toUpperCase(); });\n"
    "            return (inl.getPropertyValue ? inl.getPropertyValue(key) : null) || t[camel] || t[key] || '';\n"
    "          };\n"
    "        }\n"
    "        if (typeof p === 'string') {\n"
    "          var v = inl[p];\n"
    "          if (v !== undefined && v !== '') return v;\n"
    "          return t[p] || '';\n"
    "        }\n"
    "        return undefined;\n"
    "      }\n"
    "    });\n"
    "  };\n"
    "\n"
    "  g.MediaQueryListEvent = class MediaQueryListEvent extends Event {\n"
    "    constructor(type, init) { init = init || {}; super(type, init); this.media = init.media || ''; this.matches = Boolean(init.matches); }\n"
    "  };\n"
    "  g.matchMedia = function(q) {\n"
    "    q = String(q);\n"
    "    var matches = true, known = false;\n"
    "    var minW = q.match(/min-width:\\s*(\\d+)px/);\n"
    "    if (minW) { known = true; if (g.innerWidth < parseInt(minW[1], 10)) matches = false; }\n"
    "    var maxW = q.match(/max-width:\\s*(\\d+)px/);\n"
    "    if (maxW) { known = true; if (g.innerWidth > parseInt(maxW[1], 10)) matches = false; }\n"
    "    if (/prefers-color-scheme\\s*:\\s*light/i.test(q)) { known = true; matches = false; }\n"
    "    else if (/prefers-color-scheme\\s*:\\s*dark/i.test(q)) known = true;\n"
    "    matches = known && matches;\n"
    "    var list = new EventTarget(); list.matches = matches; list.media = q; list.onchange = null;\n"
    "    list.addListener = function(cb) { list.addEventListener('change', cb); };\n"
    "    list.removeListener = function(cb) { list.removeEventListener('change', cb); };\n"
    "    list.dispatchEvent = EventTarget.prototype.dispatchEvent.bind(list);\n"
    "    return list;\n"
    "  };\n"
    "\n"
    "  if (!g.CSS) g.CSS = {};\n"
    "  if (!g.CSS.escape) g.CSS.escape = function(value) {\n"
    "    var s = String(value), out = '';\n"
    "    for (var i = 0; i < s.length; i++) { var c = s.charCodeAt(i);\n"
    "      if (c === 0) { out += '\\uFFFD'; continue; }\n"
    "      if ((c >= 48 && c <= 57 && i === 0) || (c >= 48 && c <= 57 && i === 1 && s.charAt(0) === '-')) out += '\\\\' + c.toString(16) + ' ';\n"
    "      else if (i === 0 && c === 45 && s.length === 1) out += '\\\\-';\n"
    "      else if (c < 32 || c === 127) out += '\\\\' + c.toString(16) + ' ';\n"
    "      else if (/^[A-Za-z0-9_-]$/.test(s.charAt(i)) || c >= 128) out += s.charAt(i);\n"
    "      else out += '\\\\' + s.charAt(i);\n"
    "    } return out;\n"
    "  };\n"
    "  if (!g.CSS.supports) g.CSS.supports = function(prop, value) {\n"
    "    if (arguments.length === 1) { var text = String(prop), at = text.indexOf(':'); if (at < 0) return false; value = text.substring(at + 1); prop = text.substring(0, at); }\n"
    "    prop = String(prop).trim().toLowerCase(); value = String(value == null ? '' : value).trim().toLowerCase();\n"
    "    if (!prop || !value) return false;\n"
    "    var props = { display: 1, position: 1, color: 1, 'background-color': 1, 'font-size': 1, 'font-weight': 1,\n"
    "      margin: 1, padding: 1, width: 1, height: 1, opacity: 1, transform: 1, 'box-sizing': 1,\n"
    "      'flex-direction': 1, 'flex-wrap': 1, 'justify-content': 1, 'align-items': 1, gap: 1,\n"
    "      'grid-template-columns': 1, 'grid-template-rows': 1, 'grid-area': 1, 'z-index': 1 };\n"
    "    if (!props[prop]) return false;\n"
    "    if (prop === 'display') return /^(block|inline|inline-block|flex|grid|none|contents|table|flow-root)$/.test(value);\n"
    "    if (prop === 'position') return /^(static|relative|absolute|fixed|sticky)$/.test(value);\n"
    "    return value !== 'invalid' && value !== 'initial-invalid';\n"
    "  };\n"
    "  if (!g.CSSStyleSheet) g.CSSStyleSheet = class CSSStyleSheet {\n"
    "    constructor() { this.cssRules = []; this.disabled = false; }\n"
    "    replaceSync(text) { this.cssRules = [{ cssText: String(text == null ? '' : text), type: 1 }]; return this; }\n"
    "    replace(text) { this.replaceSync(text); return Promise.resolve(this); }\n"
    "    insertRule(rule, index) { var r = { cssText: String(rule), type: 1 }; if (index == null || index >= this.cssRules.length) this.cssRules.push(r); else this.cssRules.splice(index, 0, r); return index == null ? this.cssRules.length - 1 : index; }\n"
    "    deleteRule(index) { if (index >= 0 && index < this.cssRules.length) this.cssRules.splice(index, 1); }\n"
    "  };\n"
    "  if (!g.ShadowRoot) {\n"
    "    g.ShadowRoot = function ShadowRoot() { throw new TypeError('Illegal constructor'); };\n"
    "    g.ShadowRoot.prototype.toString = function() { return '[object ShadowRoot]'; };\n"
    "  }\n"
    "\n"
    "  g.requestAnimationFrame = function(cb) { return setTimeout(function() { cb(Date.now()); }, 16); };\n"
    "  g.cancelAnimationFrame = function(id) { clearTimeout(id); };\n"
    "  if (typeof Promise.withResolvers !== 'function') Promise.withResolvers = function() { var resolve, reject; var promise = new Promise(function(r, j) { resolve = r; reject = j; }); return { promise: promise, resolve: resolve, reject: reject }; };\n"
    "  if (!g.requestIdleCallback) {\n"
    "    g.requestIdleCallback = function(cb) {\n"
    "      return setTimeout(function() {\n"
    "        cb({ didTimeout: false, timeRemaining: function() { return 50; } });\n"
    "      }, 1);\n"
    "    };\n"
    "  }\n"
    "  if (!g.cancelIdleCallback) g.cancelIdleCallback = function(id) { clearTimeout(id); };\n"
    "  if (!g.queueMicrotask) g.queueMicrotask = function(cb) { Promise.resolve().then(cb); };\n"
    "  if (!g.structuredClone) g.structuredClone = function(val) { return JSON.parse(JSON.stringify(val)); };\n"
    "  if (!g.CSS) g.CSS = {};\n"
    "  if (!g.CSS.supports) g.CSS.supports = function() { return true; };\n"
    "  if (!g.CSS.escape) g.CSS.escape = function(s) { return String(s).replace(/([^\\w-])/g, '\\\\$1'); };\n"
    "  g.alert = function(m) { console.log('[Alert] ' + m); };\n"
    "  g.confirm = function(m) { console.log('[Confirm] ' + m); return true; };\n"
    "  g.prompt = function(m, d) { return d || ''; };\n"
    "  if (!g.open) g.open = function(url, target, features) { var w = { closed: false, location: { href: String(url || 'about:blank') }, close: function() { this.closed = true; }, focus: function() {}, blur: function() {} }; return w; };\n"
    "  if (!g.getSelection) g.getSelection = function() { return { anchorNode: null, focusNode: null, isCollapsed: true, rangeCount: 0, toString: function() { return ''; }, removeAllRanges: function() {}, addRange: function() {} }; };\n"
    "\n"
    "  class Storage {\n"
    "    constructor() { this._d = Object.create(null); }\n"
    "    getItem(k) { k = String(k); return Object.prototype.hasOwnProperty.call(this._d, k) ? this._d[k] : null; }\n"
    "    setItem(k, v) { this._d[String(k)] = String(v); }\n"
    "    removeItem(k) { delete this._d[String(k)]; }\n"
    "    clear() { this._d = Object.create(null); }\n"
    "    key(i) { var ks = Object.keys(this._d); return i >= 0 && i < ks.length ? ks[i] : null; }\n"
    "    get length() { return Object.keys(this._d).length; }\n"
    "  }\n"
    "  function makeSP(s) {\n"
    "    return new Proxy(s, {\n"
    "      get: function(t, p) { if (p in t || typeof p === 'symbol') return t[p]; return t.getItem(p); },\n"
    "      set: function(t, p, v) { if (p in t) { t[p] = v; return true; } t.setItem(p, v); return true; },\n"
    "      deleteProperty: function(t, p) { t.removeItem(p); return true; }\n"
    "    });\n"
    "  }\n"
    "  g.localStorage = makeSP(new Storage());\n"
    "  g.sessionStorage = makeSP(new Storage());\n"
    "\n"
    "  g.crypto = {\n"
    "    getRandomValues: function(arr) {\n"
    "      if (!arr || !arr.buffer) throw new TypeError('Argument must be a TypedArray');\n"
    "      var b = new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength);\n"
    "      for (var i = 0; i < b.length; i++) b[i] = Math.floor(Math.random() * 256);\n"
    "      return arr;\n"
    "    },\n"
    "    randomUUID: function() {\n"
    "      var hex = '0123456789abcdef';\n"
    "      var s = '';\n"
    "      for (var i = 0; i < 36; i++) {\n"
    "        if (i === 8 || i === 13 || i === 18 || i === 23) s += '-';\n"
    "        else if (i === 14) s += '4';\n"
    "        else if (i === 19) s += hex[(Math.floor(Math.random() * 4) + 8)];\n"
    "        else s += hex[Math.floor(Math.random() * 16)];\n"
    "      }\n"
    "      return s;\n"
    "    },\n"
    "    subtle: { digest: function() { return Promise.resolve(new ArrayBuffer(32)); } }\n"
    "  };\n"
    "\n"
    "  g.performance = {\n"
    "    now: function() { return Date.now(); },\n"
    "    timeOrigin: Date.now(),\n"
    "    getEntries: function() { return []; },\n"
    "    getEntriesByType: function() { return []; },\n"
    "    mark: function() {},\n"
    "    measure: function() {}\n"
    "  };\n"
    "\n"
    "  var __liwusMutationObservers = [];\n"
    "  function __liwusMutationNotify(target, record) {\n"
    "    if (record && record.type === 'attributes' && target && target._customElementDefinition) {\n"
    "      var observed = target._customElementDefinition.ctor && target._customElementDefinition.ctor.observedAttributes;\n"
    "      if (observed && record.attributeName && Array.prototype.indexOf.call(observed, record.attributeName) >= 0 && typeof target.attributeChangedCallback === 'function') {\n"
    "        var currentValue = target.getAttribute ? target.getAttribute(record.attributeName) : null;\n"
    "        try { target.attributeChangedCallback(record.attributeName, record.oldValue === undefined ? null : record.oldValue, currentValue); } catch (e) { console.warn('Custom element attributeChangedCallback error:', e); }\n"
    "      }\n"
    "    }\n"
    "    if (record && record.type === 'childList') {\n"
    "      var added = record.addedNodes || [], removed = record.removedNodes || [];\n"
    "      for (var ci = 0; ci < added.length; ci++) __liwusConnectCustom(added[ci]);\n"
    "      for (var ri = 0; ri < removed.length; ri++) __liwusDisconnectCustom(removed[ri]);\n"
    "    }\n"
    "    for (var oi = 0; oi < __liwusMutationObservers.length; oi++) {\n"
    "      var ob = __liwusMutationObservers[oi];\n"
    "      if (!ob._target || !ob._options || !ob._active) continue;\n"
    "      var same = ob._target === target;\n"
    "      var inside = !same && ob._options.subtree && ob._target.contains && ob._target.contains(target);\n"
    "      if (!same && !inside) continue;\n"
    "      if (record.type === 'attributes' && !ob._options.attributes) continue;\n"
    "      if (record.type === 'childList' && !ob._options.childList) continue;\n"
    "      if (record.type === 'characterData' && !ob._options.characterData) continue;\n"
    "      if (ob._options.attributeFilter && record.attributeName &&\n"
    "          ob._options.attributeFilter.indexOf(record.attributeName) < 0) continue;\n"
    "      ob._records.push(g.MutationRecord ? new g.MutationRecord(record) : record);\n"
    "      if (!ob._queued) {\n"
    "        ob._queued = true;\n"
    "        (function(o) { queueMicrotask(function() {\n"
    "          o._queued = false;\n"
    "          if (!o._active || !o._records.length) return;\n"
    "          var records = o._records.slice(); o._records.length = 0;\n"
    "          try { o._cb(records, o); } catch (e) { console.error(e); }\n"
    "        }); })(ob);\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  g.MutationObserver = class MutationObserver {\n"
    "    constructor(cb) { if (typeof cb !== 'function') throw new TypeError('callback must be a function'); this._cb = cb; this._target = null; this._options = null; this._records = []; this._queued = false; this._active = true; __liwusMutationObservers.push(this); }\n"
    "    observe(target, options) { if (!target) throw new TypeError('target is required'); this._target = target; this._options = options || {}; this._active = true; }\n"
    "    disconnect() { this._active = false; this._target = null; this._records.length = 0; }\n"
    "    takeRecords() { var r = this._records.slice(); this._records.length = 0; return r; }\n"
    "  };\n"
    "  g.MutationRecord = class MutationRecord {\n"
    "    constructor(init) { init = init || {}; this.type = init.type || 'childList'; this.target = init.target || null; this.addedNodes = init.addedNodes || []; this.removedNodes = init.removedNodes || []; this.previousSibling = init.previousSibling || null; this.nextSibling = init.nextSibling || null; this.attributeName = init.attributeName || null; this.attributeNamespace = null; this.oldValue = init.oldValue === undefined ? null : init.oldValue; }\n"
    "  };\n"
    "  g.NodeFilter = { SHOW_ALL: 0xFFFFFFFF, SHOW_ELEMENT: 0x1, SHOW_ATTRIBUTE: 0x2, SHOW_TEXT: 0x4, SHOW_CDATA_SECTION: 0x8, SHOW_COMMENT: 0x80, FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3 };\n"
    "  function __liwusWalkNodes(root, out) { if (!root) return; var nodes = root.childNodes || []; for (var i = 0; i < nodes.length; i++) { out.push(nodes[i]); __liwusWalkNodes(nodes[i], out); } }\n"
    "  function __liwusWalker(root, what, filter, iterator) { var all = []; __liwusWalkNodes(root, all); var index = -1; function visible(n) { if (!n) return false; var bit = n.nodeType === 1 ? g.NodeFilter.SHOW_ELEMENT : (n.nodeType === 3 ? g.NodeFilter.SHOW_TEXT : g.NodeFilter.SHOW_ALL); if (what !== 0 && !(what & bit)) return false; if (!filter) return true; var result = typeof filter === 'function' ? filter(n) : (filter.acceptNode ? filter.acceptNode(n) : g.NodeFilter.FILTER_ACCEPT); return result === g.NodeFilter.FILTER_ACCEPT; } var api = { root: root, whatToShow: what, filter: filter || null, currentNode: root, nextNode: function() { for (var i = index + 1; i < all.length; i++) if (visible(all[i])) { index = i; api.currentNode = all[i]; return all[i]; } index = all.length; return null; }, previousNode: function() { for (var i = index - 1; i >= 0; i--) if (visible(all[i])) { index = i; api.currentNode = all[i]; return all[i]; } index = -1; return null; } }; if (!iterator) { api.firstChild = function() { var n = root && root.firstChild; while (n && !visible(n)) n = n.nextSibling; api.currentNode = n || root; return n || null; }; api.lastChild = function() { var n = root && root.lastChild; while (n && !visible(n)) n = n.previousSibling; api.currentNode = n || root; return n || null; }; } return api; }\n"
    "  if (typeof document !== 'undefined') {\n"
    "    if (!document.createTreeWalker) document.createTreeWalker = function(root, what, filter) { return __liwusWalker(root, what == null ? g.NodeFilter.SHOW_ALL : what, filter, false); };\n"
    "    if (!document.createNodeIterator) document.createNodeIterator = function(root, what, filter) { return __liwusWalker(root, what == null ? g.NodeFilter.SHOW_ALL : what, filter, true); };\n"
    "  }\n"
    "  g.ResizeObserverEntry = class ResizeObserverEntry {\n"
    "    constructor(target) { this.target = target; this.contentRect = target && target.getBoundingClientRect ? target.getBoundingClientRect() : new g.DOMRect(); this.borderBoxSize = [{ inlineSize: this.contentRect.width, blockSize: this.contentRect.height }]; this.contentBoxSize = this.borderBoxSize; this.devicePixelContentBoxSize = this.borderBoxSize; }\n"
    "  };\n"
    "  g.ResizeObserver = class ResizeObserver {\n"
    "    constructor(cb) { this._cb = cb; this._targets = []; }\n"
    "    observe(el) { if (!el) return; if (this._targets.indexOf(el) < 0) this._targets.push(el); var self = this; queueMicrotask(function() { if (self._cb) self._cb(self._targets.map(function(t) { return new g.ResizeObserverEntry(t); }), self); }); }\n"
    "    unobserve(el) { this._targets = this._targets.filter(function(t) { return t !== el; }); }\n"
    "    disconnect() { this._targets.length = 0; }\n"
    "  };\n"
    "  g.IntersectionObserverEntry = class IntersectionObserverEntry {\n"
    "    constructor(init) { init = init || {}; this.time = Number(init.time || Date.now()); this.target = init.target || null; this.rootBounds = init.rootBounds || null; this.boundingClientRect = init.boundingClientRect || new g.DOMRect(); this.intersectionRect = init.intersectionRect || this.boundingClientRect; this.isIntersecting = Boolean(init.isIntersecting); this.intersectionRatio = Number(init.intersectionRatio || 0); }\n"
    "  };\n"
    "  g.IntersectionObserver = class IntersectionObserver {\n"
    "    constructor(cb, options) { this._cb = cb; this._options = options || {}; this._targets = []; }\n"
    "    observe(el) { if (!el) return; if (this._targets.indexOf(el) < 0) this._targets.push(el); var self = this; queueMicrotask(function() { if (self._cb) self._cb(self._targets.map(function(t) { var rect = t.getBoundingClientRect ? t.getBoundingClientRect() : new g.DOMRect(); return new g.IntersectionObserverEntry({ target: t, isIntersecting: true, intersectionRatio: 1, boundingClientRect: rect, intersectionRect: rect, rootBounds: null, time: Date.now() }); }), self); }); }\n"
    "    unobserve(el) { this._targets = this._targets.filter(function(t) { return t !== el; }); }\n"
    "    disconnect() { this._targets.length = 0; }\n"
    "    takeRecords() { return []; }\n"
    "  };\n"
    "\n"
    "  g.btoa = function(str) {\n"
    "    var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=';\n"
    "    var out = ''; str = String(str);\n"
    "    for (var b, c, idx = 0, m = chars; str.charAt(idx | 0) || (m = '=', idx % 1); out += m.charAt(63 & b >> 8 - idx % 1 * 8)) {\n"
    "      c = str.charCodeAt(idx += 3/4);\n"
    "      if (c > 0xFF) throw new Error('btoa: Character out of range');\n"
    "      b = b << 8 | c;\n"
    "    }\n"
    "    return out;\n"
    "  };\n"
    "  g.atob = function(inp) {\n"
    "    var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=';\n"
    "    var str = String(inp).replace(/=+$/, '');\n"
    "    if (str.length % 4 === 1) throw new Error('atob: Invalid base64');\n"
    "    var out = '';\n"
    "    for (var bc = 0, bs, b, idx = 0; b = str.charAt(idx++); ~b && (bs = bc % 4 ? bs * 64 + b : b, bc++ % 4) ? out += String.fromCharCode(255 & bs >> (-2 * bc & 6)) : 0) {\n"
    "      b = chars.indexOf(b);\n"
    "    }\n"
    "    return out;\n"
    "  };\n"
    "\n"
    "  g.TextEncoder = class TextEncoder {\n"
    "    constructor() { this.encoding = 'utf-8'; }\n"
    "    encode(str) {\n"
    "      str = String(str);\n"
    "      var u = [];\n"
    "      for (var i = 0; i < str.length; i++) {\n"
    "        var c = str.charCodeAt(i);\n"
    "        if (c < 0x80) u.push(c);\n"
    "        else if (c < 0x800) u.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f));\n"
    "        else if (c < 0xd800 || c >= 0xe000) u.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));\n"
    "        else { i++; c = 0x10000 + (((c & 0x3ff) << 10) | (str.charCodeAt(i) & 0x3ff)); u.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 0x3f), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f)); }\n"
    "      }\n"
    "      return new Uint8Array(u);\n"
    "    }\n"
    "  };\n"
    "  g.TextDecoder = class TextDecoder {\n"
    "    constructor(l) { this.encoding = l || 'utf-8'; }\n"
    "    decode(b) {\n"
    "      if (!b) return '';\n"
    "      var a = new Uint8Array(b.buffer || b, b.byteOffset || 0, b.byteLength || b.length);\n"
    "      var s = '';\n"
    "      for (var i = 0; i < a.length; i++) s += String.fromCharCode(a[i]);\n"
    "      try { return decodeURIComponent(escape(s)); } catch(e) { return s; }\n"
    "    }\n"
    "  };\n"
    "\n"
    "  class URLSearchParams {\n"
    "    constructor(init) {\n"
    "      this._p = [];\n"
    "      if (typeof init === 'string') {\n"
    "        if (init.charAt(0) === '?') init = init.slice(1);\n"
    "        if (init) {\n"
    "          var pairs = init.split('&');\n"
    "          for (var i = 0; i < pairs.length; i++) {\n"
    "            var eq = pairs[i].indexOf('=');\n"
    "            if (eq >= 0) this.append(decodeURIComponent(pairs[i].slice(0, eq).split('+').join(' ')), decodeURIComponent(pairs[i].slice(eq + 1).split('+').join(' ')));\n"
    "            else this.append(decodeURIComponent(pairs[i].split('+').join(' ')), '');\n"
    "          }\n"
    "        }\n"
    "      } else if (Array.isArray(init)) {\n"
    "        for (var ai = 0; ai < init.length; ai++) {\n"
    "          var pair = init[ai];\n"
    "          if (Array.isArray(pair) && pair.length >= 2) this.append(pair[0], pair[1]);\n"
    "        }\n"
    "      } else if (init && typeof init === 'object') {\n"
    "        var self = this; Object.keys(init).forEach(function(k) { self.append(k, init[k]); });\n"
    "      }\n"
    "    }\n"
    "    append(k, v) { this._p.push([String(k), String(v)]); }\n"
    "    delete(k) { this._p = this._p.filter(function(x) { return x[0] !== String(k); }); }\n"
    "    get(k) { var x = this._p.find(function(p) { return p[0] === String(k); }); return x ? x[1] : null; }\n"
    "    getAll(k) { return this._p.filter(function(x) { return x[0] === String(k); }).map(function(x) { return x[1]; }); }\n"
    "    has(k) { return this._p.some(function(x) { return x[0] === String(k); }); }\n"
    "    set(k, v) { this.delete(k); this.append(k, v); }\n"
    "    toString() { return this._p.map(function(x) { return encodeURIComponent(x[0]) + '=' + encodeURIComponent(x[1]); }).join('&'); }\n"
    "    forEach(cb) { var s = this; this._p.forEach(function(x) { cb(x[1], x[0], s); }); }\n"
    "    entries() { return this._p.slice()[Symbol.iterator](); }\n"
    "    keys() { return this._p.map(function(x) { return x[0]; })[Symbol.iterator](); }\n"
    "    values() { return this._p.map(function(x) { return x[1]; })[Symbol.iterator](); }\n"
    "    sort() { this._p.sort(function(a, b) { return a[0] < b[0] ? -1 : (a[0] > b[0] ? 1 : 0); }); }\n"
    "    get size() { return this._p.length; }\n"
    "    [Symbol.iterator]() { return this._p.slice()[Symbol.iterator](); }\n"
    "  }\n"
    "  g.URLSearchParams = URLSearchParams;\n"
    "\n"
    "  class URL {\n"
    "    constructor(u, b) {\n"
    "      var full = String(u);\n"
    "      if (b && full.indexOf('://') < 0) {\n"
    "        var bs = String(b);\n"
    "        var bEnd = bs.search(/[?#]/); if (bEnd >= 0) bs = bs.substring(0, bEnd);\n"
    "        var schemeEnd = bs.indexOf('://');\n"
    "        var baseOrigin = schemeEnd >= 0 ? bs.substring(0, bs.indexOf('/', schemeEnd + 3) >= 0 ? bs.indexOf('/', schemeEnd + 3) : bs.length) : '';\n"
    "        if (full.indexOf('//') === 0) {\n"
    "          full = (schemeEnd >= 0 ? bs.substring(0, schemeEnd + 1) : 'https:') + full;\n"
    "        } else if (full.charAt(0) === '?') {\n"
    "          full = bs + full;\n"
    "        } else if (full.charAt(0) === '#') {\n"
    "          full = bs + full;\n"
    "        } else if (full.charAt(0) === '/') {\n"
    "          full = baseOrigin + full;\n"
    "        } else {\n"
    "          var lSlash = bs.lastIndexOf('/');\n"
    "          full = (lSlash >= 0 ? bs.substring(0, lSlash + 1) : (bs + '/')) + full;\n"
    "        }\n"
    "      }\n"
    "      var urlPrefix = full.match(/^([a-zA-Z][a-zA-Z0-9+.-]*:\\/\\/[^/]*)(.*)$/);\n"
    "      if (urlPrefix && urlPrefix[2]) {\n"
    "        var pathPart = urlPrefix[2], suffixAt = pathPart.search(/[?#]/), suffix = suffixAt >= 0 ? pathPart.substring(suffixAt) : '';\n"
    "        var pathOnly = suffixAt >= 0 ? pathPart.substring(0, suffixAt) : pathPart, parts = pathOnly.split('/'), stack = [];\n"
    "        for (var pi = 0; pi < parts.length; pi++) { var part = parts[pi]; if (!part || part === '.') continue; if (part === '..') { if (stack.length) stack.pop(); } else stack.push(part); }\n"
    "        full = urlPrefix[1] + '/' + stack.join('/') + suffix;\n"
    "      }\n"
    "      this._parse(full);\n"
    "    }\n"
    "    _parse(full) {\n"
    "      var pEnd = full.indexOf('://');\n"
    "      this.protocol = pEnd >= 0 ? full.substring(0, pEnd + 1) : 'http:';\n"
    "      var rest = pEnd >= 0 ? full.substring(pEnd + 3) : full;\n"
    "      var hIdx = rest.indexOf('#');\n"
    "      this.hash = hIdx >= 0 ? rest.substring(hIdx) : '';\n"
    "      if (hIdx >= 0) rest = rest.substring(0, hIdx);\n"
    "      var qIdx = rest.indexOf('?');\n"
    "      this.search = qIdx >= 0 ? rest.substring(qIdx) : '';\n"
    "      if (qIdx >= 0) rest = rest.substring(0, qIdx);\n"
    "      var pIdx = rest.indexOf('/');\n"
    "      this.host = pIdx >= 0 ? rest.substring(0, pIdx) : rest;\n"
    "      this.pathname = pIdx >= 0 ? rest.substring(pIdx) : '/';\n"
    "      this.origin = this.protocol + '//' + this.host;\n"
    "      var col = this.host.lastIndexOf(':');\n"
    "      if (col > 0 && !this.host.endsWith(']')) {\n"
    "        this.hostname = this.host.substring(0, col);\n"
    "        this.port = this.host.substring(col + 1);\n"
    "      } else {\n"
    "        this.hostname = this.host;\n"
    "        this.port = '';\n"
    "      }\n"
    "      this.searchParams = new URLSearchParams(this.search);\n"
    "    }\n"
    "    get href() {\n"
    "      var s = this.searchParams.toString();\n"
    "      return (this.origin || '') + this.pathname + (s ? '?' + s : '') + this.hash;\n"
    "    }\n"
    "    set href(v) { this._parse(String(v)); }\n"
    "    toString() { return this.href; }\n"
    "    static createObjectURL() { return 'blob:null/' + g.crypto.randomUUID(); }\n"
    "    static revokeObjectURL() {}\n"
    "    static canParse(u, b) { try { new URL(u, b); return true; } catch(e) { return false; } }\n"
    "  }\n"
    "  g.URL = URL;\n"
    "\n"
    "  if (!g.URLPattern) {\n"
    "    class URLPattern {\n"
    "      constructor(input, base) { if (typeof input === 'string') this._pattern = new URL(input, base || 'https://liwus.invalid/'); else this._pattern = input || {}; this.protocol = this._pattern.protocol || '*'; this.hostname = this._pattern.hostname || '*'; this.port = this._pattern.port || '*'; this.pathname = this._pattern.pathname || '*'; this.search = this._pattern.search || '*'; this.hash = this._pattern.hash || '*'; }\n"
    "      _match(pattern, value) { pattern = String(pattern || '*'); value = String(value || ''); if (pattern === '*') return true; if (pattern.charAt(0) === '*' && pattern.charAt(pattern.length - 1) === '*') return value.indexOf(pattern.slice(1, -1)) >= 0; if (pattern.charAt(0) === '*') return value.slice(-pattern.length + 1) === pattern.slice(1); if (pattern.charAt(pattern.length - 1) === '*') return value.slice(0, pattern.length - 1) === pattern.slice(0, -1); return pattern === value; }\n"
    "      test(input, base) { var u = input instanceof URL ? input : new URL(input, base || 'https://liwus.invalid/'); return this._match(this.protocol, u.protocol) && this._match(this.hostname, u.hostname) && this._match(this.port, u.port) && this._match(this.pathname, u.pathname) && this._match(this.search, u.search) && this._match(this.hash, u.hash); }\n"
    "      exec(input, base) { if (!this.test(input, base)) return null; var u = input instanceof URL ? input : new URL(input, base || 'https://liwus.invalid/'); return { protocol: { input: u.protocol }, hostname: { input: u.hostname }, port: { input: u.port }, pathname: { input: u.pathname }, search: { input: u.search }, hash: { input: u.hash }, groups: {} }; }\n"
    "    }\n"
    "    g.URLPattern = URLPattern;\n"
    "  }\n"
    "\n"
    "  class Headers {\n"
    "    constructor(init) {\n"
    "      this._h = Object.create(null);\n"
    "      if (init) {\n"
    "        if (init instanceof Headers) init.forEach((v, k) => this.set(k, v));\n"
    "        else if (Array.isArray(init)) init.forEach(([k, v]) => this.append(k, v));\n"
    "        else Object.keys(init).forEach(k => this.set(k, init[k]));\n"
    "      }\n"
    "    }\n"
    "    append(k, v) { k = k.toLowerCase(); this._h[k] = this._h[k] ? this._h[k] + ', ' + v : String(v); }\n"
    "    delete(k) { delete this._h[k.toLowerCase()]; }\n"
    "    get(k) { return this._h[k.toLowerCase()] || null; }\n"
    "    has(k) { return k.toLowerCase() in this._h; }\n"
    "    set(k, v) { this._h[k.toLowerCase()] = String(v); }\n"
    "    forEach(cb) { Object.keys(this._h).forEach(k => cb(this._h[k], k, this)); }\n"
    "    entries() { return Object.entries(this._h)[Symbol.iterator](); }\n"
    "    keys() { return Object.keys(this._h)[Symbol.iterator](); }\n"
    "    values() { return Object.values(this._h)[Symbol.iterator](); }\n"
    "    getSetCookie() { var v = this.get('set-cookie'); return v ? v.split(/,\\s*(?=[^;]+?=)/) : []; }\n"
    "    [Symbol.iterator]() { return this.entries()[Symbol.iterator](); }\n"
    "  }\n"
    "  g.Headers = Headers;\n"
    "\n"
    "  class Response {\n"
    "    constructor(body, init) {\n"
    "      init = init || {};\n"
    "      this._b = body != null ? String(body) : '';\n"
    "      this.status = init.status != null ? init.status : 200;\n"
    "      this.statusText = init.statusText || 'OK';\n"
    "      this.ok = this.status >= 200 && this.status < 300;\n"
    "      this.headers = new Headers(init.headers);\n"
    "      this.url = init.url || '';\n"
    "      this.redirected = Boolean(init.redirected);\n"
    "      this.type = init.type || 'basic';\n"
    "      this.bodyUsed = false;\n"
    "    }\n"
    "    _consume() { if (this.bodyUsed) return Promise.reject(new TypeError('Body has already been consumed')); this.bodyUsed = true; return Promise.resolve(this._b); }\n"
    "    text() { return this._consume(); }\n"
    "    json() { var self = this; return this._consume().then(function(body) { return JSON.parse(body); }); }\n"
    "    blob() { var self = this; return this._consume().then(function(body) { return new Blob([body], { type: self.headers.get('content-type') || 'text/plain' }); }); }\n"
    "    arrayBuffer() { var self = this; return this._consume().then(function(body) { return (new TextEncoder()).encode(body).buffer; }); }\n"
    "    formData() { return this._consume().then(function() { return new FormData(); }); }\n"
    "    clone() { if (this.bodyUsed) throw new TypeError('Body has already been consumed'); return new Response(this._b, { status: this.status, statusText: this.statusText, headers: this.headers, url: this.url, redirected: this.redirected, type: this.type }); }\n"
    "  }\n"
    "  Response.error = function() { return new Response('', { status: 0, statusText: '', type: 'error' }); };\n"
    "  Response.redirect = function(url, status) { status = status || 302; if ([301,302,303,307,308].indexOf(status) < 0) throw new RangeError('Invalid redirect status'); return new Response('', { status: status, url: String(url), type: 'opaqueredirect', headers: { location: String(url) } }); };\n"
    "  Response.json = function(data, init) { init = init || {}; var headers = new Headers(init.headers); if (!headers.has('content-type')) headers.set('content-type', 'application/json'); return new Response(JSON.stringify(data), { status: init.status == null ? 200 : init.status, statusText: init.statusText || 'OK', headers: headers, url: init.url || '' }); };\n"
    "  g.Response = Response;\n"
    "\n"
    "  class Request {\n"
    "    constructor(input, init) {\n"
    "      init = init || {};\n"
    "      this.url = typeof input === 'string' ? input : input.url;\n"
    "      this.method = (init.method || (input.method || 'GET')).toUpperCase();\n"
    "      this.headers = new Headers(init.headers || input.headers);\n"
    "      this.body = init.body || null;\n"
    "      this.cache = init.cache || (input.cache || 'default');\n"
    "      this.credentials = init.credentials || (input.credentials || 'same-origin');\n"
    "      this.mode = init.mode || (input.mode || 'cors');\n"
    "      this.redirect = init.redirect || (input.redirect || 'follow');\n"
    "      this.referrer = init.referrer || (input.referrer || 'about:client');\n"
    "      this.signal = init.signal || (input.signal || new AbortSignal());\n"
    "      this.bodyUsed = false;\n"
    "    }\n"
    "    _consume() { if (this.bodyUsed) return Promise.reject(new TypeError('Body has already been consumed')); this.bodyUsed = true; return Promise.resolve(this.body == null ? '' : String(this.body)); }\n"
    "    text() { return this._consume(); }\n"
    "    json() { return this._consume().then(function(body) { return JSON.parse(body); }); }\n"
    "    arrayBuffer() { return this._consume().then(function(body) { return (new TextEncoder()).encode(body).buffer; }); }\n"
    "    blob() { return this._consume().then(function(body) { return new Blob([body]); }); }\n"
    "    formData() { return this._consume().then(function() { return new FormData(); }); }\n"
    "    clone() { if (this.bodyUsed) throw new TypeError('Body has already been consumed'); return new Request(this.url, { method: this.method, headers: this.headers, body: this.body, cache: this.cache, credentials: this.credentials, mode: this.mode, redirect: this.redirect, referrer: this.referrer, signal: this.signal }); }\n"
    "  }\n"
    "  g.Request = Request;\n"
    "  if (typeof g.fetch === 'function' && !g.__liwusNativeFetchWrapped) {\n"
    "    var __liwusNativeFetch = g.fetch;\n"
    "    g.fetch = function(input, init) {\n"
    "      var request = input instanceof Request ? input : new Request(input, init || {});\n"
    "      if (request.signal && request.signal.aborted) return Promise.reject(request.signal.reason || new Error('The operation was aborted'));\n"
    "      var headerText = '';\n"
    "      if (request.headers && request.headers.forEach) request.headers.forEach(function(v, k) { if (headerText) headerText += '|'; headerText += String(k) + ':' + String(v); });\n"
    "      var bodyText = '';\n"
    "      if (request.body != null) {\n"
    "        if (typeof URLSearchParams !== 'undefined' && request.body instanceof URLSearchParams) {\n"
    "          bodyText = request.body.toString();\n"
    "          if (!request.headers.has('content-type')) { request.headers.set('content-type', 'application/x-www-form-urlencoded;charset=UTF-8'); headerText = ''; request.headers.forEach(function(v, k) { if (headerText) headerText += '|'; headerText += String(k) + ':' + String(v); }); }\n"
    "        } else if (typeof FormData !== 'undefined' && request.body instanceof FormData) {\n"
    "          var formPairs = []; request.body.forEach(function(v, k) { formPairs.push(encodeURIComponent(k) + '=' + encodeURIComponent(typeof v === 'string' ? v : String(v))); });\n"
    "          bodyText = formPairs.join('&');\n"
    "          if (!request.headers.has('content-type')) { request.headers.set('content-type', 'application/x-www-form-urlencoded;charset=UTF-8'); headerText = ''; request.headers.forEach(function(v, k) { if (headerText) headerText += '|'; headerText += String(k) + ':' + String(v); }); }\n"
    "        } else if (typeof Blob !== 'undefined' && request.body instanceof Blob) {\n"
    "          bodyText = (request.body._parts || []).join('');\n"
    "        } else { bodyText = String(request.body); }\n"
    "      }\n"
    "      var nativePromise = __liwusNativeFetch(request.url, request.method, headerText, bodyText);\n"
    "      if (!request.signal || !request.signal.addEventListener) return nativePromise;\n"
    "      return new Promise(function(resolve, reject) {\n"
    "        var settled = false;\n"
    "        function finishResolve(value) { if (!settled) { settled = true; resolve(value); } }\n"
    "        function finishReject(error) { if (!settled) { settled = true; reject(error); } }\n"
    "        request.signal.addEventListener('abort', function() { finishReject(request.signal.reason || new DOMException('The operation was aborted', 'AbortError')); }, { once: true });\n"
    "        nativePromise.then(finishResolve, finishReject);\n"
    "      });\n"
    "    };\n"
    "    g.__liwusNativeFetchWrapped = true;\n"
    "  }\n"
    "\n"
    "  class FormData {\n"
    "    constructor(form) {\n"
    "      this._entries = [];\n"
    "      if (form && form.elements) {\n"
    "        for (var i = 0; i < form.elements.length; i++) {\n"
    "          var el = form.elements[i];\n"
    "          if (el.name && !el.disabled) {\n"
    "            if (el.type === 'checkbox' || el.type === 'radio') {\n"
    "              if (el.checked) this.append(el.name, el.value || 'on');\n"
    "            } else {\n"
    "              this.append(el.name, el.value || '');\n"
    "            }\n"
    "          }\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "    append(k, v) { this._entries.push([String(k), v]); }\n"
    "    delete(k) { this._entries = this._entries.filter(function(x) { return x[0] !== String(k); }); }\n"
    "    get(k) { var x = this._entries.find(function(e) { return e[0] === String(k); }); return x ? x[1] : null; }\n"
    "    getAll(k) { return this._entries.filter(function(e) { return e[0] === String(k); }).map(function(e) { return e[1]; }); }\n"
    "    has(k) { return this._entries.some(function(e) { return e[0] === String(k); }); }\n"
    "    set(k, v) { this.delete(k); this.append(k, v); }\n"
    "    forEach(cb) { var s = this; this._entries.forEach(function(x) { cb(x[1], x[0], s); }); }\n"
    "    entries() { return this._entries.slice()[Symbol.iterator](); }\n"
    "    keys() { return this._entries.map(function(x) { return x[0]; })[Symbol.iterator](); }\n"
    "    values() { return this._entries.map(function(x) { return x[1]; })[Symbol.iterator](); }\n"
    "    [Symbol.iterator]() { return this._entries.slice()[Symbol.iterator](); }\n"
    "  }\n"
    "  g.FormData = FormData;\n"
    "\n"
    "  class Blob {\n"
    "    constructor(parts, options) {\n"
    "      options = options || {};\n"
    "      this.type = options.type || '';\n"
    "      this.size = (parts || []).reduce(function(acc, p) { return acc + (p.byteLength || (p.length || 0)); }, 0);\n"
    "      this._parts = parts || [];\n"
    "    }\n"
    "    text() { return Promise.resolve(this._parts.join('')); }\n"
    "    arrayBuffer() { return Promise.resolve(new ArrayBuffer(this.size)); }\n"
    "    stream() { var self = this; return new ReadableStream({ start: function(c) { if (self._parts.length) c.enqueue(self._parts.join('')); c.close(); } }); }\n"
    "    bytes() { return this.arrayBuffer().then(function(buf) { return new Uint8Array(buf); }); }\n"
    "    slice() { return new Blob([], { type: this.type }); }\n"
    "  }\n"
    "  g.Blob = Blob;\n"
    "\n"
    "  if (!g.FileReader) {\n"
    "    class FileReader extends EventTarget {\n"
    "      constructor() { super(); this.readyState = 0; this.result = null; this.error = null; this.onload = null; this.onerror = null; this.onloadend = null; }\n"
    "      _finish(value) { this.result = value; this.readyState = 2; var ev = new Event('load'); if (this.onload) this.onload(ev); this.dispatchEvent(ev); var end = new Event('loadend'); if (this.onloadend) this.onloadend(end); this.dispatchEvent(end); }\n"
    "      _fail(err) { this.error = err || new Error('FileReader failed'); this.readyState = 2; var ev = new ErrorEvent('error', { error: this.error, message: this.error.message || '' }); if (this.onerror) this.onerror(ev); this.dispatchEvent(ev); }\n"
    "      readAsText(blob) { this.readyState = 1; var self = this; Promise.resolve(blob && blob.text ? blob.text() : '').then(function(v) { self._finish(v); }, function(e) { self._fail(e); }); }\n"
    "      readAsArrayBuffer(blob) { this.readyState = 1; var self = this; Promise.resolve(blob && blob.arrayBuffer ? blob.arrayBuffer() : new ArrayBuffer(0)).then(function(v) { self._finish(v); }, function(e) { self._fail(e); }); }\n"
    "      readAsDataURL(blob) { this.readyState = 1; var self = this; Promise.resolve(blob && blob.text ? blob.text() : '').then(function(v) { self._finish('data:' + ((blob && blob.type) || 'application/octet-stream') + ';base64,' + btoa(v)); }, function(e) { self._fail(e); }); }\n"
    "      abort() { this.readyState = 0; this.result = null; }\n"
    "    }\n"
    "    FileReader.EMPTY = 0; FileReader.LOADING = 1; FileReader.DONE = 2; g.FileReader = FileReader;\n"
    "  }\n"
    "\n"
    "  class File extends Blob {\n"
    "    constructor(parts, name, options) {\n"
    "      super(parts, options);\n"
    "      this.name = String(name);\n"
    "      this.lastModified = (options && options.lastModified) || Date.now();\n"
    "    }\n"
    "  }\n"
    "  g.File = File;\n"
    "\n"
    "  if (!g.ReadableStream) {\n"
    "    class ReadableStream {\n"
    "      constructor(source) {\n"
    "        source = source || {}; this._queue = []; this._closed = false; this._errored = null; this._reader = null;\n"
    "        var self = this; this._controller = { enqueue: function(v) { if (!self._closed) self._queue.push(v); }, close: function() { self._closed = true; }, error: function(e) { self._errored = e || new Error('Stream error'); self._closed = true; } };\n"
    "        try { if (source.start) source.start(this._controller); } catch (e) { this._controller.error(e); }\n"
    "      }\n"
    "      get locked() { return Boolean(this._reader); }\n"
    "      getReader() { if (this._reader) throw new TypeError('ReadableStream is locked'); var self = this; this._reader = {\n"
    "        get closed() { return self._closed ? Promise.resolve() : new Promise(function(resolve) { self._closedResolve = resolve; }); },\n"
    "        read: function() { if (self._errored) return Promise.reject(self._errored); if (self._queue.length) return Promise.resolve({ done: false, value: self._queue.shift() }); return Promise.resolve({ done: self._closed, value: undefined }); },\n"
    "        cancel: function(reason) { self._queue = []; self._closed = true; try { if (self._cancel) self._cancel(reason); } catch (e) {} return Promise.resolve(); },\n"
    "        releaseLock: function() { self._reader = null; }\n"
    "      }; return this._reader; }\n"
    "      cancel(reason) { this._queue = []; this._closed = true; try { if (this._cancel) this._cancel(reason); } catch (e) {} return Promise.resolve(); }\n"
    "      tee() { var a = this, left = [], right = []; while (a._queue.length) { var v = a._queue.shift(); left.push(v); right.push(v); } return [new ReadableStream({ start: function(c) { left.forEach(c.enqueue); if (a._closed) c.close(); } }), new ReadableStream({ start: function(c) { right.forEach(c.enqueue); if (a._closed) c.close(); } })]; }\n"
    "      pipeTo(dest) { var reader = this.getReader(); var loop = function() { return reader.read().then(function(r) { if (r.done) { if (dest.close) dest.close(); return; } if (dest.write) dest.write(r.value); return loop(); }); }; return loop(); }\n"
    "      pipeThrough(pair) { this.pipeTo(pair.writable); return pair.readable; }\n"
    "    }\n"
    "    g.ReadableStream = ReadableStream;\n"
    "  }\n"
    "  if (!g.WritableStream) {\n"
    "    g.WritableStream = class WritableStream {\n"
    "      constructor(sink) { this._sink = sink || {}; this._closed = false; }\n"
    "      get locked() { return Boolean(this._writer); }\n"
    "      getWriter() { if (this._writer) throw new TypeError('WritableStream is locked'); var self = this; this._writer = { write: function(v) { try { return Promise.resolve(self._sink.write ? self._sink.write(v) : undefined); } catch (e) { return Promise.reject(e); } }, close: function() { self._closed = true; try { return Promise.resolve(self._sink.close ? self._sink.close() : undefined); } catch (e) { return Promise.reject(e); } }, abort: function(e) { self._closed = true; try { return Promise.resolve(self._sink.abort ? self._sink.abort(e) : undefined); } catch (x) { return Promise.reject(x); } }, releaseLock: function() { self._writer = null; } }; return this._writer; }\n"
    "    };\n"
    "  }\n"
    "  if (!g.TransformStream) g.TransformStream = class TransformStream {\n"
    "    constructor(transformer) { var self = this; transformer = transformer || {}; this.readable = new ReadableStream({ start: function(c) { self._readController = c; } }); this.writable = new WritableStream({ write: function(v) { if (transformer.transform) transformer.transform(v, { enqueue: function(x) { self._readController.enqueue(x); }, error: function(e) { self._readController.error(e); } }); else self._readController.enqueue(v); }, close: function() { if (transformer.flush) transformer.flush({ enqueue: function(x) { self._readController.enqueue(x); } }); self._readController.close(); } }); }\n"
    "  };\n"
    "  if (!g.TextEncoderStream) g.TextEncoderStream = class TextEncoderStream { constructor() { var enc = new TextEncoder(); var t = new TransformStream({ transform: function(v, c) { c.enqueue(enc.encode(String(v))); } }); this.readable = t.readable; this.writable = t.writable; } };\n"
    "  if (!g.TextDecoderStream) g.TextDecoderStream = class TextDecoderStream { constructor() { var dec = new TextDecoder(); var t = new TransformStream({ transform: function(v, c) { c.enqueue(dec.decode(v, { stream: true })); }, flush: function(c) { c.enqueue(dec.decode()); } }); this.readable = t.readable; this.writable = t.writable; } };\n"
    "  if (g.Response && !Object.getOwnPropertyDescriptor(g.Response.prototype, 'body')) Object.defineProperty(g.Response.prototype, 'body', { get: function() { var self = this; if (!self._stream) self._stream = new ReadableStream({ start: function(c) { if (!self.bodyUsed) c.enqueue(self._b); c.close(); } }); return self._stream; }, configurable: true });\n"
    "\n"
    "  class DOMParser {\n"
    "    parseFromString(source, type) {\n"
    "      var host = document.createElement('div');\n"
    "      host.innerHTML = String(source == null ? '' : source);\n"
    "      var parsed = {\n"
    "        contentType: type || 'text/html', body: host, documentElement: host,\n"
    "        head: host.querySelector ? host.querySelector('head') : null,\n"
    "        querySelector: function(s) { return host.querySelector(s); },\n"
    "        querySelectorAll: function(s) { return host.querySelectorAll(s); },\n"
    "        getElementById: function(id) { return host.querySelector('#' + id); },\n"
    "        createElement: function(n) { return document.createElement(n); },\n"
    "        createTextNode: function(t) { return document.createTextNode(t); },\n"
    "        createDocumentFragment: function() { return document.createDocumentFragment(); },\n"
    "        addEventListener: function() {}, removeEventListener: function() {},\n"
    "        getElementsByTagName: function(n) { return host.querySelectorAll(n); },\n"
    "        getElementsByClassName: function(n) { return host.querySelectorAll('.' + n); }\n"
    "      };\n"
    "      return parsed;\n"
    "    }\n"
    "  }\n"
    "  g.DOMParser = DOMParser;\n"
    "  g.XMLSerializer = class XMLSerializer {\n"
    "    serializeToString(node) {\n"
    "      if (node == null) return '';\n"
    "      if (node.outerHTML != null) return String(node.outerHTML);\n"
    "      return node.textContent != null ? String(node.textContent) : String(node);\n"
    "    }\n"
    "  };\n"
    "  if (!g.structuredClone) {\n"
    "    g.structuredClone = function(value) {\n"
    "      if (value == null || typeof value !== 'object') return value;\n"
    "      if (value instanceof Date) return new Date(value.getTime());\n"
    "      if (value instanceof ArrayBuffer) return value.slice(0);\n"
    "      if (ArrayBuffer.isView && ArrayBuffer.isView(value)) return new value.constructor(value);\n"
    "      if (Array.isArray(value)) return value.map(g.structuredClone);\n"
    "      var out = {}; Object.keys(value).forEach(function(k) { out[k] = g.structuredClone(value[k]); }); return out;\n"
    "    };\n"
    "  }\n"
    "\n"
    "  class XMLHttpRequest extends EventTarget {\n"
    "    constructor() {\n"
    "      super();\n"
    "      this.readyState = 0; this.status = 0; this.statusText = '';\n"
    "      this.responseText = ''; this.response = ''; this.responseType = '';\n"
    "      this.responseURL = ''; this.timeout = 0; this.withCredentials = false;\n"
    "      this.onreadystatechange = null; this.onloadstart = null; this.onload = null; this.onerror = null; this.onabort = null; this.onloadend = null; this.ontimeout = null;\n"
    "      this._h = {}; this._rh = {}; this._aborted = false; this.upload = new EventTarget();\n"
    "    }\n"
    "    _emit(type, init) { var ev = new Event(type, init || {}); var handler = this['on' + type]; if (handler) handler.call(this, ev); this.dispatchEvent(ev); }\n"
    "    open(m, u, async, user, password) { if (async === false) throw new DOMException('Synchronous XHR is not supported', 'InvalidAccessError'); this._m = String(m || 'GET').toUpperCase(); this._u = String(u); this._aborted = false; this.readyState = 1; this._emit('readystatechange'); }\n"
    "    setRequestHeader(k, v) { if (this.readyState !== 1) throw new DOMException('Invalid XHR state', 'InvalidStateError'); this._h[String(k).toLowerCase()] = String(v); }\n"
    "    send(b) {\n"
    "      if (this.readyState !== 1) throw new DOMException('Invalid XHR state', 'InvalidStateError');\n"
    "      var self = this; this._aborted = false; this._emit('loadstart');\n"
    "      fetch(this._u, { method: this._m, headers: this._h, body: b })\n"
    "        .then(function(res) { if (self._aborted) throw new Error('aborted'); self.status = res.status; self.statusText = res.statusText || 'OK'; self.responseURL = res.url || self._u; self._rh = {}; if (res.headers && res.headers.forEach) res.headers.forEach(function(v, k) { self._rh[String(k).toLowerCase()] = String(v); }); self.readyState = 2; self._emit('readystatechange'); return res.text(); })\n"
    "        .then(function(txt) {\n"
    "          if (self._aborted) return; self.readyState = 3; self.responseText = txt; self.response = self.responseType === 'json' ? JSON.parse(txt) : txt; self._emit('readystatechange'); self.readyState = 4; self._emit('readystatechange'); self._emit('load'); self._emit('loadend');\n"
    "        })\n"
    "        .catch(function(err) {\n"
    "          if (self._aborted) return; self.status = 0; self.readyState = 4; self._emit('readystatechange'); self._emit('error', { error: err }); self._emit('loadend');\n"
    "        });\n"
    "    }\n"
    "    abort() { if (this.readyState === 0) return; this._aborted = true; this.readyState = 0; this._emit('abort'); this._emit('loadend'); }\n"
    "    getResponseHeader(k) { if (this.readyState < 2) return null; return this._rh[String(k).toLowerCase()] || null; }\n"
    "    getAllResponseHeaders() { if (this.readyState < 2) return ''; return Object.keys(this._rh).map(function(k) { return k + ': ' + this._rh[k] + '\\r\\n'; }).join(''); }\n"
    "  }\n"
    "  XMLHttpRequest.UNSENT = 0; XMLHttpRequest.OPENED = 1; XMLHttpRequest.HEADERS_RECEIVED = 2; XMLHttpRequest.LOADING = 3; XMLHttpRequest.DONE = 4;\n"
    "  g.XMLHttpRequest = XMLHttpRequest;\n"
    "\n"
    "  if (!g.WebSocket) {\n"
    "    class WebSocket extends EventTarget {\n"
    "      constructor(url, protocols) {\n"
    "        super(); this.url = String(url); this.protocol = ''; this.readyState = 3;\n"
    "        this.bufferedAmount = 0; this.extensions = ''; this.binaryType = 'blob';\n"
    "        this.onopen = null; this.onmessage = null; this.onerror = null; this.onclose = null;\n"
    "        var self = this; queueMicrotask(function() {\n"
    "          var ev = new Event('error'); if (self.onerror) self.onerror(ev); self.dispatchEvent(ev);\n"
    "          var close = new Event('close'); if (self.onclose) self.onclose(close); self.dispatchEvent(close);\n"
    "        });\n"
    "      }\n"
    "      send() { throw new Error('WebSocket transport is not available in LiwusWeb'); }\n"
    "      close(code, reason) { this.readyState = 3; var ev = new Event('close'); ev.code = code || 1000; ev.reason = reason || ''; this.dispatchEvent(ev); }\n"
    "    }\n"
    "    WebSocket.CONNECTING = 0; WebSocket.OPEN = 1; WebSocket.CLOSING = 2; WebSocket.CLOSED = 3;\n"
    "    g.WebSocket = WebSocket;\n"
    "  }\n"
    "  if (!g.EventSource) {\n"
    "    class EventSource extends EventTarget {\n"
    "      constructor(url) { super(); this.url = String(url); this.readyState = 2; this.withCredentials = false; this.onopen = null; this.onmessage = null; this.onerror = null; }\n"
    "      close() { this.readyState = 2; }\n"
    "    }\n"
    "    EventSource.CONNECTING = 0; EventSource.OPEN = 1; EventSource.CLOSED = 2; g.EventSource = EventSource;\n"
    "  }\n"
    "  if (!g.BroadcastChannel) {\n"
    "    var __liwusChannels = Object.create(null);\n"
    "    class BroadcastChannel extends EventTarget {\n"
    "      constructor(name) { super(); this.name = String(name); this.onmessage = null; ( __liwusChannels[this.name] || (__liwusChannels[this.name] = []) ).push(this); }\n"
    "      postMessage(value) { var list = __liwusChannels[this.name] || []; for (var i = 0; i < list.length; i++) if (list[i] !== this) { var ev = new Event('message'); ev.data = value; if (list[i].onmessage) list[i].onmessage(ev); list[i].dispatchEvent(ev); } }\n"
    "      close() { var list = __liwusChannels[this.name] || []; var i = list.indexOf(this); if (i >= 0) list.splice(i, 1); }\n"
    "    }\n"
    "    g.BroadcastChannel = BroadcastChannel;\n"
    "  }\n"
    "\n"
    "  if (typeof Intl === 'undefined') g.Intl = {};\n"
    "  if (!Intl.DateTimeFormat) {\n"
    "    Intl.DateTimeFormat = function() {\n"
    "      return {\n"
    "        format: function(d) { return new Date(d).toISOString(); },\n"
    "        formatToParts: function(d) { return [{ type: 'literal', value: new Date(d).toISOString() }]; },\n"
    "        resolvedOptions: function() { return { locale: 'en-US', timeZone: 'UTC' }; }\n"
    "      };\n"
    "    };\n"
    "  }\n"
    "  if (!Intl.RelativeTimeFormat) {\n"
    "    Intl.RelativeTimeFormat = function() {\n"
    "      return {\n"
    "        format: function(val, unit) {\n"
    "          val = Number(val);\n"
    "          var u = String(unit).toLowerCase();\n"
    "          if (u.charAt(u.length - 1) !== 's' && Math.abs(val) !== 1) u += 's';\n"
    "          return val < 0 ? (-val + ' ' + u + ' ago') : (val + ' ' + u);\n"
    "        },\n"
    "        formatToParts: function(val, unit) {\n"
    "          return [{ type: 'literal', value: String(val) }, { type: 'unit', value: unit }];\n"
    "        },\n"
    "        resolvedOptions: function() { return { locale: 'en-US' }; }\n"
    "      };\n"
    "    };\n"
    "  }\n"
    "  if (!Intl.NumberFormat) {\n"
    "    Intl.NumberFormat = function() {\n"
    "      return {\n"
    "        format: function(n) { return String(n); },\n"
    "        resolvedOptions: function() { return { locale: 'en-US' }; }\n"
    "      };\n"
    "    };\n"
    "  }\n"
    "  if (!Intl.PluralRules) {\n"
    "    Intl.PluralRules = function() { return { select: function(n) { return Number(n) === 1 ? 'one' : 'other'; }, selectRange: function(a, b) { return Number(a) === 1 && Number(b) === 1 ? 'one' : 'other'; }, resolvedOptions: function() { return { locale: 'en-US', type: 'cardinal' }; } }; };\n"
    "  }\n"
    "  if (!Intl.ListFormat) {\n"
    "    Intl.ListFormat = function(locale, options) { options = options || {}; var conjunction = options.type === 'disjunction' ? 'or' : 'and'; return { format: function(items) { var a = Array.from(items || []).map(String); if (a.length < 2) return a.join(''); if (a.length === 2) return a[0] + ' ' + conjunction + ' ' + a[1]; return a.slice(0, -1).join(', ') + ', ' + conjunction + ' ' + a[a.length - 1]; }, formatToParts: function(items) { return [{ type: 'literal', value: this.format(items) }]; }, resolvedOptions: function() { return { locale: 'en-US', type: options.type || 'conjunction', style: options.style || 'long' }; } }; };\n"
    "  }\n"
    "  if (!Intl.Collator) {\n"
    "    Intl.Collator = function() { return { compare: function(a, b) { a = String(a); b = String(b); return a < b ? -1 : (a > b ? 1 : 0); }, resolvedOptions: function() { return { locale: 'en-US' }; } }; };\n"
    "  }\n"
    "\n"
    "  if (!g.CustomEvent) g.CustomEvent = function CustomEvent(t, init) { init = init || {}; var e = new Event(t, init); e.detail = init.detail !== undefined ? init.detail : null; return e; };\n"
    "  if (!g.MouseEvent) g.MouseEvent = Event;\n"
    "  if (!g.KeyboardEvent) g.KeyboardEvent = Event;\n"
    "  if (!g.FocusEvent) g.FocusEvent = Event;\n"
    "  if (!g.UIEvent) g.UIEvent = Event;\n"
    "})();\n";

static const char *qjs_url_authority_end(const char *url) {
    const char *p = url;
    while (*p && *p != '/' && *p != '?' && *p != '#') ++p;
    return p;
}

static size_t qjs_url_path_length(const char *url) {
    const char *p = url;
    while (*p && *p != '?' && *p != '#') ++p;
    return (size_t)(p - url);
}

static int qjs_install_web_globals(JSContext *ctx, const char *href) {
    if (href && href[0]) {
        size_t n = strlen(href);
        if (n >= sizeof(qjs_location_href)) return -1;
        memcpy(qjs_location_href, href, n + 1);
    } else {
        memcpy(qjs_location_href, "about:blank", sizeof("about:blank"));
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue global_alias = JS_DupValue(ctx, global);
    if (JS_SetPropertyStr(ctx, global, "window", global_alias) < 0 ||
        JS_SetPropertyStr(ctx, global, "self", JS_DupValue(ctx, global)) < 0) {
        JS_FreeValue(ctx, global);
        return -1;
    }

    JSValue navigator = JS_NewObject(ctx);
    if (JS_IsException(navigator) ||
        qjs_set_string(ctx, navigator, "userAgent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36") < 0 ||
        qjs_set_string(ctx, navigator, "platform", "LIWUS") < 0 ||
        qjs_set_string(ctx, navigator, "language", "en-US") < 0 ||
        JS_SetPropertyStr(ctx, navigator, "onLine", JS_TRUE) < 0 ||
        JS_SetPropertyStr(ctx, global, "navigator", navigator) < 0) {
        JS_FreeValue(ctx, navigator);
        JS_FreeValue(ctx, global);
        return -1;
    }

    JSValue location = JS_NewObject(ctx);
    if (JS_IsException(location)) { JS_FreeValue(ctx, global); return -1; }
    const char *href_begin = qjs_location_href;
    const char *colon = strchr(href_begin, ':');
    const char *authority = NULL;
    const char *authority_end = NULL;
    if (colon && colon[1] == '/' && colon[2] == '/') {
        authority = colon + 3;
        authority_end = qjs_url_authority_end(authority);
    }
    char protocol[24] = "", host[256] = "", hostname[256] = "", port[16] = "";
    char origin[320] = "null", pathname[QJS_FETCH_URL_BYTES] = "";
    char search[QJS_FETCH_URL_BYTES] = "", hash[QJS_FETCH_URL_BYTES] = "";
    if (colon && (size_t)(colon - href_begin) < sizeof(protocol) - 1) {
        size_t n = (size_t)(colon - href_begin);
        memcpy(protocol, href_begin, n);
        protocol[n] = ':';
        protocol[n + 1] = '\0';
    }
    if (authority) {
        const char *host_start = authority;
        for (const char *at = authority; at < authority_end; ++at)
            if (*at == '@') host_start = at + 1;
        size_t raw_host_length = (size_t)(authority_end - host_start);
        if (raw_host_length >= sizeof(host)) raw_host_length = sizeof(host) - 1;
        memcpy(host, host_start, raw_host_length); host[raw_host_length] = '\0';
        size_t host_length = raw_host_length;
        const char *port_start = NULL;
        if (host[0] == '[') {
            const char *close = strchr(host, ']');
            if (close) {
                host_length = (size_t)(close - host) + 1;
                if (close[1] == ':') port_start = close + 2;
            }
        } else {
            const char *colon_in_host = strrchr(host, ':');
            if (colon_in_host) {
                host_length = (size_t)(colon_in_host - host);
                port_start = colon_in_host + 1;
            }
        }
        if (host_length >= sizeof(hostname)) host_length = sizeof(hostname) - 1;
        memcpy(hostname, host, host_length); hostname[host_length] = '\0';
        if (port_start && *port_start) {
            size_t port_length = raw_host_length -
                (size_t)(port_start - host);
            if (port_length >= sizeof(port)) port_length = sizeof(port) - 1;
            memcpy(port, port_start, port_length); port[port_length] = '\0';
        }
        size_t protocol_length = strlen(protocol);
        size_t host_property_length = strlen(host);
        if (protocol_length + 2 + host_property_length < sizeof(origin)) {
            memcpy(origin, protocol, protocol_length);
            memcpy(origin + protocol_length, "//", 2);
            memcpy(origin + protocol_length + 2, host, host_property_length + 1);
        }
    }
    const char *path_start = authority ? authority_end :
        (colon ? colon + 1 : href_begin);
    if (authority && (*path_start == '\0' || *path_start == '?' || *path_start == '#'))
        strcpy(pathname, "/");
    else {
        size_t n = qjs_url_path_length(path_start);
        if (n >= sizeof(pathname)) n = sizeof(pathname) - 1;
        memcpy(pathname, path_start, n); pathname[n] = '\0';
    }
    const char *query = strchr(path_start, '?');
    const char *fragment = strchr(path_start, '#');
    if (query && (!fragment || query < fragment)) {
        size_t n = (size_t)((fragment ? fragment : path_start + strlen(path_start)) - query);
        if (n >= sizeof(search)) n = sizeof(search) - 1;
        memcpy(search, query, n); search[n] = '\0';
    }
    if (fragment) {
        size_t n = strlen(fragment);
        if (n >= sizeof(hash)) n = sizeof(hash) - 1;
        memcpy(hash, fragment, n); hash[n] = '\0';
    }
    if (qjs_set_string(ctx, location, "href", qjs_location_href) < 0 ||
        qjs_set_string(ctx, location, "origin", origin) < 0 ||
        qjs_set_string(ctx, location, "protocol", protocol) < 0 ||
        qjs_set_string(ctx, location, "host", host) < 0 ||
        qjs_set_string(ctx, location, "hostname", hostname) < 0 ||
        qjs_set_string(ctx, location, "port", port) < 0 ||
        qjs_set_string(ctx, location, "pathname", pathname) < 0 ||
        qjs_set_string(ctx, location, "search", search) < 0 ||
        qjs_set_string(ctx, location, "hash", hash) < 0 ||
        JS_SetPropertyStr(ctx, global, "location", JS_DupValue(ctx, location)) < 0) {
        JS_FreeValue(ctx, location);
        JS_FreeValue(ctx, global);
        return -1;
    }
    if (qjs_dom_document_object_valid) {
        if (qjs_set_string(ctx, qjs_dom_document_object, "URL", qjs_location_href) < 0 ||
            qjs_set_string(ctx, qjs_dom_document_object, "documentURI", qjs_location_href) < 0 ||
            qjs_set_string(ctx, qjs_dom_document_object, "baseURI", qjs_location_href) < 0 ||
            JS_SetPropertyStr(ctx, qjs_dom_document_object, "location",
                              JS_DupValue(ctx, location)) < 0) {
            JS_FreeValue(ctx, location);
            JS_FreeValue(ctx, global);
            return -1;
        }
    }
    JSValue bootstrap_result = JS_Eval(ctx, qjs_web_env_bootstrap,
                                       sizeof(qjs_web_env_bootstrap) - 1,
                                       "<liwus-web-bootstrap>",
                                       JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(bootstrap_result)) {
        print_exception(ctx);
        JS_FreeValue(ctx, bootstrap_result);
    } else {
        JS_FreeValue(ctx, bootstrap_result);
    }
    JS_FreeValue(ctx, location);
    JS_FreeValue(ctx, global);
    return 0;
}

static JSValue qjs_dom_decode_slice(JSContext *ctx, liw_web_slice_t value,
                                   int needs_decode) {
    if (!needs_decode) return JS_NewStringLen(ctx, value.data, value.length);
    size_t decoded_length = 0;
    if (liw_webdom_decode_entities(value.data, value.length, NULL, 0,
                                   &decoded_length) != LIW_WEBDOM_OK ||
        decoded_length > QJS_MAX_DOM_TEXT_BYTES)
        return JS_ThrowRangeError(ctx, "DOM text limit exceeded");
    char *decoded = (char *)malloc(decoded_length + 1);
    if (!decoded) return JS_ThrowOutOfMemory(ctx);
    size_t actual_length = 0;
    int rc = liw_webdom_decode_entities(value.data, value.length, decoded,
        decoded_length + 1, &actual_length);
    JSValue result = rc == LIW_WEBDOM_OK
        ? JS_NewStringLen(ctx, decoded, actual_length)
        : JS_ThrowInternalError(ctx, "cannot decode HTML text");
    free(decoded);
    return result;
}

typedef enum {
    QJS_SELECTOR_TAG,
    QJS_SELECTOR_ID,
    QJS_SELECTOR_CLASS,
    QJS_SELECTOR_ATTRIBUTE,
    QJS_SELECTOR_ATTRIBUTE_VALUE,
    QJS_SELECTOR_ATTRIBUTE_WORD,
    QJS_SELECTOR_ATTRIBUTE_DASH,
    QJS_SELECTOR_ATTRIBUTE_PREFIX,
    QJS_SELECTOR_ATTRIBUTE_SUFFIX,
    QJS_SELECTOR_ATTRIBUTE_SUBSTRING,
    QJS_SELECTOR_ALL,
} qjs_selector_kind_t;

typedef struct {
    qjs_selector_kind_t kind;
    const char *name;
    size_t name_length;
    const char *value;
    size_t value_length;
    uint8_t value_case_insensitive;
} qjs_selector_t;

static void *qjs_malloc(JSMallocState *state, size_t size) {
    if (size == 0 || size > SIZE_MAX - sizeof(qjs_alloc_header_t) ||
        state->malloc_size > state->malloc_limit ||
        size + sizeof(qjs_alloc_header_t) >
            state->malloc_limit - state->malloc_size) {
        return NULL;
    }

    qjs_alloc_header_t *header =
        (qjs_alloc_header_t *)malloc(sizeof(*header) + size);
    if (!header) return NULL;
    header->size = size;
    state->malloc_count++;
    state->malloc_size += size + sizeof(*header);
    return header + 1;
}

static void qjs_free(JSMallocState *state, void *ptr) {
    if (!ptr) return;
    qjs_alloc_header_t *header = (qjs_alloc_header_t *)ptr - 1;
    if (state->malloc_count) state->malloc_count--;
    size_t accounted = header->size + sizeof(*header);
    if (accounted <= state->malloc_size) state->malloc_size -= accounted;
    free(header);
}

static void *qjs_realloc(JSMallocState *state, void *ptr, size_t size) {
    if (!ptr) return qjs_malloc(state, size);
    if (size == 0) {
        qjs_free(state, ptr);
        return NULL;
    }

    qjs_alloc_header_t *old_header = (qjs_alloc_header_t *)ptr - 1;
    size_t old_size = old_header->size;
    if (size > SIZE_MAX - sizeof(*old_header) ||
        state->malloc_size > state->malloc_limit ||
        (size > old_size &&
         size - old_size > state->malloc_limit - state->malloc_size)) {
        return NULL;
    }

    qjs_alloc_header_t *new_header =
        (qjs_alloc_header_t *)realloc(old_header, sizeof(*new_header) + size);
    if (!new_header) return NULL;
    new_header->size = size;
    if (size >= old_size) state->malloc_size += size - old_size;
    else state->malloc_size -= old_size - size;
    return new_header + 1;
}

static size_t qjs_malloc_usable_size(const void *ptr) {
    if (!ptr) return 0;
    const qjs_alloc_header_t *header = (const qjs_alloc_header_t *)ptr - 1;
    /* QuickJS passes the start of its raw allocation here and expects the
     * usable payload size. The wrapper header is outside that allocation. */
    return header->size;
}

static const JSMallocFunctions qjs_allocator = {
    .js_malloc = qjs_malloc,
    .js_free = qjs_free,
    .js_realloc = qjs_realloc,
    .js_malloc_usable_size = qjs_malloc_usable_size,
};

static JSValue qjs_console_log(JSContext *ctx, JSValueConst this_value,
                               int argc, JSValueConst *argv) {
    (void)this_value;
    FILE *stream = qjs_dump_html ? stderr : stdout;
    for (int i = 0; i < argc; ++i) {
        if (i && fputc(' ', stream) == EOF)
            return JS_ThrowInternalError(ctx, "console output failed");
        size_t length = 0;
        const char *text = JS_ToCStringLen(ctx, &length, argv[i]);
        if (!text) return JS_EXCEPTION;
        size_t written = fwrite(text, 1, length, stream);
        JS_FreeCString(ctx, text);
        if (written != length)
            return JS_ThrowInternalError(ctx, "console output failed");
    }
    if (fputc('\n', stream) == EOF)
        return JS_ThrowInternalError(ctx, "console output failed");
    return JS_UNDEFINED;
}

static JSValue qjs_microtask_job(JSContext *ctx, int argc,
                                 JSValueConst *argv) {
    if (argc != 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "queueMicrotask callback is not callable");
    JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
    if (JS_IsException(result)) return result;
    JS_FreeValue(ctx, result);
    return JS_UNDEFINED;
}

static JSValue qjs_queue_microtask(JSContext *ctx, JSValueConst this_value,
                                   int argc, JSValueConst *argv) {
    (void)this_value;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "queueMicrotask requires a function");
    if (JS_EnqueueJob(ctx, qjs_microtask_job, 1, argv) < 0)
        return JS_ThrowOutOfMemory(ctx);
    return JS_UNDEFINED;
}

static JSValue qjs_set_timer(JSContext *ctx, JSValueConst this_value,
                             int argc, JSValueConst *argv, int repeat) {
    (void)this_value;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "timer callback must be a function");

    int64_t delay_ms = 0;
    if (argc > 1 && JS_ToInt64(ctx, &delay_ms, argv[1]) < 0)
        return JS_EXCEPTION;
    if (delay_ms < 0) delay_ms = 0;
    if (delay_ms > 60000) delay_ms = 60000;
    uint64_t delay_ticks = ((uint64_t)delay_ms + 9U) / 10U;
    uint64_t interval_ticks = repeat ? (delay_ticks ? delay_ticks : 1U) : 0;

    size_t slot = QJS_MAX_TIMERS;
    for (size_t i = 0; i < QJS_MAX_TIMERS; ++i) {
        if (!qjs_timers[i].active) { slot = i; break; }
    }
    if (slot == QJS_MAX_TIMERS)
        return JS_ThrowRangeError(ctx, "timer limit reached");

    uint32_t id = qjs_next_timer_id++;
    if (!id) id = qjs_next_timer_id++;
    qjs_timer_t *timer = &qjs_timers[slot];
    timer->id = id;
    timer->due_ticks = liw_timer_ticks() + delay_ticks;
    timer->interval_ticks = interval_ticks;
    timer->callback = JS_DupValue(ctx, argv[0]);
    timer->active = 1;
    return JS_NewUint32(ctx, id);
}

static JSValue qjs_set_timeout(JSContext *ctx, JSValueConst this_value,
                               int argc, JSValueConst *argv) {
    return qjs_set_timer(ctx, this_value, argc, argv, 0);
}

static JSValue qjs_set_interval(JSContext *ctx, JSValueConst this_value,
                                int argc, JSValueConst *argv) {
    return qjs_set_timer(ctx, this_value, argc, argv, 1);
}

static JSValue qjs_clear_timer(JSContext *ctx, JSValueConst this_value,
                               int argc, JSValueConst *argv) {
    (void)this_value;
    if (argc < 1) return JS_UNDEFINED;
    uint32_t id = 0;
    if (JS_ToUint32(ctx, &id, argv[0]) < 0) return JS_EXCEPTION;
    for (size_t i = 0; i < QJS_MAX_TIMERS; ++i) {
        qjs_timer_t *timer = &qjs_timers[i];
        if (!timer->active || timer->id != id) continue;
        JS_FreeValue(ctx, timer->callback);
        timer->callback = JS_UNDEFINED;
        timer->active = 0;
        break;
    }
    return JS_UNDEFINED;
}

static void qjs_drain_microtasks(JSContext *ctx) {
    JSRuntime *runtime = JS_GetRuntime(ctx);
    for (unsigned job = 0; job < QJS_MICROTASK_CHECKPOINT_LIMIT; ++job) {
        JSContext *job_ctx = NULL;
        int job_rc = JS_ExecutePendingJob(runtime, &job_ctx);
        if (job_rc == 0) return;
        if (job_rc < 0) {
            /* Report one rejected job, then continue with the remaining queue. */
            print_exception(job_ctx ? job_ctx : ctx);
        }
    }
    (void)fputs("qjs: microtask checkpoint limit reached\n", stderr);
}

static void qjs_run_bootstrap_timers(JSContext *ctx) {
    uint64_t start_ticks = liw_timer_ticks();
    unsigned callbacks = 0;
    while (callbacks < QJS_TIMER_CALLBACK_LIMIT) {
        size_t selected = QJS_MAX_TIMERS;
        uint64_t earliest = UINT64_MAX;
        for (size_t i = 0; i < QJS_MAX_TIMERS; ++i) {
            if (qjs_timers[i].active && qjs_timers[i].due_ticks < earliest) {
                selected = i;
                earliest = qjs_timers[i].due_ticks;
            }
        }
        if (selected == QJS_MAX_TIMERS) return;

        uint64_t now = liw_timer_ticks();
        uint64_t elapsed = now - start_ticks;
        if (elapsed >= QJS_TIMER_BOOTSTRAP_WINDOW_TICKS) break;
        if (earliest > now) {
            uint64_t wait_ticks = earliest - now;
            uint64_t remaining = QJS_TIMER_BOOTSTRAP_WINDOW_TICKS - elapsed;
            if (wait_ticks > remaining) break;
            (void)liw_sleep_ms((uint32_t)(wait_ticks * 10U));
            now = liw_timer_ticks();
        }

        qjs_timer_t *timer = &qjs_timers[selected];
        JSValue callback = JS_DupValue(ctx, timer->callback);
        if (timer->interval_ticks) {
            timer->due_ticks = now + timer->interval_ticks;
        } else {
            JS_FreeValue(ctx, timer->callback);
            timer->callback = JS_UNDEFINED;
            timer->active = 0;
        }
        JSValue result = JS_Call(ctx, callback, JS_UNDEFINED, 0, NULL);
        JS_FreeValue(ctx, callback);
        if (JS_IsException(result)) print_exception(ctx);
        JS_FreeValue(ctx, result);
        qjs_drain_microtasks(ctx);
        callbacks++;
    }
    if (callbacks == QJS_TIMER_CALLBACK_LIMIT)
        (void)fputs("qjs: timer callback limit reached\n", stderr);
}

static void qjs_clear_all_timers(JSContext *ctx) {
    for (size_t i = 0; i < QJS_MAX_TIMERS; ++i) {
        if (!qjs_timers[i].active) continue;
        JS_FreeValue(ctx, qjs_timers[i].callback);
        qjs_timers[i].callback = JS_UNDEFINED;
        qjs_timers[i].active = 0;
    }
}

static int qjs_run_due_timer(JSContext *ctx) {
    uint64_t now = liw_timer_ticks();
    size_t selected = QJS_MAX_TIMERS;
    uint64_t earliest = UINT64_MAX;
    for (size_t i = 0; i < QJS_MAX_TIMERS; ++i) {
        if (qjs_timers[i].active && qjs_timers[i].due_ticks <= now &&
            qjs_timers[i].due_ticks < earliest) {
            selected = i;
            earliest = qjs_timers[i].due_ticks;
        }
    }
    if (selected == QJS_MAX_TIMERS) return 0;

    qjs_timer_t *timer = &qjs_timers[selected];
    JSValue callback = JS_DupValue(ctx, timer->callback);
    if (timer->interval_ticks) {
        timer->due_ticks = now + timer->interval_ticks;
    } else {
        JS_FreeValue(ctx, timer->callback);
        timer->callback = JS_UNDEFINED;
        timer->active = 0;
    }
    JSValue result = JS_Call(ctx, callback, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(ctx, callback);
    if (JS_IsException(result)) print_exception(ctx);
    JS_FreeValue(ctx, result);
    qjs_drain_microtasks(ctx);
    return 1;
}

static int qjs_publish_dom(const char *output_path, int notify_fd) {
    size_t html_length = 0;
    if (liw_webdom_serialize(qjs_dom_document, NULL, 0, &html_length) !=
            LIW_WEBDOM_OK || html_length > LIW_WEBDOM_MAX_SOURCE)
        return -1;
    char *html = (char *)malloc(html_length + 1);
    if (!html) return -1;
    int rc = liw_webdom_serialize(qjs_dom_document, html, html_length + 1,
                                  &html_length);
    if (rc != LIW_WEBDOM_OK) { free(html); return -1; }

    int fd = liw_open(output_path, LIW_O_WRONLY | LIW_O_CREAT | LIW_O_TRUNC);
    if (fd < 0) { free(html); return -1; }
    long written = liw_write_all(fd, html, html_length);
    (void)liw_close(fd);
    free(html);
    if (written != 0) return -1;
    /* Notifications are coalescible: if the bounded channel is full, the
     * consumer can still read the newest complete DOM snapshot. */
    (void)liw_write(notify_fd, "D", 1);
    return 0;
}

static int qjs_dispatch_runtime_command(JSContext *ctx, char *command) {
    if (strcmp(command, "quit") == 0) return 1;
    if (strcmp(command, "ack") == 0) return 3;
    char *space = strchr(command, ' ');
    if (!space) return 0;
    *space++ = '\0';
    char *value = NULL;
    if (strcmp(command, "input") == 0) {
        char *value_sep = strchr(space, ' ');
        if (!value_sep) return 0;
        *value_sep++ = '\0';
        value = value_sep;
    }
    if (strcmp(command, "click") != 0 && strcmp(command, "input") != 0 &&
        strcmp(command, "change") != 0 && strcmp(command, "keydown") != 0 &&
        strcmp(command, "keyup") != 0) return 0;
    (void)qjs_dispatch_native_event(ctx, space, command, value);
    qjs_drain_microtasks(ctx);
    /* The native text area is already showing each edit. Do not rebuild its
     * LVGL tree for every key; publish after change/click or a later timer. */
    if (strcmp(command, "input") == 0) return 4;
    return 2;
}

static void qjs_send_fetch_requests(void) {
    if (qjs_fetch_request_fd < 0) return;
    for (size_t i = 0; i < QJS_MAX_PENDING_FETCHES; ++i) {
        qjs_fetch_t *fetch = &qjs_fetches[i];
        if (!fetch->active || fetch->sent) continue;
        char body_hex[QJS_FETCH_BODY_BYTES * 2U + 1U];
        static const char digits[] = "0123456789abcdef";
        for (size_t bi = 0; bi < fetch->body_length; ++bi) {
            unsigned char c = (unsigned char)fetch->body[bi];
            body_hex[bi * 2U] = digits[c >> 4];
            body_hex[bi * 2U + 1U] = digits[c & 15U];
        }
        body_hex[fetch->body_length * 2U] = '\0';
        char request[QJS_FETCH_URL_BYTES + QJS_FETCH_HEADERS_BYTES +
                     QJS_FETCH_BODY_BYTES * 2U + 96U];
        int length = snprintf(request, sizeof(request), "%u %u %s\t%s\t%s\t%s\n",
                              qjs_fetch_generation, fetch->id, fetch->url,
                              fetch->method, fetch->headers, body_hex);
        if (length <= 0 || (size_t)length >= sizeof(request)) continue;
        long written = liw_write_all(qjs_fetch_request_fd, request,
                                     (size_t)length);
        if (written == 0) {
            fetch->sent = 1;
            break; /* Keep the bounded IPC channel from accumulating bursts. */
        }
    }
}

static int qjs_resolve_fetch_response(JSContext *ctx, char *line) {
    char *first = strchr(line, ' ');
    if (!first) return 0;
    *first++ = '\0';
    char *second = strchr(first, ' ');
    if (!second) return 0;
    *second++ = '\0';
    char *third = strchr(second, ' ');
    if (!third) return 0;
    *third++ = '\0';
    char *content_type = strchr(third, ' ');
    char *response_headers = NULL;
    if (content_type) {
        *content_type++ = '\0';
        response_headers = strchr(content_type, '\t');
        if (response_headers) *response_headers++ = '\0';
    } else {
        content_type = "";
    }
    uint32_t generation = (uint32_t)strtoul(line, NULL, 10);
    uint32_t id = (uint32_t)strtoul(first, NULL, 10);
    int status = atoi(second);
    char *path_end = strpbrk(third, "\r\n");
    if (path_end) *path_end = '\0';
    char *type_end = strpbrk(content_type, "\r\n");
    if (type_end) *type_end = '\0';
    if (response_headers) {
        char *headers_end = strpbrk(response_headers, "\r\n");
        if (headers_end) *headers_end = '\0';
    }
    if (generation != qjs_fetch_generation) return 0;
    qjs_fetch_t *fetch = NULL;
    for (size_t i = 0; i < QJS_MAX_PENDING_FETCHES; ++i) {
        if (qjs_fetches[i].active && qjs_fetches[i].id == id) {
            fetch = &qjs_fetches[i];
            break;
        }
    }
    if (!fetch) return 0;

    JSValue settlement = JS_UNDEFINED;
    if (status <= 0 || !third[0]) {
        settlement = JS_NewString(ctx, "fetch network request failed");
        JSValue args[1] = {settlement};
        JSValue result = JS_Call(ctx, fetch->reject, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, result);
    } else {
        size_t body_length = 0;
        char *body = read_bounded_file(third, QJS_MAX_FETCH_BYTES,
                                       &body_length);
        (void)liw_unlink(third);
        if (!body) {
            settlement = JS_NewString(ctx, "fetch response unavailable");
            JSValue args[1] = {settlement};
            JSValue result = JS_Call(ctx, fetch->reject, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
        } else {
            JSValue body_value = JS_NewStringLen(ctx, body, body_length);
            free(body);
            JSValue global = JS_GetGlobalObject(ctx);
            JSValue factory = JS_GetPropertyStr(ctx, global,
                                                 "__liwusMakeResponse");
            JSValue headers = JS_NewObject(ctx);
            if (content_type[0]) {
                JS_SetPropertyStr(ctx, headers, "content-type",
                                  JS_NewString(ctx, content_type));
            }
            if (response_headers && response_headers[0]) {
                char *cursor = response_headers;
                while (*cursor) {
                    char *next = strchr(cursor, '|');
                    if (next) *next = '\0';
                    char *colon = strchr(cursor, ':');
                    if (colon && colon > cursor) {
                        *colon++ = '\0';
                        while (*colon == ' ' || *colon == '\t') colon++;
                        JS_SetPropertyStr(ctx, headers, cursor,
                                          JS_NewString(ctx, colon));
                    }
                    if (!next) break;
                    cursor = next + 1;
                }
            }
            JSValue args[4] = {JS_NewInt32(ctx, status), body_value,
                               JS_NewString(ctx, fetch->url), headers};
            settlement = JS_Call(ctx, factory, global, 4, args);
            JS_FreeValue(ctx, args[0]);
            JS_FreeValue(ctx, body_value);
            JS_FreeValue(ctx, args[2]);
            JS_FreeValue(ctx, headers);
            JS_FreeValue(ctx, factory);
            JS_FreeValue(ctx, global);
            if (JS_IsException(settlement)) {
                JSValue exception = JS_GetException(ctx);
                JSValue reject_args[1] = {exception};
                JSValue rejected = JS_Call(ctx, fetch->reject, JS_UNDEFINED,
                                           1, reject_args);
                JS_FreeValue(ctx, rejected);
                JS_FreeValue(ctx, exception);
            } else {
                JSValue resolve_args[1] = {settlement};
                JSValue result = JS_Call(ctx, fetch->resolve, JS_UNDEFINED,
                                         1, resolve_args);
                if (JS_IsException(result)) print_exception(ctx);
                JS_FreeValue(ctx, result);
            }
        }
    }
    JS_FreeValue(ctx, settlement);
    JS_FreeValue(ctx, fetch->resolve);
    JS_FreeValue(ctx, fetch->reject);
    fetch->resolve = JS_UNDEFINED;
    fetch->reject = JS_UNDEFINED;
    fetch->active = 0;
    fetch->sent = 0;
    qjs_drain_microtasks(ctx);
    return 1;
}

static int qjs_take_fetch_response_line(char *line, size_t capacity) {
    if (!line || capacity == 0 || qjs_fetch_response_fd < 0) return -1;
    char *newline = memchr(qjs_fetch_response_buffer, '\n',
                           qjs_fetch_response_length);
    if (!newline) {
        if (qjs_fetch_response_length >= sizeof(qjs_fetch_response_buffer) - 1U)
            return -1;
        long count = liw_read(qjs_fetch_response_fd,
            qjs_fetch_response_buffer + qjs_fetch_response_length,
            sizeof(qjs_fetch_response_buffer) - qjs_fetch_response_length - 1U);
        if (count <= 0) return 0;
        qjs_fetch_response_length += (size_t)count;
        qjs_fetch_response_buffer[qjs_fetch_response_length] = '\0';
        newline = memchr(qjs_fetch_response_buffer, '\n',
                         qjs_fetch_response_length);
        if (!newline) return 0;
    }
    size_t length = (size_t)(newline - qjs_fetch_response_buffer);
    if (length + 1U > capacity) return -1;
    memcpy(line, qjs_fetch_response_buffer, length);
    line[length] = '\0';
    size_t consumed = length + 1U;
    qjs_fetch_response_length -= consumed;
    memmove(qjs_fetch_response_buffer,
            qjs_fetch_response_buffer + consumed,
            qjs_fetch_response_length);
    qjs_fetch_response_buffer[qjs_fetch_response_length] = '\0';
    return 1;
}

static int qjs_defer_fetch_response(const char *line) {
    if (!line || qjs_deferred_fetch_count >= QJS_DEFERRED_FETCH_RESPONSES)
        return -1;
    size_t slot = (qjs_deferred_fetch_head + qjs_deferred_fetch_count) %
                  QJS_DEFERRED_FETCH_RESPONSES;
    size_t length = strlen(line);
    if (length >= QJS_FETCH_RESPONSE_LINE_BYTES) return -1;
    memcpy(qjs_deferred_fetch_responses[slot], line, length + 1U);
    qjs_deferred_fetch_count++;
    return 0;
}

static int qjs_poll_fetch_responses(JSContext *ctx) {
    if (qjs_fetch_response_fd < 0) return 0;
    int changed = 0;
    char line[QJS_FETCH_RESPONSE_LINE_BYTES];
    while (qjs_deferred_fetch_count) {
        size_t slot = qjs_deferred_fetch_head;
        qjs_deferred_fetch_head = (qjs_deferred_fetch_head + 1U) %
                                  QJS_DEFERRED_FETCH_RESPONSES;
        qjs_deferred_fetch_count--;
        changed |= qjs_resolve_fetch_response(
            ctx, qjs_deferred_fetch_responses[slot]);
    }
    for (;;) {
        int rc = qjs_take_fetch_response_line(line, sizeof(line));
        if (rc <= 0) break;
        changed |= qjs_resolve_fetch_response(ctx, line);
    }
    return changed;
}

static char *qjs_module_normalize(JSContext *ctx, const char *base_name,
                                  const char *module_name, void *opaque) {
    (void)opaque;
    char normalized[QJS_FETCH_URL_BYTES];
    qjs_import_map_entry_t entries[QJS_IMPORT_MAP_MAX_ENTRIES];
    for (size_t i = 0; i < qjs_import_map_count; ++i) {
        entries[i].specifier = qjs_import_map_specifiers[i];
        entries[i].address = qjs_import_map_addresses[i];
    }
    if (qjs_resolve_module_url_with_import_map(base_name, module_name,
            entries, qjs_import_map_count, normalized,
            sizeof(normalized)) != 0) {
        JS_ThrowTypeError(ctx, "unsupported or invalid module URL");
        return NULL;
    }
    size_t length = strlen(normalized);
    char *copy = js_malloc(ctx, length + 1U);
    if (copy) memcpy(copy, normalized, length + 1U);
    return copy;
}

static int qjs_parse_fetch_response_line(const char *line, uint32_t *generation,
                                         uint32_t *request_id, int *status,
                                         char *path, size_t path_capacity) {
    if (!line || !generation || !request_id || !status || !path ||
        !path_capacity) return -1;
    char copy[QJS_FETCH_RESPONSE_LINE_BYTES];
    size_t length = strlen(line);
    if (length >= sizeof(copy)) return -1;
    memcpy(copy, line, length + 1U);
    char *first = strchr(copy, ' ');
    if (!first) return -1;
    *first++ = '\0';
    char *second = strchr(first, ' ');
    if (!second) return -1;
    *second++ = '\0';
    char *third = strchr(second, ' ');
    if (!third) return -1;
    *third++ = '\0';
    size_t path_len = strlen(third);
    while (path_len && (third[path_len - 1U] == '\r' ||
                        third[path_len - 1U] == '\n'))
        third[--path_len] = '\0';
    if (path_len >= path_capacity) return -1;
    *generation = (uint32_t)strtoul(copy, NULL, 10);
    *request_id = (uint32_t)strtoul(first, NULL, 10);
    *status = atoi(second);
    memcpy(path, third, path_len + 1U);
    return 0;
}

static JSModuleDef *qjs_load_browser_module(JSContext *ctx,
                                            const char *module_name,
                                            void *opaque) {
    (void)opaque;
    if (!qjs_persistent_page || qjs_fetch_request_fd < 0 ||
        qjs_fetch_response_fd < 0 || !module_name ||
        !strstr(module_name, "://")) {
        JS_ThrowReferenceError(ctx, "module URL cannot be resolved");
        return NULL;
    }
    if (qjs_module_count >= QJS_MAX_MODULES) {
        JS_ThrowRangeError(ctx, "page module count limit exceeded");
        return NULL;
    }
    uint32_t request_id = qjs_next_module_fetch_id++;
    if (request_id < QJS_MODULE_FETCH_ID_BASE) {
        qjs_next_module_fetch_id = QJS_MODULE_FETCH_ID_BASE + 1U;
        request_id = QJS_MODULE_FETCH_ID_BASE;
    }
    char request_url[QJS_FETCH_URL_BYTES];
    size_t module_name_length = strlen(module_name);
    if (module_name_length >= sizeof(request_url)) {
        JS_ThrowRangeError(ctx, "module URL too long");
        return NULL;
    }
    memcpy(request_url, module_name, module_name_length + 1U);
    char *fragment = strchr(request_url, '#');
    if (fragment) *fragment = '\0';
    char request[QJS_FETCH_URL_BYTES + 24U];
    int request_len = snprintf(request, sizeof(request), "%u %u %s\n",
                               qjs_fetch_generation, request_id, request_url);
    if (request_len <= 0 || (size_t)request_len >= sizeof(request) ||
        liw_write_all(qjs_fetch_request_fd, request, (size_t)request_len) != 0) {
        JS_ThrowInternalError(ctx, "cannot request imported module");
        return NULL;
    }

    char response[QJS_FETCH_RESPONSE_LINE_BYTES];
    char response_path[128];
    int response_status = 0;
    bool received = false;
    while (qjs_module_wait_ticks < QJS_MODULE_FETCH_TIMEOUT_TICKS) {
        qjs_module_wait_ticks++;
        int rc = qjs_take_fetch_response_line(response, sizeof(response));
        if (rc < 0) {
            JS_ThrowInternalError(ctx, "module response queue overflow");
            return NULL;
        }
        if (rc > 0) {
            uint32_t generation = 0, response_id = 0;
            int status = 0;
            char path[128];
            if (qjs_parse_fetch_response_line(response, &generation,
                    &response_id, &status, path, sizeof(path)) == 0 &&
                generation == qjs_fetch_generation) {
                if (response_id == request_id) {
                    if (status >= 200 && status < 300 && path[0]) {
                        memcpy(response_path, path, strlen(path) + 1U);
                        response_status = status;
                        received = true;
                        break;
                    }
                    if (path[0]) (void)liw_unlink(path);
                    JS_ThrowReferenceError(ctx, "imported module fetch failed");
                    return NULL;
                }
                if (qjs_defer_fetch_response(response) < 0) {
                    JS_ThrowInternalError(ctx, "deferred fetch queue full");
                    return NULL;
                }
            }
        } else {
            (void)liw_sleep_ms(10);
        }
    }
    if (!received) {
        JS_ThrowInternalError(ctx, "imported module fetch timed out");
        return NULL;
    }
    (void)response_status;
    size_t source_length = 0;
    char *source = read_bounded_file(response_path, 1024U * 1024U,
                                     &source_length);
    (void)liw_unlink(response_path);
    if (!source || source_length == 0 ||
        source_length > QJS_MAX_MODULE_BYTES - qjs_module_source_bytes) {
        free(source);
        JS_ThrowRangeError(ctx, "page module byte limit exceeded");
        return NULL;
    }
    qjs_module_count++;
    qjs_module_source_bytes += source_length;
    JSValue compiled = JS_Eval(ctx, source, source_length, module_name,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(source);
    if (JS_IsException(compiled)) return NULL;
    JSModuleDef *module = (JSModuleDef *)JS_VALUE_GET_PTR(compiled);
    JS_FreeValue(ctx, compiled);
    return module;
}

static int qjs_browser_event_loop(JSContext *ctx, const char *output_path) {
    int input_fd = liw_open("/ipc/webjs-in", LIW_O_RDONLY);
    int notify_fd = liw_open("/ipc/webjs-out", LIW_O_WRONLY);
    if (input_fd < 0 || notify_fd < 0) {
        if (input_fd >= 0) (void)liw_close(input_fd);
        if (notify_fd >= 0) (void)liw_close(notify_fd);
        return 1;
    }

    /* Publish the post-script DOM before announcing it. The desktop may poll
     * as soon as the notification arrives and must never observe an empty or
     * stale output file as the page's initial runtime snapshot. */
    if (qjs_publish_dom(output_path, notify_fd) < 0) {
        (void)liw_close(notify_fd);
        (void)liw_close(input_fd);
        return 1;
    }
    int running = 1;
    int awaiting_ack = 1;
    int deferred_dom_update = 0;
    char input[1024];
    size_t input_length = 0;
    while (running) {
        /* Bound each browser task independently. A page's first script must
         * not consume the interrupt budget for every later script/event. */
        qjs_reset_execution_budget();
        qjs_send_fetch_requests();
        long count = liw_read(input_fd, input + input_length,
                             sizeof(input) - input_length - 1);
        if (count > 0) input_length += (size_t)count;
        input[input_length] = '\0';

        char *line = input;
        char *newline;
        int changed = 0;
        while ((newline = memchr(line, '\n', input + input_length - line))) {
            *newline = '\0';
            int command_rc = qjs_dispatch_runtime_command(ctx, line);
            if (command_rc == 1) { running = 0; break; }
            if (command_rc == 2) {
                changed = 1;
                awaiting_ack = 1;
            } else if (command_rc == 4) {
                /* Event dispatched in-place; retain the live native input. */
            } else if (command_rc == 3) {
                awaiting_ack = 0;
            }
            line = newline + 1;
        }
        if (line != input) {
            input_length -= (size_t)(line - input);
            memmove(input, line, input_length);
        }
        if (input_length == sizeof(input) - 1) input_length = 0;
        if (!running) break;

        if (qjs_poll_fetch_responses(ctx)) {
            if (awaiting_ack) {
                /* Coalesce response DOM changes until the renderer releases
                 * the previous snapshot. */
                deferred_dom_update = 1;
            } else {
                changed = 1;
                awaiting_ack = 1;
            }
        }
        if (!awaiting_ack && deferred_dom_update) {
            changed = 1;
            awaiting_ack = 1;
            deferred_dom_update = 0;
        }

        if (!awaiting_ack && qjs_run_due_timer(ctx)) {
            changed = 1;
            awaiting_ack = 1;
        }
        if (changed && qjs_publish_dom(output_path, notify_fd) < 0)
            (void)fputs("qjs: browser DOM update could not be published\n", stderr);
        if (!changed) (void)liw_sleep_ms(10);
    }
    (void)liw_close(notify_fd);
    (void)liw_close(input_fd);
    return 0;
}

static qjs_dom_node_t *qjs_get_dom_node(JSContext *ctx, JSValueConst value) {
    qjs_dom_node_t *node = (qjs_dom_node_t *)JS_GetOpaque2(
        ctx, value, qjs_dom_node_class);
    if (node && node->document != qjs_dom_document) {
        JS_ThrowTypeError(ctx, "DOM node belongs to another document");
        return NULL;
    }
    return node;
}

static liw_web_node_id_t qjs_dom_receiver_node(JSContext *ctx,
                                               JSValueConst value) {
    qjs_dom_node_t *node = (qjs_dom_node_t *)JS_GetOpaque(
        value, qjs_dom_node_class);
    if (node) {
        if (node->document != qjs_dom_document) {
            JS_ThrowTypeError(ctx, "DOM node belongs to another document");
            return LIW_WEBDOM_NODE_NONE;
        }
        return node->node;
    }
    if (qjs_dom_document_object_valid &&
        JS_StrictEq(ctx, value, qjs_dom_document_object))
        return liw_webdom_root(qjs_dom_document);
    JS_ThrowTypeError(ctx, "incompatible DOM receiver");
    return LIW_WEBDOM_NODE_NONE;
}

static void qjs_dom_node_finalizer(JSRuntime *runtime, JSValue value) {
    (void)runtime;
    qjs_dom_node_t *node = (qjs_dom_node_t *)JS_GetOpaque(
        value, qjs_dom_node_class);
    if (node && !node->is_static) free(node);
}

static JSValue qjs_dom_wrap_node(JSContext *ctx, liw_web_node_id_t id) {
    if (id == LIW_WEBDOM_NODE_NONE) return JS_NULL;
    if (id == liw_webdom_root(qjs_dom_document) &&
        qjs_dom_document_object_valid)
        return JS_DupValue(ctx, qjs_dom_document_object);
    if (id >= LIW_WEBDOM_MAX_NODES) return JS_ThrowRangeError(ctx, "DOM node limit exceeded");
    if (qjs_dom_node_cache_valid[id])
        return JS_DupValue(ctx, qjs_dom_node_cache[id]);
    qjs_dom_node_t *node = (qjs_dom_node_t *)malloc(sizeof(*node));
    if (!node) return JS_ThrowOutOfMemory(ctx);
    node->document = qjs_dom_document;
    node->node = id;
    node->is_static = 0;
    JSValue object = JS_NewObjectClass(ctx, qjs_dom_node_class);
    if (JS_IsException(object)) {
        free(node);
        return object;
    }
    JS_SetOpaque(object, node);
    qjs_dom_node_cache[id] = JS_DupValue(ctx, object);
    qjs_dom_node_cache_valid[id] = 1;
    return object;
}

static JSValue qjs_dom_get_element_by_id(JSContext *ctx, JSValueConst this_value,
                                         int argc, JSValueConst *argv) {
    (void)this_value;
    if (argc < 1) return JS_ThrowTypeError(ctx, "id is required");
    const char *id = JS_ToCString(ctx, argv[0]);
    if (!id) return JS_EXCEPTION;
    liw_web_node_id_t node = liw_webdom_get_element_by_id(qjs_dom_document, id);
    JS_FreeCString(ctx, id);
    return qjs_dom_wrap_node(ctx, node);
}

static JSValue qjs_dom_create_element(JSContext *ctx, JSValueConst this_value,
                                      int argc, JSValueConst *argv) {
    (void)this_value;
    if (argc < 1) return JS_ThrowTypeError(ctx, "tag name is required");
    size_t length = 0;
    const char *name = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!name) return JS_EXCEPTION;
    liw_web_node_id_t node = liw_webdom_create_element(
        qjs_dom_document, name, length);
    JS_FreeCString(ctx, name);
    if (node == LIW_WEBDOM_NODE_NONE)
        return JS_ThrowOutOfMemory(ctx);
    return qjs_dom_wrap_node(ctx, node);
}

static JSValue qjs_dom_create_text_node(JSContext *ctx, JSValueConst this_value,
                                        int argc, JSValueConst *argv) {
    (void)this_value;
    if (argc < 1) return JS_ThrowTypeError(ctx, "text is required");
    size_t length = 0;
    const char *text = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!text) return JS_EXCEPTION;
    liw_web_node_id_t node = liw_webdom_create_text(
        qjs_dom_document, text, length);
    JS_FreeCString(ctx, text);
    if (node == LIW_WEBDOM_NODE_NONE)
        return JS_ThrowOutOfMemory(ctx);
    return qjs_dom_wrap_node(ctx, node);
}

static JSValue qjs_dom_create_comment(JSContext *ctx, JSValueConst this_value,
                                     int argc, JSValueConst *argv) {
    (void)this_value;
    if (argc < 1) return JS_ThrowTypeError(ctx, "comment data is required");
    size_t length = 0;
    const char *text = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!text) return JS_EXCEPTION;
    liw_web_node_id_t node = liw_webdom_create_comment(qjs_dom_document,
                                                       text, length);
    JS_FreeCString(ctx, text);
    if (node == LIW_WEBDOM_NODE_NONE) return JS_ThrowOutOfMemory(ctx);
    return qjs_dom_wrap_node(ctx, node);
}

static JSValue qjs_dom_create_document_fragment(JSContext *ctx,
                                                JSValueConst this_value,
                                                int argc, JSValueConst *argv) {
    (void)this_value; (void)argc; (void)argv;
    liw_web_node_id_t node = liw_webdom_create_fragment(qjs_dom_document);
    if (node == LIW_WEBDOM_NODE_NONE) return JS_ThrowOutOfMemory(ctx);
    return qjs_dom_wrap_node(ctx, node);
}

static JSValue qjs_dom_has_attribute(JSContext *ctx, JSValueConst this_value,
                                     int argc, JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "attribute name is required");
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    liw_web_slice_t value;
    int found = liw_webdom_get_attribute(node->document, node->node,
                                          name, &value) == LIW_WEBDOM_OK;
    JS_FreeCString(ctx, name);
    return JS_NewBool(ctx, found);
}

static JSValue qjs_dom_has_attribute_ns(JSContext *ctx,
                                        JSValueConst this_value, int argc,
                                        JSValueConst *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "namespace and name are required");
    return qjs_dom_has_attribute(ctx, this_value, 1, argv + 1);
}

static JSValue qjs_dom_toggle_attribute(JSContext *ctx, JSValueConst this_value,
                                        int argc, JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "attribute name is required");
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    liw_web_slice_t ignored;
    int present = liw_webdom_get_attribute(node->document, node->node,
                                            name, &ignored) == LIW_WEBDOM_OK;
    int force = -1;
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        force = JS_ToBool(ctx, argv[1]);
        if (force < 0) { JS_FreeCString(ctx, name); return JS_EXCEPTION; }
    }
    int add = force < 0 ? !present : force;
    int rc = add
        ? liw_webdom_set_attribute(node->document, node->node, name,
            strlen(name), "", 0)
        : liw_webdom_remove_attribute(node->document, node->node, name);
    JS_FreeCString(ctx, name);
    if (rc != LIW_WEBDOM_OK)
        return JS_ThrowTypeError(ctx, "cannot toggle attribute");
    return JS_NewBool(ctx, add);
}

static JSValue qjs_dom_clone_node(JSContext *ctx, JSValueConst this_value,
                                  int argc, JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    int deep = 0;
    if (argc > 0) {
        deep = JS_ToBool(ctx, argv[0]);
        if (deep < 0) return JS_EXCEPTION;
    }
    if (liw_webdom_node_type(node->document, node->node) == LIW_WEBDOM_DOCUMENT)
        return JS_ThrowTypeError(ctx, "document cloning is not supported");
    liw_web_node_id_t clone = liw_webdom_clone_node(node->document,
                                                    node->node, deep);
    if (clone == LIW_WEBDOM_NODE_NONE)
        return JS_ThrowRangeError(ctx, "DOM clone limit exceeded");
    return qjs_dom_wrap_node(ctx, clone);
}

static JSValue qjs_dom_outer_html(JSContext *ctx, JSValueConst this_value) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    size_t length = 0;
    int rc = liw_webdom_serialize_node(node->document, node->node,
                                       NULL, 0, &length);
    if (rc != LIW_WEBDOM_OK || length > QJS_MAX_DOM_TEXT_BYTES)
        return JS_ThrowRangeError(ctx, "DOM HTML limit exceeded");
    char *html = (char *)malloc(length + 1);
    if (!html) return JS_ThrowOutOfMemory(ctx);
    rc = liw_webdom_serialize_node(node->document, node->node,
                                    html, length + 1, &length);
    JSValue result = rc == LIW_WEBDOM_OK
        ? JS_NewStringLen(ctx, html, length)
        : JS_ThrowInternalError(ctx, "cannot serialize DOM node");
    free(html);
    return result;
}

static JSValue qjs_dom_set_outer_html(JSContext *ctx, JSValueConst this_value,
                                     JSValueConst value) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    size_t length = 0;
    const char *html = JS_ToCStringLen(ctx, &length, value);
    if (!html) return JS_EXCEPTION;
    int rc = liw_webdom_set_outer_html(node->document, node->node, html, length);
    JS_FreeCString(ctx, html);
    if (rc == LIW_WEBDOM_NO_MEMORY || rc == LIW_WEBDOM_LIMIT ||
        rc == LIW_WEBDOM_TOO_LARGE)
        return JS_ThrowRangeError(ctx, "DOM HTML limit exceeded");
    if (rc != LIW_WEBDOM_OK)
        return JS_ThrowTypeError(ctx, "cannot set outerHTML on this node");
    return JS_UNDEFINED;
}

static JSValue qjs_dom_outer_html_magic(JSContext *ctx,
                                        JSValueConst this_value, int magic) {
    (void)magic;
    return qjs_dom_outer_html(ctx, this_value);
}

static JSValue qjs_dom_set_outer_html_magic(JSContext *ctx,
                                            JSValueConst this_value,
                                            JSValueConst value, int magic) {
    (void)magic;
    return qjs_dom_set_outer_html(ctx, this_value, value);
}

static liw_web_node_id_t qjs_dom_find_element(const char *tag) {
    size_t length = strlen(tag);
    size_t count = liw_webdom_node_count(qjs_dom_document);
    for (liw_web_node_id_t i = 0; i < count; ++i) {
        if (liw_webdom_node_type(qjs_dom_document, i) != LIW_WEBDOM_ELEMENT)
            continue;
        liw_web_slice_t name = liw_webdom_name(qjs_dom_document, i);
        if (qjs_slice_equal(name.data, name.length, tag, length, 1)) return i;
    }
    return LIW_WEBDOM_NODE_NONE;
}

static JSValue qjs_dom_body(JSContext *ctx, JSValueConst this_value) {
    (void)this_value;
    return qjs_dom_wrap_node(ctx, qjs_dom_find_element("body"));
}

static JSValue qjs_dom_head(JSContext *ctx, JSValueConst this_value) {
    (void)this_value;
    return qjs_dom_wrap_node(ctx, qjs_dom_find_element("head"));
}

static JSValue qjs_dom_document_element(JSContext *ctx, JSValueConst this_value) {
    (void)this_value;
    return qjs_dom_wrap_node(ctx, qjs_dom_find_element("html"));
}

static JSValue qjs_dom_head_magic(JSContext *ctx, JSValueConst this_value,
                                  int magic) {
    (void)magic;
    return qjs_dom_head(ctx, this_value);
}

static JSValue qjs_dom_document_element_magic(JSContext *ctx,
                                              JSValueConst this_value,
                                              int magic) {
    (void)magic;
    return qjs_dom_document_element(ctx, this_value);
}

static JSValue qjs_dom_title(JSContext *ctx, JSValueConst this_value) {
    (void)this_value;
    size_t count = liw_webdom_node_count(qjs_dom_document);
    for (liw_web_node_id_t i = 0; i < count; ++i) {
        liw_web_slice_t name = liw_webdom_name(qjs_dom_document, i);
        if (liw_webdom_node_type(qjs_dom_document, i) == LIW_WEBDOM_ELEMENT &&
            name.length == 5 && name.data[0] == 't' && name.data[1] == 'i' &&
            name.data[2] == 't' && name.data[3] == 'l' && name.data[4] == 'e') {
            JSValue node = qjs_dom_wrap_node(ctx, i);
            if (JS_IsException(node)) return node;
            JSValue text = qjs_dom_node_text(ctx, node);
            JS_FreeValue(ctx, node);
            return text;
        }
    }
    return JS_NewString(ctx, "");
}

static JSValue qjs_dom_set_title(JSContext *ctx, JSValueConst this_value,
                                 JSValueConst value) {
    (void)this_value;
    size_t count = liw_webdom_node_count(qjs_dom_document);
    for (liw_web_node_id_t i = 0; i < count; ++i) {
        liw_web_slice_t name = liw_webdom_name(qjs_dom_document, i);
        if (liw_webdom_node_type(qjs_dom_document, i) == LIW_WEBDOM_ELEMENT &&
            name.length == 5 && name.data[0] == 't' && name.data[1] == 'i' &&
            name.data[2] == 't' && name.data[3] == 'l' && name.data[4] == 'e') {
            JSValue node = qjs_dom_wrap_node(ctx, i);
            if (JS_IsException(node)) return node;
            JSValue result = qjs_dom_set_node_text(ctx, node, value);
            JS_FreeValue(ctx, node);
            return result;
        }
    }
    return JS_ThrowTypeError(ctx, "document has no title element");
}

static JSValue qjs_dom_get_attribute(JSContext *ctx, JSValueConst this_value,
                                     int argc, JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "attribute name is required");
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    liw_web_slice_t value;
    int rc = liw_webdom_get_attribute(node->document, node->node, name, &value);
    int needs_decode = rc == LIW_WEBDOM_OK &&
        liw_webdom_attribute_needs_entity_decode(node->document, node->node,
                                                 name);
    JS_FreeCString(ctx, name);
    if (rc == LIW_WEBDOM_NOT_FOUND) return JS_NULL;
    if (rc != LIW_WEBDOM_OK) return JS_ThrowTypeError(ctx, "not an element");
    return qjs_dom_decode_slice(ctx, value, needs_decode);
}

static JSValue qjs_dom_set_attribute(JSContext *ctx, JSValueConst this_value,
                                     int argc, JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "attribute name and value are required");
    size_t name_length = 0, value_length = 0;
    const char *name = JS_ToCStringLen(ctx, &name_length, argv[0]);
    if (!name) return JS_EXCEPTION;
    const char *value = JS_ToCStringLen(ctx, &value_length, argv[1]);
    if (!value) {
        JS_FreeCString(ctx, name);
        return JS_EXCEPTION;
    }
    int rc = liw_webdom_set_attribute(node->document, node->node,
                                     name, name_length, value, value_length);
    JS_FreeCString(ctx, value);
    JS_FreeCString(ctx, name);
    if (rc == LIW_WEBDOM_NO_MEMORY || rc == LIW_WEBDOM_LIMIT)
        return JS_ThrowOutOfMemory(ctx);
    if (rc != LIW_WEBDOM_OK)
        return JS_ThrowTypeError(ctx, "cannot set attribute on this node");
    return JS_UNDEFINED;
}

static JSValue qjs_dom_remove_attribute(JSContext *ctx,
                                        JSValueConst this_value, int argc,
                                        JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "attribute name is required");
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    int rc = liw_webdom_remove_attribute(node->document, node->node, name);
    JS_FreeCString(ctx, name);
    if (rc != LIW_WEBDOM_OK && rc != LIW_WEBDOM_NOT_FOUND)
        return JS_ThrowTypeError(ctx, "cannot remove attribute from this node");
    return JS_UNDEFINED;
}

static JSValue qjs_dom_get_attribute_ns(JSContext *ctx,
                                        JSValueConst this_value, int argc,
                                        JSValueConst *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "namespace and name are required");
    return qjs_dom_get_attribute(ctx, this_value, 1, argv + 1);
}

static JSValue qjs_dom_set_attribute_ns(JSContext *ctx,
                                        JSValueConst this_value, int argc,
                                        JSValueConst *argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "namespace, name and value are required");
    JSValue args[2] = { argv[1], argv[2] };
    return qjs_dom_set_attribute(ctx, this_value, 2, args);
}

static JSValue qjs_dom_remove_attribute_ns(JSContext *ctx,
                                           JSValueConst this_value, int argc,
                                           JSValueConst *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "namespace and name are required");
    return qjs_dom_remove_attribute(ctx, this_value, 1, argv + 1);
}

static const char *qjs_dom_attribute_name(int magic) {
    static const char *const names[] = { "id", "class", "value", "href" };
    return magic >= 0 && magic < (int)(sizeof(names) / sizeof(names[0]))
        ? names[magic] : NULL;
}

static JSValue qjs_dom_get_attribute_magic(JSContext *ctx,
                                           JSValueConst this_value, int magic) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    const char *name = qjs_dom_attribute_name(magic);
    if (!name) return JS_ThrowInternalError(ctx, "invalid DOM property");
    liw_web_slice_t value;
    if (liw_webdom_get_attribute(node->document, node->node, name, &value) !=
        LIW_WEBDOM_OK) return JS_NewString(ctx, "");
    return qjs_dom_decode_slice(ctx, value,
        liw_webdom_attribute_needs_entity_decode(node->document, node->node,
                                                 name));
}

static JSValue qjs_dom_set_attribute_magic(JSContext *ctx,
                                           JSValueConst this_value,
                                           JSValueConst value, int magic) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    const char *name = qjs_dom_attribute_name(magic);
    if (!name) return JS_ThrowInternalError(ctx, "invalid DOM property");
    size_t length = 0;
    const char *text = JS_ToCStringLen(ctx, &length, value);
    if (!text) return JS_EXCEPTION;
    int rc = liw_webdom_set_attribute(node->document, node->node, name,
                                      strlen(name), text, length);
    JS_FreeCString(ctx, text);
    if (rc == LIW_WEBDOM_NO_MEMORY || rc == LIW_WEBDOM_LIMIT)
        return JS_ThrowOutOfMemory(ctx);
    if (rc != LIW_WEBDOM_OK)
        return JS_ThrowTypeError(ctx, "cannot set property on this node");
    return JS_UNDEFINED;
}

typedef JSValue qjs_dom_getter_magic_fn(JSContext *ctx, JSValueConst this_value,
                                        int magic);
typedef JSValue qjs_dom_setter_magic_fn(JSContext *ctx, JSValueConst this_value,
                                        JSValueConst value, int magic);

static int qjs_define_dom_accessor(JSContext *ctx, JSValueConst object,
                                   const char *name,
                                   qjs_dom_getter_magic_fn *getter,
                                   qjs_dom_setter_magic_fn *setter, int magic) {
    JSCFunctionType getter_type = { .getter_magic = getter };
    JSCFunctionType setter_type = { .setter_magic = setter };
    JSValue get = JS_NewCFunction2(ctx, getter_type.generic, name, 0,
                                   JS_CFUNC_getter_magic, magic);
    JSValue set = setter
        ? JS_NewCFunction2(ctx, setter_type.generic, name, 1,
                           JS_CFUNC_setter_magic, magic)
        : JS_UNDEFINED;
    if (JS_IsException(get) || JS_IsException(set)) {
        JS_FreeValue(ctx, get);
        JS_FreeValue(ctx, set);
        return -1;
    }
    JSAtom atom = JS_NewAtom(ctx, name);
    int rc = JS_DefinePropertyGetSet(ctx, object, atom, get, set,
                                     JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom);
    return rc < 0 ? -1 : 0;
}

typedef enum {
    QJS_CLASS_ADD,
    QJS_CLASS_REMOVE,
    QJS_CLASS_CONTAINS,
    QJS_CLASS_TOGGLE,
} qjs_class_operation_t;

static int qjs_dom_class_token_valid(const char *token, size_t length) {
    if (!length) return 0;
    for (size_t i = 0; i < length; ++i)
        if (qjs_dom_space(token[i])) return 0;
    return 1;
}

static JSValue qjs_dom_class_operation(JSContext *ctx, JSValueConst node_value,
                                       JSValueConst token_value,
                                       qjs_class_operation_t operation,
                                       int force_present) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, node_value);
    if (!node) return JS_EXCEPTION;
    if (liw_webdom_node_type(node->document, node->node) != LIW_WEBDOM_ELEMENT)
        return JS_ThrowTypeError(ctx, "classList requires an element");
    size_t token_length = 0;
    const char *token = JS_ToCStringLen(ctx, &token_length, token_value);
    if (!token) return JS_EXCEPTION;
    if (!qjs_dom_class_token_valid(token, token_length)) {
        JS_FreeCString(ctx, token);
        return JS_ThrowTypeError(ctx, "class token must be a non-empty word");
    }

    liw_web_slice_t current = { .data = "", .length = 0 };
    (void)liw_webdom_get_attribute(node->document, node->node, "class", &current);
    int found = 0;
    size_t cursor = 0;
    while (cursor < current.length) {
        while (cursor < current.length && qjs_dom_space(current.data[cursor])) cursor++;
        size_t start = cursor;
        while (cursor < current.length && !qjs_dom_space(current.data[cursor])) cursor++;
        if (qjs_slice_equal(current.data + start, cursor - start,
                            token, token_length, 0)) found = 1;
    }
    if (operation == QJS_CLASS_CONTAINS) {
        JS_FreeCString(ctx, token);
        return JS_NewBool(ctx, found);
    }
    int add = operation == QJS_CLASS_ADD ||
              (operation == QJS_CLASS_TOGGLE &&
               (force_present >= 0 ? force_present : !found));
    if (operation == QJS_CLASS_REMOVE ||
        (operation == QJS_CLASS_TOGGLE && !add)) {
        size_t capacity = current.length + 1;
        char *updated = (char *)malloc(capacity);
        if (!updated) {
            JS_FreeCString(ctx, token);
            return JS_ThrowOutOfMemory(ctx);
        }
        size_t out = 0;
        cursor = 0;
        while (cursor < current.length) {
            while (cursor < current.length && qjs_dom_space(current.data[cursor])) cursor++;
            size_t start = cursor;
            while (cursor < current.length && !qjs_dom_space(current.data[cursor])) cursor++;
            if (qjs_slice_equal(current.data + start, cursor - start,
                                token, token_length, 0)) continue;
            if (out) updated[out++] = ' ';
            memcpy(updated + out, current.data + start, cursor - start);
            out += cursor - start;
        }
        updated[out] = '\0';
        int rc = liw_webdom_set_attribute(node->document, node->node, "class", 5,
                                          updated, out);
        free(updated);
        JS_FreeCString(ctx, token);
        if (rc == LIW_WEBDOM_LIMIT || rc == LIW_WEBDOM_NO_MEMORY)
            return JS_ThrowOutOfMemory(ctx);
        if (rc != LIW_WEBDOM_OK)
            return JS_ThrowTypeError(ctx, "cannot update class list");
        return operation == QJS_CLASS_TOGGLE ? JS_NewBool(ctx, 0) : JS_UNDEFINED;
    }
    if (operation == QJS_CLASS_ADD && found) {
        JS_FreeCString(ctx, token);
        return JS_UNDEFINED;
    }
    if (!found && add) {
        if (token_length > LIW_WEBDOM_MAX_SOURCE - 2U ||
            current.length > LIW_WEBDOM_MAX_SOURCE - token_length - 2U) {
            JS_FreeCString(ctx, token);
            return JS_ThrowOutOfMemory(ctx);
        }
        size_t updated_length = current.length + token_length +
                                (current.length ? 1U : 0U);
        char *updated = (char *)malloc(updated_length + 1);
        if (!updated) {
            JS_FreeCString(ctx, token);
            return JS_ThrowOutOfMemory(ctx);
        }
        if (current.length) {
            memcpy(updated, current.data, current.length);
            updated[current.length] = ' ';
        }
        memcpy(updated + current.length + (current.length ? 1U : 0U),
               token, token_length);
        updated[updated_length] = '\0';
        int rc = liw_webdom_set_attribute(node->document, node->node, "class", 5,
                                          updated, updated_length);
        free(updated);
        JS_FreeCString(ctx, token);
        if (rc == LIW_WEBDOM_LIMIT || rc == LIW_WEBDOM_NO_MEMORY)
            return JS_ThrowOutOfMemory(ctx);
        if (rc != LIW_WEBDOM_OK)
            return JS_ThrowTypeError(ctx, "cannot update class list");
    } else {
        JS_FreeCString(ctx, token);
    }
    return operation == QJS_CLASS_TOGGLE ? JS_NewBool(ctx, 1) : JS_UNDEFINED;
}

static JSValue qjs_classlist_function(JSContext *ctx, JSValueConst this_value,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValue *function_data) {
    (void)this_value;
    if (magic == QJS_CLASS_CONTAINS && argc != 1)
        return JS_ThrowTypeError(ctx, "contains expects one token");
    if (magic == QJS_CLASS_TOGGLE && (argc < 1 || argc > 2))
        return JS_ThrowTypeError(ctx, "toggle expects a token and optional force");
    if ((magic == QJS_CLASS_ADD || magic == QJS_CLASS_REMOVE) && argc < 1)
        return JS_ThrowTypeError(ctx, "classList method expects at least one token");
    JSValue result = JS_UNDEFINED;
    for (int i = 0; i < argc; ++i) {
        int force = -1;
        if (magic == QJS_CLASS_TOGGLE && argc == 2) {
            force = JS_ToBool(ctx, argv[1]);
            if (force < 0) return JS_EXCEPTION;
        }
        result = qjs_dom_class_operation(ctx, function_data[0], argv[i],
            (qjs_class_operation_t)magic, force);
        if (JS_IsException(result)) return result;
        if (magic == QJS_CLASS_CONTAINS || magic == QJS_CLASS_TOGGLE) return result;
    }
    return result;
}

static JSValue qjs_dom_class_list(JSContext *ctx, JSValueConst this_value) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (liw_webdom_node_type(node->document, node->node) != LIW_WEBDOM_ELEMENT)
        return JS_ThrowTypeError(ctx, "classList requires an element");
    JSValue list = JS_NewObject(ctx);
    if (JS_IsException(list)) return list;
    JSValue data[1] = { JS_DupValue(ctx, this_value) };
    static const struct {
        const char *name;
        int length;
        int magic;
    } methods[] = {
        { "add", 1, QJS_CLASS_ADD },
        { "remove", 1, QJS_CLASS_REMOVE },
        { "contains", 1, QJS_CLASS_CONTAINS },
        { "toggle", 2, QJS_CLASS_TOGGLE },
    };
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        JSValue function = JS_NewCFunctionData(ctx, qjs_classlist_function,
            methods[i].length, methods[i].magic, 1, (JSValueConst *)data);
        if (JS_IsException(function) ||
            JS_SetPropertyStr(ctx, list, methods[i].name, function) < 0) {
            JS_FreeValue(ctx, data[0]);
            JS_FreeValue(ctx, list);
            return JS_EXCEPTION;
        }
    }
    JS_FreeValue(ctx, data[0]);
    return list;
}

static int qjs_dom_insert_fragment_children(liw_web_document_t *doc,
                                           liw_web_node_id_t parent,
                                           liw_web_node_id_t fragment,
                                           liw_web_node_id_t reference) {
    liw_web_node_type_t parent_type = liw_webdom_node_type(doc, parent);
    if (parent_type != LIW_WEBDOM_DOCUMENT &&
        parent_type != LIW_WEBDOM_ELEMENT &&
        parent_type != LIW_WEBDOM_FRAGMENT) return LIW_WEBDOM_INVALID;
    for (liw_web_node_id_t ancestor = parent;
         ancestor != LIW_WEBDOM_NODE_NONE;
         ancestor = liw_webdom_parent(doc, ancestor))
        if (ancestor == fragment) return LIW_WEBDOM_INVALID;
    if (reference != LIW_WEBDOM_NODE_NONE &&
        liw_webdom_parent(doc, reference) != parent) return LIW_WEBDOM_NOT_FOUND;
    liw_web_node_id_t child = liw_webdom_first_child(doc, fragment);
    while (child != LIW_WEBDOM_NODE_NONE) {
        liw_web_node_id_t next = liw_webdom_next_sibling(doc, child);
        int rc = liw_webdom_insert_before(doc, parent, child, reference);
        if (rc != LIW_WEBDOM_OK) return rc;
        child = next;
    }
    return LIW_WEBDOM_OK;
}

static JSValue qjs_dom_append_child(JSContext *ctx, JSValueConst this_value,
                                    int argc, JSValueConst *argv) {
    qjs_dom_node_t *parent = qjs_get_dom_node(ctx, this_value);
    if (!parent) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "child node is required");
    qjs_dom_node_t *child = qjs_get_dom_node(ctx, argv[0]);
    if (!child) return JS_EXCEPTION;
    int rc = liw_webdom_node_type(child->document, child->node) == LIW_WEBDOM_FRAGMENT
        ? qjs_dom_insert_fragment_children(parent->document, parent->node,
            child->node, LIW_WEBDOM_NODE_NONE)
        : liw_webdom_append_child(parent->document, parent->node, child->node);
    if (rc == LIW_WEBDOM_NO_MEMORY || rc == LIW_WEBDOM_LIMIT)
        return JS_ThrowOutOfMemory(ctx);
    if (rc != LIW_WEBDOM_OK)
        return JS_ThrowTypeError(ctx, "invalid DOM appendChild operation");
    return JS_DupValue(ctx, argv[0]);
}

static JSValue qjs_dom_remove_child(JSContext *ctx, JSValueConst this_value,
                                    int argc, JSValueConst *argv) {
    qjs_dom_node_t *parent = qjs_get_dom_node(ctx, this_value);
    if (!parent) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "child node is required");
    qjs_dom_node_t *child = qjs_get_dom_node(ctx, argv[0]);
    if (!child) return JS_EXCEPTION;
    int rc = liw_webdom_remove_child(parent->document, parent->node, child->node);
    if (rc != LIW_WEBDOM_OK)
        return JS_ThrowTypeError(ctx, "node is not a child of this parent");
    return JS_DupValue(ctx, argv[0]);
}

static JSValue qjs_dom_has_child_nodes(JSContext *ctx, JSValueConst this_value,
                                      int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    return JS_NewBool(ctx, liw_webdom_first_child(node->document, node->node) !=
                           LIW_WEBDOM_NODE_NONE);
}

static JSValue qjs_dom_has_attributes(JSContext *ctx, JSValueConst this_value,
                                      int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    return JS_NewBool(ctx, liw_webdom_attribute_count(node->document,
                                                      node->node) != 0);
}

static JSValue qjs_dom_get_attribute_names(JSContext *ctx,
                                           JSValueConst this_value,
                                           int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    JSValue names = JS_NewArray(ctx);
    if (JS_IsException(names)) return names;
    size_t count = liw_webdom_attribute_count(node->document, node->node);
    for (uint32_t i = 0; i < count; ++i) {
        liw_web_slice_t name, value;
        if (liw_webdom_attribute_at(node->document, node->node, i,
                                    &name, &value) != LIW_WEBDOM_OK) continue;
        JSValue text = JS_NewStringLen(ctx, name.data, name.length);
        if (JS_IsException(text) || JS_SetPropertyUint32(ctx, names, i, text) < 0) {
            JS_FreeValue(ctx, names);
            return JS_EXCEPTION;
        }
    }
    return names;
}

static int qjs_dom_nodes_equal(const liw_web_document_t *doc,
                               liw_web_node_id_t a, liw_web_node_id_t b,
                               size_t *steps) {
    if (++*steps > LIW_WEBDOM_MAX_NODES ||
        liw_webdom_node_type(doc, a) != liw_webdom_node_type(doc, b)) return 0;
    liw_web_node_type_t type = liw_webdom_node_type(doc, a);
    if (type == LIW_WEBDOM_ELEMENT) {
        liw_web_slice_t an = liw_webdom_name(doc, a), bn = liw_webdom_name(doc, b);
        if (!qjs_slice_equal(an.data, an.length, bn.data, bn.length, 1)) return 0;
        size_t ac = liw_webdom_attribute_count(doc, a);
        if (ac != liw_webdom_attribute_count(doc, b)) return 0;
        for (size_t i = 0; i < ac; ++i) {
            liw_web_slice_t name, av;
            if (liw_webdom_attribute_at(doc, a, i, &name, &av) != LIW_WEBDOM_OK)
                return 0;
            int found = 0;
            size_t bc = liw_webdom_attribute_count(doc, b);
            for (size_t j = 0; j < bc; ++j) {
                liw_web_slice_t other_name, bv;
                if (liw_webdom_attribute_at(doc, b, j, &other_name, &bv) !=
                    LIW_WEBDOM_OK ||
                    !qjs_slice_equal(name.data, name.length,
                                     other_name.data, other_name.length, 1))
                    continue;
                if (av.length != bv.length ||
                    memcmp(av.data, bv.data, av.length) != 0) return 0;
                found = 1;
                break;
            }
            if (!found) return 0;
        }
    } else if (type == LIW_WEBDOM_TEXT || type == LIW_WEBDOM_COMMENT) {
        liw_web_slice_t av = liw_webdom_value(doc, a), bv = liw_webdom_value(doc, b);
        if (av.length != bv.length || memcmp(av.data, bv.data, av.length) != 0 ||
            liw_webdom_text_needs_entity_decode(doc, a) !=
            liw_webdom_text_needs_entity_decode(doc, b)) return 0;
    }
    liw_web_node_id_t ac = liw_webdom_first_child(doc, a);
    liw_web_node_id_t bc = liw_webdom_first_child(doc, b);
    while (ac != LIW_WEBDOM_NODE_NONE && bc != LIW_WEBDOM_NODE_NONE) {
        if (!qjs_dom_nodes_equal(doc, ac, bc, steps)) return 0;
        ac = liw_webdom_next_sibling(doc, ac);
        bc = liw_webdom_next_sibling(doc, bc);
    }
    return ac == LIW_WEBDOM_NODE_NONE && bc == LIW_WEBDOM_NODE_NONE;
}

static JSValue qjs_dom_is_same_node(JSContext *ctx, JSValueConst this_value,
                                    int argc, JSValueConst *argv) {
    if (argc < 1 || JS_IsNull(argv[0])) return JS_FALSE;
    if (!qjs_get_dom_node(ctx, this_value)) return JS_EXCEPTION;
    if (!qjs_get_dom_node(ctx, argv[0])) return JS_EXCEPTION;
    return JS_NewBool(ctx, JS_StrictEq(ctx, this_value, argv[0]));
}

static JSValue qjs_dom_is_equal_node(JSContext *ctx, JSValueConst this_value,
                                     int argc, JSValueConst *argv) {
    qjs_dom_node_t *a = qjs_get_dom_node(ctx, this_value);
    if (!a) return JS_EXCEPTION;
    if (argc < 1 || JS_IsNull(argv[0])) return JS_FALSE;
    qjs_dom_node_t *b = qjs_get_dom_node(ctx, argv[0]);
    if (!b) return JS_EXCEPTION;
    if (a->document != b->document) return JS_FALSE;
    size_t steps = 0;
    return JS_NewBool(ctx, qjs_dom_nodes_equal(a->document, a->node,
                                                b->node, &steps));
}

static JSValue qjs_dom_compare_document_position(JSContext *ctx,
                                                 JSValueConst this_value,
                                                 int argc, JSValueConst *argv) {
    qjs_dom_node_t *a = qjs_get_dom_node(ctx, this_value);
    if (!a) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "node is required");
    qjs_dom_node_t *b = qjs_get_dom_node(ctx, argv[0]);
    if (!b) return JS_EXCEPTION;
    if (a->document != b->document)
        return JS_NewUint32(ctx, 1U | 32U | 4U);
    if (a->node == b->node) return JS_NewUint32(ctx, 0);
    liw_web_node_id_t aa[LIW_WEBDOM_MAX_DEPTH + 2];
    liw_web_node_id_t ba[LIW_WEBDOM_MAX_DEPTH + 2];
    size_t an = 0, bn = 0;
    for (uint32_t n = a->node; n != LIW_WEBDOM_NODE_NONE &&
         an < LIW_WEBDOM_MAX_DEPTH + 2; n = liw_webdom_parent(a->document, n)) aa[an++] = n;
    for (uint32_t n = b->node; n != LIW_WEBDOM_NODE_NONE &&
         bn < LIW_WEBDOM_MAX_DEPTH + 2; n = liw_webdom_parent(b->document, n)) ba[bn++] = n;
    if (!an || !bn || aa[an - 1] != ba[bn - 1])
        return JS_NewUint32(ctx, 1U | 32U | 4U);
    if (an < bn && aa[an - 1] == ba[an - 1]) {
        size_t common = 0;
        while (common < an && common < bn && aa[an - common - 1] == ba[bn - common - 1]) common++;
        if (common == an) return JS_NewUint32(ctx, 4U | 16U);
    }
    if (bn < an) {
        size_t common = 0;
        while (common < an && common < bn && aa[an - common - 1] == ba[bn - common - 1]) common++;
        if (common == bn) return JS_NewUint32(ctx, 2U | 8U);
    }
    size_t common = 0;
    while (common < an && common < bn && aa[an - common - 1] == ba[bn - common - 1]) common++;
    if (!common || common >= an || common >= bn)
        return JS_NewUint32(ctx, 1U | 32U | 4U);
    liw_web_node_id_t abranch = aa[an - common - 1];
    liw_web_node_id_t bbranch = ba[bn - common - 1];
    for (liw_web_node_id_t child = liw_webdom_first_child(a->document,
            aa[an - common]); child != LIW_WEBDOM_NODE_NONE;
         child = liw_webdom_next_sibling(a->document, child)) {
        if (child == abranch) return JS_NewUint32(ctx, 4U);
        if (child == bbranch) return JS_NewUint32(ctx, 2U);
    }
    return JS_NewUint32(ctx, 1U | 32U | 4U);
}

static JSValue qjs_dom_get_root_node(JSContext *ctx, JSValueConst this_value,
                                     int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    liw_web_node_id_t root = node->node;
    while (liw_webdom_parent(node->document, root) != LIW_WEBDOM_NODE_NONE)
        root = liw_webdom_parent(node->document, root);
    return qjs_dom_wrap_node(ctx, root);
}

static int qjs_dom_normalize_subtree(JSContext *ctx,
                                     liw_web_document_t *doc,
                                     liw_web_node_id_t parent,
                                     size_t depth) {
    if (depth > LIW_WEBDOM_MAX_DEPTH) return -1;
    liw_web_node_id_t child = liw_webdom_first_child(doc, parent);
    while (child != LIW_WEBDOM_NODE_NONE) {
        liw_web_node_id_t next = liw_webdom_next_sibling(doc, child);
        if (liw_webdom_node_type(doc, child) == LIW_WEBDOM_TEXT) {
            size_t capacity = 64, length = 0;
            char *text = (char *)malloc(capacity);
            if (!text) return -1;
            liw_web_node_id_t current = child;
            do {
                JSValue wrapped = qjs_dom_wrap_node(ctx, current);
                JSValue value = JS_IsException(wrapped) ? JS_EXCEPTION
                    : qjs_dom_node_text(ctx, wrapped);
                if (JS_IsException(value)) { JS_FreeValue(ctx, wrapped); free(text); return -1; }
                size_t part_length = 0;
                const char *part = JS_ToCStringLen(ctx, &part_length, value);
                if (!part) {
                    JS_FreeValue(ctx, value);
                    JS_FreeValue(ctx, wrapped);
                    free(text);
                    return -1;
                }
                if (part_length > QJS_MAX_DOM_TEXT_BYTES - length) {
                    JS_FreeCString(ctx, part);
                    JS_FreeValue(ctx, value);
                    JS_FreeValue(ctx, wrapped);
                    free(text);
                    return -1;
                }
                size_t required = length + part_length + 1;
                if (required > capacity) {
                    size_t next_capacity = capacity;
                    while (next_capacity < required) next_capacity *= 2;
                    char *grown = (char *)realloc(text, next_capacity);
                    if (!grown) {
                        JS_FreeCString(ctx, part);
                        JS_FreeValue(ctx, value);
                        JS_FreeValue(ctx, wrapped);
                        free(text);
                        return -1;
                    }
                    text = grown; capacity = next_capacity;
                }
                memcpy(text + length, part, part_length);
                length += part_length;
                JS_FreeCString(ctx, part);
                JS_FreeValue(ctx, value);
                JS_FreeValue(ctx, wrapped);
                current = liw_webdom_next_sibling(doc, current);
                if (current != LIW_WEBDOM_NODE_NONE &&
                    liw_webdom_node_type(doc, current) != LIW_WEBDOM_TEXT) break;
            } while (current != LIW_WEBDOM_NODE_NONE);
            text[length] = '\0';
            if (liw_webdom_set_text(doc, child, text, length) != LIW_WEBDOM_OK) {
                free(text); return -1;
            }
            free(text);
            liw_web_node_id_t merge = liw_webdom_next_sibling(doc, child);
            while (merge != LIW_WEBDOM_NODE_NONE &&
                   liw_webdom_node_type(doc, merge) == LIW_WEBDOM_TEXT) {
                liw_web_node_id_t remove = merge;
                merge = liw_webdom_next_sibling(doc, merge);
                if (liw_webdom_remove_child(doc, parent, remove) != LIW_WEBDOM_OK) return -1;
            }
            next = liw_webdom_next_sibling(doc, child);
            if (liw_webdom_value(doc, child).length == 0) {
                if (liw_webdom_remove_child(doc, parent, child) != LIW_WEBDOM_OK) return -1;
                child = next;
                continue;
            }
        } else if (liw_webdom_node_type(doc, child) == LIW_WEBDOM_ELEMENT ||
                   liw_webdom_node_type(doc, child) == LIW_WEBDOM_FRAGMENT) {
            if (qjs_dom_normalize_subtree(ctx, doc, child, depth + 1) < 0) return -1;
        }
        child = next;
    }
    return 0;
}

static JSValue qjs_dom_normalize(JSContext *ctx, JSValueConst this_value,
                                 int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (qjs_dom_normalize_subtree(ctx, node->document, node->node, 0) < 0)
        return JS_ThrowRangeError(ctx, "DOM normalization limit exceeded");
    return JS_UNDEFINED;
}

enum {
    QJS_NODE_TYPE, QJS_NODE_NAME, QJS_PARENT_NODE, QJS_PARENT_ELEMENT,
    QJS_FIRST_CHILD, QJS_LAST_CHILD, QJS_NEXT_SIBLING, QJS_PREVIOUS_SIBLING,
    QJS_CHILDREN, QJS_CHILD_ELEMENT_COUNT, QJS_IS_CONNECTED, QJS_TAG_NAME,
    QJS_CHILD_NODES, QJS_FIRST_ELEMENT_CHILD, QJS_LAST_ELEMENT_CHILD,
    QJS_NEXT_ELEMENT_SIBLING, QJS_PREVIOUS_ELEMENT_SIBLING, QJS_OWNER_DOCUMENT
};

static JSValue qjs_dom_node_property(JSContext *ctx, JSValueConst this_value,
                                     int magic) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    liw_web_document_t *doc = node->document;
    liw_web_node_id_t id = node->node;
    liw_web_node_id_t found = LIW_WEBDOM_NODE_NONE;
    liw_web_node_type_t type = liw_webdom_node_type(doc, id);
    switch (magic) {
    case QJS_NODE_TYPE:
        return JS_NewInt32(ctx, type == LIW_WEBDOM_DOCUMENT ? 9 :
            type == LIW_WEBDOM_ELEMENT ? 1 : type == LIW_WEBDOM_TEXT ? 3 :
            type == LIW_WEBDOM_FRAGMENT ? 11 : 8);
    case QJS_NODE_NAME:
        if (type == LIW_WEBDOM_DOCUMENT) return JS_NewString(ctx, "#document");
        if (type == LIW_WEBDOM_TEXT) return JS_NewString(ctx, "#text");
        if (type == LIW_WEBDOM_COMMENT) return JS_NewString(ctx, "#comment");
        if (type == LIW_WEBDOM_FRAGMENT)
            return JS_NewString(ctx, "#document-fragment");
        {
            liw_web_slice_t name = liw_webdom_name(doc, id);
            char upper[256];
            size_t length = name.length < sizeof(upper) ? name.length : sizeof(upper);
            for (size_t i = 0; i < length; ++i) {
                char c = name.data[i];
                upper[i] = c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
            }
            return JS_NewStringLen(ctx, upper, length);
        }
    case QJS_TAG_NAME:
        if (type != LIW_WEBDOM_ELEMENT) return JS_NULL;
        {
            liw_web_slice_t name = liw_webdom_name(doc, id);
            char upper[256];
            size_t length = name.length < sizeof(upper) ? name.length : sizeof(upper);
            for (size_t i = 0; i < length; ++i) {
                char c = name.data[i];
                upper[i] = c >= 'a' && c <= 'z' ? (char)(c - ('a' - 'A')) : c;
            }
            return JS_NewStringLen(ctx, upper, length);
        }
    case QJS_PARENT_NODE:
    case QJS_PARENT_ELEMENT:
        found = liw_webdom_parent(doc, id);
        if (magic == QJS_PARENT_ELEMENT &&
            liw_webdom_node_type(doc, found) != LIW_WEBDOM_ELEMENT)
            found = LIW_WEBDOM_NODE_NONE;
        break;
    case QJS_FIRST_CHILD: found = liw_webdom_first_child(doc, id); break;
    case QJS_LAST_CHILD: found = liw_webdom_last_child(doc, id); break;
    case QJS_NEXT_SIBLING: found = liw_webdom_next_sibling(doc, id); break;
    case QJS_PREVIOUS_SIBLING: found = liw_webdom_previous_sibling(doc, id); break;
    case QJS_FIRST_ELEMENT_CHILD:
        found = liw_webdom_first_child(doc, id);
        while (found != LIW_WEBDOM_NODE_NONE &&
               liw_webdom_node_type(doc, found) != LIW_WEBDOM_ELEMENT)
            found = liw_webdom_next_sibling(doc, found);
        break;
    case QJS_LAST_ELEMENT_CHILD:
        found = liw_webdom_last_child(doc, id);
        while (found != LIW_WEBDOM_NODE_NONE &&
               liw_webdom_node_type(doc, found) != LIW_WEBDOM_ELEMENT)
            found = liw_webdom_previous_sibling(doc, found);
        break;
    case QJS_NEXT_ELEMENT_SIBLING:
        found = liw_webdom_next_sibling(doc, id);
        while (found != LIW_WEBDOM_NODE_NONE &&
               liw_webdom_node_type(doc, found) != LIW_WEBDOM_ELEMENT)
            found = liw_webdom_next_sibling(doc, found);
        break;
    case QJS_PREVIOUS_ELEMENT_SIBLING:
        found = liw_webdom_previous_sibling(doc, id);
        while (found != LIW_WEBDOM_NODE_NONE &&
               liw_webdom_node_type(doc, found) != LIW_WEBDOM_ELEMENT)
            found = liw_webdom_previous_sibling(doc, found);
        break;
    case QJS_IS_CONNECTED:
        return JS_NewBool(ctx, liw_webdom_is_connected(doc, id));
    case QJS_OWNER_DOCUMENT:
        return id == liw_webdom_root(doc) || !qjs_dom_document_object_valid
            ? JS_NULL : JS_DupValue(ctx, qjs_dom_document_object);
    case QJS_CHILDREN: {
        JSValue array = JS_NewArray(ctx);
        if (JS_IsException(array)) return array;
        uint32_t index = 0;
        size_t steps = 0;
        for (liw_web_node_id_t child = liw_webdom_first_child(doc, id);
             child != LIW_WEBDOM_NODE_NONE && steps++ < LIW_WEBDOM_MAX_NODES;
             child = liw_webdom_next_sibling(doc, child)) {
            if (liw_webdom_node_type(doc, child) != LIW_WEBDOM_ELEMENT) continue;
            JSValue wrapped = qjs_dom_wrap_node(ctx, child);
            if (JS_IsException(wrapped) ||
                JS_SetPropertyUint32(ctx, array, index++, wrapped) < 0) {
                JS_FreeValue(ctx, array);
                return JS_EXCEPTION;
            }
        }
        return array;
    }
    case QJS_CHILD_NODES: {
        JSValue array = JS_NewArray(ctx);
        if (JS_IsException(array)) return array;
        uint32_t index = 0;
        size_t steps = 0;
        for (liw_web_node_id_t child = liw_webdom_first_child(doc, id);
             child != LIW_WEBDOM_NODE_NONE && steps++ < LIW_WEBDOM_MAX_NODES;
             child = liw_webdom_next_sibling(doc, child)) {
            JSValue wrapped = qjs_dom_wrap_node(ctx, child);
            if (JS_IsException(wrapped) ||
                JS_SetPropertyUint32(ctx, array, index++, wrapped) < 0) {
                JS_FreeValue(ctx, array);
                return JS_EXCEPTION;
            }
        }
        return array;
    }
    case QJS_CHILD_ELEMENT_COUNT: {
        uint32_t count = 0;
        size_t steps = 0;
        for (liw_web_node_id_t child = liw_webdom_first_child(doc, id);
             child != LIW_WEBDOM_NODE_NONE && steps++ < LIW_WEBDOM_MAX_NODES;
             child = liw_webdom_next_sibling(doc, child))
            if (liw_webdom_node_type(doc, child) == LIW_WEBDOM_ELEMENT) count++;
        return JS_NewUint32(ctx, count);
    }
    default: return JS_ThrowInternalError(ctx, "invalid DOM node property");
    }
    return qjs_dom_wrap_node(ctx, found);
}

static JSValue qjs_dom_insert_before(JSContext *ctx, JSValueConst this_value,
                                     int argc, JSValueConst *argv) {
    qjs_dom_node_t *parent = qjs_get_dom_node(ctx, this_value);
    if (!parent) return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "insertBefore requires child and reference");
    qjs_dom_node_t *child = qjs_get_dom_node(ctx, argv[0]);
    if (!child) return JS_EXCEPTION;
    liw_web_node_id_t reference_id = LIW_WEBDOM_NODE_NONE;
    if (!JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        qjs_dom_node_t *reference = qjs_get_dom_node(ctx, argv[1]);
        if (!reference) return JS_EXCEPTION;
        reference_id = reference->node;
    }
    int rc = liw_webdom_node_type(child->document, child->node) == LIW_WEBDOM_FRAGMENT
        ? qjs_dom_insert_fragment_children(parent->document, parent->node,
            child->node, reference_id)
        : liw_webdom_insert_before(parent->document, parent->node,
                                   child->node, reference_id);
    if (rc != LIW_WEBDOM_OK)
        return JS_ThrowTypeError(ctx, "invalid DOM insertBefore operation");
    return JS_DupValue(ctx, argv[0]);
}

static JSValue qjs_dom_replace_child(JSContext *ctx, JSValueConst this_value,
                                     int argc, JSValueConst *argv) {
    qjs_dom_node_t *parent = qjs_get_dom_node(ctx, this_value);
    if (!parent) return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "replaceChild requires new and old nodes");
    qjs_dom_node_t *replacement = qjs_get_dom_node(ctx, argv[0]);
    qjs_dom_node_t *old = qjs_get_dom_node(ctx, argv[1]);
    if (!replacement || !old) return JS_EXCEPTION;
    if (liw_webdom_parent(parent->document, old->node) != parent->node)
        return JS_ThrowTypeError(ctx, "old node is not a child of this parent");
    if (replacement->node == old->node) return JS_DupValue(ctx, argv[1]);
    int rc = liw_webdom_node_type(replacement->document, replacement->node) ==
             LIW_WEBDOM_FRAGMENT
        ? qjs_dom_insert_fragment_children(parent->document, parent->node,
            replacement->node, old->node)
        : liw_webdom_insert_before(parent->document, parent->node,
                                   replacement->node, old->node);
    if (rc == LIW_WEBDOM_OK)
        rc = liw_webdom_remove_child(parent->document, parent->node, old->node);
    if (rc != LIW_WEBDOM_OK)
        return JS_ThrowTypeError(ctx, "invalid DOM replaceChild operation");
    return JS_DupValue(ctx, argv[1]);
}

static JSValue qjs_dom_remove(JSContext *ctx, JSValueConst this_value,
                              int argc, JSValueConst *argv) {
    (void)argc; (void)argv;
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    liw_web_node_id_t parent = liw_webdom_parent(node->document, node->node);
    if (parent != LIW_WEBDOM_NODE_NONE)
        (void)liw_webdom_remove_child(node->document, parent, node->node);
    return JS_UNDEFINED;
}

static JSValue qjs_dom_contains(JSContext *ctx, JSValueConst this_value,
                                int argc, JSValueConst *argv) {
    liw_web_node_id_t root_id = qjs_dom_receiver_node(ctx, this_value);
    if (root_id == LIW_WEBDOM_NODE_NONE) return JS_EXCEPTION;
    if (argc < 1 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]))
        return JS_FALSE;
    qjs_dom_node_t *other = qjs_get_dom_node(ctx, argv[0]);
    if (!other) return JS_EXCEPTION;
    liw_web_node_id_t node = other->node;
    for (size_t steps = 0; node != LIW_WEBDOM_NODE_NONE &&
         steps <= LIW_WEBDOM_MAX_DEPTH; ++steps) {
        if (node == root_id) return JS_TRUE;
        node = liw_webdom_parent(qjs_dom_document, node);
    }
    return JS_FALSE;
}

static void qjs_dom_event_finalizer(JSRuntime *runtime, JSValue value) {
    (void)runtime;
    free(JS_GetOpaque(value, qjs_dom_event_class));
}

static qjs_dom_event_t *qjs_get_dom_event(JSContext *ctx, JSValueConst value) {
    return (qjs_dom_event_t *)JS_GetOpaque2(ctx, value, qjs_dom_event_class);
}

static JSValue qjs_event_constructor(JSContext *ctx, JSValueConst new_target,
                                     int argc, JSValueConst *argv) {
    (void)new_target;
    if (argc < 1) return JS_ThrowTypeError(ctx, "Event requires a type");
    size_t type_length = 0;
    const char *type = JS_ToCStringLen(ctx, &type_length, argv[0]);
    if (!type) return JS_EXCEPTION;
    if (!type_length || type_length > QJS_DOM_MAX_EVENT_TYPE) {
        JS_FreeCString(ctx, type);
        return JS_ThrowRangeError(ctx, "event type is empty or too long");
    }
    for (size_t i = 0; i < type_length; ++i) {
        if (!type[i]) {
            JS_FreeCString(ctx, type);
            return JS_ThrowTypeError(ctx, "event type contains NUL");
        }
    }
    qjs_dom_event_t *event = (qjs_dom_event_t *)malloc(sizeof(*event));
    if (!event) {
        JS_FreeCString(ctx, type);
        return JS_ThrowOutOfMemory(ctx);
    }
    memset(event, 0, sizeof(*event));
    memcpy(event->type, type, type_length);
    event->type[type_length] = '\0';
    event->target = LIW_WEBDOM_NODE_NONE;
    event->current_target = LIW_WEBDOM_NODE_NONE;
    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue value = JS_GetPropertyStr(ctx, argv[1], "bubbles");
        if (JS_IsException(value)) {
            free(event);
            JS_FreeCString(ctx, type);
            return value;
        }
        event->bubbles = JS_ToBool(ctx, value);
        JS_FreeValue(ctx, value);
        value = JS_GetPropertyStr(ctx, argv[1], "cancelable");
        if (JS_IsException(value)) {
            free(event);
            JS_FreeCString(ctx, type);
            return value;
        }
        event->cancelable = JS_ToBool(ctx, value);
        JS_FreeValue(ctx, value);
    }
    JS_FreeCString(ctx, type);
    JSValue object = JS_NewObjectClass(ctx, qjs_dom_event_class);
    if (JS_IsException(object)) {
        free(event);
        return object;
    }
    JS_SetOpaque(object, event);
    return object;
}

static JSValue qjs_event_get_property(JSContext *ctx, JSValueConst this_value,
                                      int magic) {
    qjs_dom_event_t *event = qjs_get_dom_event(ctx, this_value);
    if (!event) return JS_EXCEPTION;
    switch (magic) {
    case 0: return JS_NewString(ctx, event->type);
    case 1: return JS_NewBool(ctx, event->bubbles);
    case 2: return JS_NewBool(ctx, event->cancelable);
    case 3: return JS_NewBool(ctx, event->default_prevented);
    case 4:
        return event->target == LIW_WEBDOM_NODE_NONE
            ? JS_NULL : qjs_dom_wrap_node(ctx, event->target);
    case 5:
        return event->current_target == LIW_WEBDOM_NODE_NONE
            ? JS_NULL : qjs_dom_wrap_node(ctx, event->current_target);
    case 6: return JS_NewInt32(ctx, event->event_phase);
    default: return JS_ThrowInternalError(ctx, "invalid event property");
    }
}

static JSValue qjs_event_prevent_default(JSContext *ctx,
                                         JSValueConst this_value, int argc,
                                         JSValueConst *argv) {
    (void)argc; (void)argv;
    qjs_dom_event_t *event = qjs_get_dom_event(ctx, this_value);
    if (!event) return JS_EXCEPTION;
    if (event->cancelable) event->default_prevented = 1;
    return JS_UNDEFINED;
}

static JSValue qjs_event_stop_propagation(JSContext *ctx,
                                          JSValueConst this_value, int argc,
                                          JSValueConst *argv) {
    (void)argc; (void)argv;
    qjs_dom_event_t *event = qjs_get_dom_event(ctx, this_value);
    if (!event) return JS_EXCEPTION;
    event->propagation_stopped = 1;
    return JS_UNDEFINED;
}

static JSValue qjs_event_stop_immediate(JSContext *ctx,
                                        JSValueConst this_value, int argc,
                                        JSValueConst *argv) {
    (void)argc; (void)argv;
    qjs_dom_event_t *event = qjs_get_dom_event(ctx, this_value);
    if (!event) return JS_EXCEPTION;
    event->immediate_stopped = 1;
    event->propagation_stopped = 1;
    return JS_UNDEFINED;
}

static int qjs_event_type_from_value(JSContext *ctx, JSValueConst value,
                                     char type[QJS_DOM_MAX_EVENT_TYPE + 1]) {
    size_t length = 0;
    const char *text = JS_ToCStringLen(ctx, &length, value);
    if (!text) return -1;
    if (!length || length > QJS_DOM_MAX_EVENT_TYPE) {
        JS_FreeCString(ctx, text);
        JS_ThrowRangeError(ctx, "event type is empty or too long");
        return -1;
    }
    for (size_t i = 0; i < length; ++i) {
        if (!text[i]) {
            JS_FreeCString(ctx, text);
            JS_ThrowTypeError(ctx, "event type contains NUL");
            return -1;
        }
    }
    memcpy(type, text, length);
    type[length] = '\0';
    JS_FreeCString(ctx, text);
    return 0;
}

static JSValue qjs_dom_add_event_listener(JSContext *ctx,
                                          JSValueConst this_value, int argc,
                                          JSValueConst *argv) {
    liw_web_node_id_t node_id = qjs_dom_receiver_node(ctx, this_value);
    if (node_id == LIW_WEBDOM_NODE_NONE) return JS_EXCEPTION;
    if (argc < 2 || (!JS_IsFunction(ctx, argv[1]) && !JS_IsObject(argv[1])))
        return JS_ThrowTypeError(ctx, "addEventListener requires type and listener");
    if (argc >= 2 && JS_IsObject(argv[1]) && !JS_IsFunction(ctx, argv[1])) {
        JSValue handle_event = JS_GetPropertyStr(ctx, argv[1], "handleEvent");
        if (JS_IsException(handle_event)) return handle_event;
        int valid = JS_IsFunction(ctx, handle_event);
        JS_FreeValue(ctx, handle_event);
        if (!valid) return JS_ThrowTypeError(ctx, "listener object requires handleEvent()");
    }
    char type[QJS_DOM_MAX_EVENT_TYPE + 1];
    if (qjs_event_type_from_value(ctx, argv[0], type) < 0) return JS_EXCEPTION;
    int capture = 0, once = 0;
    JSValue signal = JS_UNDEFINED;
    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        if (JS_IsObject(argv[2])) {
            JSValue option = JS_GetPropertyStr(ctx, argv[2], "capture");
            if (JS_IsException(option)) return option;
            capture = JS_ToBool(ctx, option);
            JS_FreeValue(ctx, option);
            if (capture < 0) return JS_EXCEPTION;
            option = JS_GetPropertyStr(ctx, argv[2], "once");
            if (JS_IsException(option)) return option;
            once = JS_ToBool(ctx, option);
            JS_FreeValue(ctx, option);
            if (once < 0) return JS_EXCEPTION;
            signal = JS_GetPropertyStr(ctx, argv[2], "signal");
            if (JS_IsException(signal)) return signal;
            if (!JS_IsObject(signal)) {
                JS_FreeValue(ctx, signal);
                signal = JS_UNDEFINED;
            } else {
                JSValue aborted = JS_GetPropertyStr(ctx, signal, "aborted");
                if (JS_IsException(aborted)) {
                    JS_FreeValue(ctx, signal);
                    return aborted;
                }
                int already_aborted = JS_ToBool(ctx, aborted);
                JS_FreeValue(ctx, aborted);
                if (already_aborted < 0) {
                    JS_FreeValue(ctx, signal);
                    return JS_EXCEPTION;
                }
                if (already_aborted) {
                    JS_FreeValue(ctx, signal);
                    return JS_UNDEFINED;
                }
            }
        } else {
            capture = JS_ToBool(ctx, argv[2]);
            if (capture < 0) return JS_EXCEPTION;
        }
    }
    size_t free_slot = QJS_DOM_MAX_EVENT_LISTENERS;
    for (size_t i = 0; i < qjs_dom_listener_count; ++i) {
        qjs_dom_listener_t *listener = &qjs_dom_listeners[i];
        if (!listener->active) { if (free_slot == QJS_DOM_MAX_EVENT_LISTENERS) free_slot = i; continue; }
        if (listener->node == node_id && listener->capture == capture &&
            strcmp(listener->type, type) == 0 &&
            JS_StrictEq(ctx, listener->callback, argv[1])) {
            JS_FreeValue(ctx, signal);
            return JS_UNDEFINED;
        }
    }
    if (free_slot == QJS_DOM_MAX_EVENT_LISTENERS) {
        if (qjs_dom_listener_count == QJS_DOM_MAX_EVENT_LISTENERS)
            JS_FreeValue(ctx, signal);
        if (qjs_dom_listener_count == QJS_DOM_MAX_EVENT_LISTENERS)
            return JS_ThrowRangeError(ctx, "event listener limit reached");
        free_slot = qjs_dom_listener_count++;
    }
    qjs_dom_listener_t *listener = &qjs_dom_listeners[free_slot];
    listener->node = node_id;
    strcpy(listener->type, type);
    listener->callback = JS_DupValue(ctx, argv[1]);
    listener->signal = signal;
    listener->capture = capture;
    listener->once = once;
    listener->object_listener = JS_IsObject(argv[1]) && !JS_IsFunction(ctx, argv[1]);
    listener->active = 1;
    return JS_UNDEFINED;
}

static JSValue qjs_dom_remove_event_listener(JSContext *ctx,
                                             JSValueConst this_value, int argc,
                                             JSValueConst *argv) {
    liw_web_node_id_t node_id = qjs_dom_receiver_node(ctx, this_value);
    if (node_id == LIW_WEBDOM_NODE_NONE) return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "removeEventListener requires type and function");
    char type[QJS_DOM_MAX_EVENT_TYPE + 1];
    if (qjs_event_type_from_value(ctx, argv[0], type) < 0) return JS_EXCEPTION;
    int capture = 0;
    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        if (JS_IsObject(argv[2])) {
            JSValue option = JS_GetPropertyStr(ctx, argv[2], "capture");
            if (JS_IsException(option)) return option;
            capture = JS_ToBool(ctx, option);
            JS_FreeValue(ctx, option);
        } else capture = JS_ToBool(ctx, argv[2]);
        if (capture < 0) return JS_EXCEPTION;
    }
    for (size_t i = 0; i < qjs_dom_listener_count; ++i) {
        qjs_dom_listener_t *listener = &qjs_dom_listeners[i];
        if (listener->active && listener->node == node_id &&
            listener->capture == capture &&
            strcmp(listener->type, type) == 0 &&
            JS_StrictEq(ctx, listener->callback, argv[1])) {
            JS_FreeValue(ctx, listener->callback);
            JS_FreeValue(ctx, listener->signal);
            listener->callback = JS_UNDEFINED;
            listener->signal = JS_UNDEFINED;
            listener->active = 0;
            break;
        }
    }
    return JS_UNDEFINED;
}

static JSValue qjs_dom_dispatch_at(JSContext *ctx, liw_web_node_id_t node,
                                   qjs_dom_event_t *event, JSValueConst event_value,
                                   int phase, int capture) {
    event->current_target = node;
    event->event_phase = phase;
    JSValue current_object = qjs_dom_wrap_node(ctx, node);
    if (JS_IsException(current_object)) return current_object;
    size_t callback_ids[QJS_DOM_MAX_EVENT_LISTENERS];
    JSValue callbacks[QJS_DOM_MAX_EVENT_LISTENERS];
    size_t count = 0;
    for (size_t i = 0; i < qjs_dom_listener_count; ++i) {
        qjs_dom_listener_t *listener = &qjs_dom_listeners[i];
        if (listener->active && !JS_IsUndefined(listener->signal)) {
            JSValue aborted = JS_GetPropertyStr(ctx, listener->signal, "aborted");
            if (!JS_IsException(aborted) && JS_ToBool(ctx, aborted) == 1) {
                JS_FreeValue(ctx, listener->callback);
                JS_FreeValue(ctx, listener->signal);
                listener->callback = JS_UNDEFINED;
                listener->signal = JS_UNDEFINED;
                listener->active = 0;
                JS_FreeValue(ctx, aborted);
                continue;
            }
            JS_FreeValue(ctx, aborted);
        }
        if (listener->active && listener->node == node &&
            listener->capture == capture && strcmp(listener->type, event->type) == 0) {
            callback_ids[count] = i;
            callbacks[count++] = JS_DupValue(ctx, listener->callback);
        }
    }
    JSValue status = JS_UNDEFINED;
    for (size_t i = 0; i < count; ++i) {
        qjs_dom_listener_t *listener = &qjs_dom_listeners[callback_ids[i]];
        if (listener->once && listener->active) {
            listener->active = 0;
            JS_FreeValue(ctx, listener->callback);
            JS_FreeValue(ctx, listener->signal);
            listener->callback = JS_UNDEFINED;
            listener->signal = JS_UNDEFINED;
        }
        JSValue args[1] = { event_value };
        JSValue result;
        if (listener->object_listener) {
            JSValue handle_event = JS_GetPropertyStr(ctx, callbacks[i], "handleEvent");
            if (JS_IsException(handle_event)) {
                JS_FreeValue(ctx, callbacks[i]);
                for (++i; i < count; ++i) JS_FreeValue(ctx, callbacks[i]);
                status = handle_event;
                break;
            }
            result = JS_Call(ctx, handle_event, callbacks[i], 1, args);
            JS_FreeValue(ctx, handle_event);
        } else {
            result = JS_Call(ctx, callbacks[i], current_object, 1, args);
        }
        JS_FreeValue(ctx, callbacks[i]);
        if (JS_IsException(result)) {
            for (++i; i < count; ++i) JS_FreeValue(ctx, callbacks[i]);
            status = result;
            break;
        }
        JS_FreeValue(ctx, result);
        if (event->immediate_stopped) {
            for (++i; i < count; ++i) JS_FreeValue(ctx, callbacks[i]);
            break;
        }
    }
    JS_FreeValue(ctx, current_object);
    return status;
}

static JSValue qjs_dom_dispatch_event(JSContext *ctx,
                                      JSValueConst this_value, int argc,
                                      JSValueConst *argv) {
    liw_web_node_id_t target_id = qjs_dom_receiver_node(ctx, this_value);
    if (target_id == LIW_WEBDOM_NODE_NONE) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "dispatchEvent requires an event");
    qjs_dom_event_t *event = qjs_get_dom_event(ctx, argv[0]);
    if (!event) return JS_EXCEPTION;
    if (event->dispatching)
        return JS_ThrowTypeError(ctx, "event is already being dispatched");
    liw_web_node_id_t path[LIW_WEBDOM_MAX_DEPTH + 1];
    size_t path_count = 0;
    for (liw_web_node_id_t current = target_id;
         current != LIW_WEBDOM_NODE_NONE && path_count < LIW_WEBDOM_MAX_DEPTH + 1;
         current = liw_webdom_parent(qjs_dom_document, current))
        path[path_count++] = current;
    event->dispatching = 1;
    event->target = target_id;
    event->propagation_stopped = 0;
    event->immediate_stopped = 0;
    JSValue status = JS_UNDEFINED;
    /* Capture travels from the document/root toward the target's parent. */
    for (size_t i = path_count; i > 1; --i) {
        status = qjs_dom_dispatch_at(ctx, path[i - 1], event, argv[0], 1, 1);
        if (JS_IsException(status) || event->propagation_stopped) break;
    }
    if (!JS_IsException(status) && !event->propagation_stopped) {
        status = qjs_dom_dispatch_at(ctx, target_id, event, argv[0], 2, 1);
        if (!JS_IsException(status) && !event->immediate_stopped)
            status = qjs_dom_dispatch_at(ctx, target_id, event, argv[0], 2, 0);
    }
    if (!JS_IsException(status) && event->bubbles && !event->propagation_stopped) {
        for (size_t i = 1; i < path_count; ++i) {
            status = qjs_dom_dispatch_at(ctx, path[i], event, argv[0], 3, 0);
            if (JS_IsException(status) || event->propagation_stopped) break;
        }
    }
    event->current_target = LIW_WEBDOM_NODE_NONE;
    event->event_phase = 0;
    event->dispatching = 0;
    if (JS_IsException(status)) return status;
    return JS_NewBool(ctx, !event->default_prevented);
}

static int qjs_dispatch_native_event(JSContext *ctx, const char *element_id,
                                     const char *type, const char *value) {
    liw_web_node_id_t node_id = liw_webdom_get_element_by_id(
        qjs_dom_document, element_id);
    if (node_id == LIW_WEBDOM_NODE_NONE) return 0;
    if (value && liw_webdom_set_attribute(qjs_dom_document, node_id, "value",
            5, value, strlen(value)) != LIW_WEBDOM_OK) return -1;

    qjs_dom_event_t *event = (qjs_dom_event_t *)calloc(1, sizeof(*event));
    if (!event) return -1;
    if (strlen(type) > QJS_DOM_MAX_EVENT_TYPE) { free(event); return -1; }
    strcpy(event->type, type);
    event->target = LIW_WEBDOM_NODE_NONE;
    event->current_target = LIW_WEBDOM_NODE_NONE;
    event->bubbles = 1;
    event->cancelable = 1;
    JSValue event_object = JS_NewObjectClass(ctx, qjs_dom_event_class);
    if (JS_IsException(event_object)) { free(event); return -1; }
    JS_SetOpaque(event_object, event);
    JSValue target = qjs_dom_wrap_node(ctx, node_id);
    if (JS_IsException(target)) {
        JS_FreeValue(ctx, event_object);
        return -1;
    }
    JSValue args[1] = {event_object};
    JSValue dispatched = qjs_dom_dispatch_event(ctx, target, 1, args);
    int result = JS_IsException(dispatched) ? -1 : JS_ToBool(ctx, dispatched);
    if (JS_IsException(dispatched)) print_exception(ctx);
    JS_FreeValue(ctx, dispatched);
    JS_FreeValue(ctx, target);
    JS_FreeValue(ctx, event_object);
    return result;
}

static void qjs_dom_clear_event_listeners(JSContext *ctx) {
    for (size_t i = 0; i < qjs_dom_listener_count; ++i) {
        if (qjs_dom_listeners[i].active)
            JS_FreeValue(ctx, qjs_dom_listeners[i].callback);
        if (qjs_dom_listeners[i].active)
            JS_FreeValue(ctx, qjs_dom_listeners[i].signal);
        qjs_dom_listeners[i].active = 0;
        qjs_dom_listeners[i].callback = JS_UNDEFINED;
        qjs_dom_listeners[i].signal = JS_UNDEFINED;
    }
    qjs_dom_listener_count = 0;
}

static int qjs_dom_space(char value) {
    return value == ' ' || value == '\t' || value == '\n' ||
           value == '\r' || value == '\f';
}

static int qjs_dom_parse_selector(const char *text, size_t length,
                                  qjs_selector_t *selector) {
    while (length && qjs_dom_space(*text)) { text++; length--; }
    while (length && qjs_dom_space(text[length - 1])) length--;
    if (!length) return -1;
    memset(selector, 0, sizeof(*selector));
    if (length == 1 && text[0] == '*') {
        selector->kind = QJS_SELECTOR_ALL;
        return 0;
    }
    if (text[0] == '#' || text[0] == '.') {
        if (length == 1) return -1;
        selector->kind = text[0] == '#' ? QJS_SELECTOR_ID : QJS_SELECTOR_CLASS;
        selector->name = text + 1;
        selector->name_length = length - 1;
    } else if (text[0] == '[' && length >= 3 && text[length - 1] == ']') {
        const char *start = text + 1;
        size_t inner_length = length - 2;
        const char *equals = NULL;
        qjs_selector_kind_t attr_kind = QJS_SELECTOR_ATTRIBUTE_VALUE;
        for (size_t i = 0; i < inner_length; ++i) {
            if (start[i] == '=') {
                equals = start + i;
                if (i > 0) {
                    switch (start[i - 1]) {
                    case '~': attr_kind = QJS_SELECTOR_ATTRIBUTE_WORD; break;
                    case '|': attr_kind = QJS_SELECTOR_ATTRIBUTE_DASH; break;
                    case '^': attr_kind = QJS_SELECTOR_ATTRIBUTE_PREFIX; break;
                    case '$': attr_kind = QJS_SELECTOR_ATTRIBUTE_SUFFIX; break;
                    case '*': attr_kind = QJS_SELECTOR_ATTRIBUTE_SUBSTRING; break;
                    default: break;
                    }
                }
                break;
            }
        }
        if (!equals) {
            selector->kind = QJS_SELECTOR_ATTRIBUTE;
            selector->name = start;
            selector->name_length = inner_length;
            while (selector->name_length && qjs_dom_space(*selector->name)) {
                selector->name++;
                selector->name_length--;
            }
            while (selector->name_length &&
                   qjs_dom_space(selector->name[selector->name_length - 1]))
                selector->name_length--;
        } else {
            selector->kind = attr_kind;
            selector->name = start;
            selector->name_length = (size_t)(equals - start);
            if (attr_kind != QJS_SELECTOR_ATTRIBUTE_VALUE)
                selector->name_length--;
            selector->value = equals + 1;
            size_t operator_length =
                attr_kind == QJS_SELECTOR_ATTRIBUTE_VALUE ? 1U : 2U;
            if (inner_length < selector->name_length + operator_length)
                return -1;
            selector->value_length = inner_length - selector->name_length -
                                     operator_length;
            while (selector->name_length && qjs_dom_space(*selector->name)) {
                selector->name++;
                selector->name_length--;
            }
            while (selector->name_length &&
                   qjs_dom_space(selector->name[selector->name_length - 1]))
                selector->name_length--;
            while (selector->value_length && qjs_dom_space(*selector->value)) {
                selector->value++;
                selector->value_length--;
            }
            while (selector->value_length &&
                   qjs_dom_space(selector->value[selector->value_length - 1]))
                selector->value_length--;
            if (selector->value_length >= 2 &&
                qjs_dom_space(selector->value[selector->value_length - 2]) &&
                (selector->value[selector->value_length - 1] == 'i' ||
                 selector->value[selector->value_length - 1] == 'I' ||
                 selector->value[selector->value_length - 1] == 's' ||
                 selector->value[selector->value_length - 1] == 'S')) {
                char modifier = selector->value[selector->value_length - 1];
                selector->value_case_insensitive = modifier == 'i' || modifier == 'I';
                selector->value_length -= 2;
                while (selector->value_length &&
                       qjs_dom_space(selector->value[selector->value_length - 1]))
                    selector->value_length--;
            }
            if (selector->value_length >= 2 &&
                ((selector->value[0] == '"' && selector->value[selector->value_length - 1] == '"') ||
                 (selector->value[0] == '\'' && selector->value[selector->value_length - 1] == '\''))) {
                selector->value++;
                selector->value_length -= 2;
            }
            if (!selector->name_length) return -1;
        }
    } else {
        selector->kind = QJS_SELECTOR_TAG;
        selector->name = text;
        selector->name_length = length;
    }
    for (size_t i = 0; i < selector->name_length; ++i) {
        char c = selector->name[i];
        if (qjs_dom_space(c) || c == ',' || c == ':' || c == '>' ||
            c == '+' || c == '~' || c == '[' || c == ']' || c == '#' ||
            c == '=' || c == '/')
            return -1;
    }
    if (selector->kind >= QJS_SELECTOR_ATTRIBUTE_VALUE &&
        selector->kind <= QJS_SELECTOR_ATTRIBUTE_SUBSTRING) {
        for (size_t i = 0; i < selector->value_length; ++i) {
            char c = selector->value[i];
            if (c == '[' || c == ']' || c == '"' || c == '\'') return -1;
        }
    }
    return 0;
}

static int qjs_slice_equal(const char *left, size_t left_length,
                           const char *right, size_t right_length,
                           int case_insensitive) {
    if (left_length != right_length) return 0;
    for (size_t i = 0; i < left_length; ++i) {
        char a = left[i], b = right[i];
        if (case_insensitive) {
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        }
        if (a != b) return 0;
    }
    return 1;
}

static int qjs_dom_matches(const liw_web_document_t *doc,
                           liw_web_node_id_t node,
                           const qjs_selector_t *selector) {
    if (liw_webdom_node_type(doc, node) != LIW_WEBDOM_ELEMENT) return 0;
    if (selector->kind == QJS_SELECTOR_ALL) return 1;
    if (selector->kind == QJS_SELECTOR_TAG) {
        liw_web_slice_t name = liw_webdom_name(doc, node);
        return qjs_slice_equal(name.data, name.length, selector->name,
                               selector->name_length, 1);
    }
    if (selector->kind == QJS_SELECTOR_ID ||
        selector->kind == QJS_SELECTOR_CLASS) {
        liw_web_slice_t value;
        const char *attribute = selector->kind == QJS_SELECTOR_ID ? "id" : "class";
        if (liw_webdom_get_attribute(doc, node, attribute, &value) != LIW_WEBDOM_OK)
            return 0;
        if (selector->kind == QJS_SELECTOR_ID)
            return qjs_slice_equal(value.data, value.length, selector->name,
                                   selector->name_length, 0);
        size_t offset = 0;
        while (offset < value.length) {
            while (offset < value.length && qjs_dom_space(value.data[offset])) offset++;
            size_t start = offset;
            while (offset < value.length && !qjs_dom_space(value.data[offset])) offset++;
            if (qjs_slice_equal(value.data + start, offset - start,
                                selector->name, selector->name_length, 0)) return 1;
        }
        return 0;
    }
    liw_web_slice_t value = {0};
    int found = 0;
    size_t count = liw_webdom_attribute_count(doc, node);
    for (size_t i = 0; i < count; ++i) {
        liw_web_slice_t attr_name, attr_value;
        if (liw_webdom_attribute_at(doc, node, i, &attr_name, &attr_value) ==
                LIW_WEBDOM_OK &&
            qjs_slice_equal(attr_name.data, attr_name.length, selector->name,
                            selector->name_length, 1)) {
            value = attr_value;
            found = 1;
            break;
        }
    }
    if (!found) return 0;
    if (selector->kind == QJS_SELECTOR_ATTRIBUTE) return 1;
    if (selector->kind == QJS_SELECTOR_ATTRIBUTE_VALUE)
        return qjs_slice_equal(value.data, value.length, selector->value,
                               selector->value_length,
                               selector->value_case_insensitive);
    if (selector->kind == QJS_SELECTOR_ATTRIBUTE_WORD) {
        size_t pos = 0;
        while (pos < value.length) {
            while (pos < value.length && qjs_dom_space(value.data[pos])) pos++;
            size_t start = pos;
            while (pos < value.length && !qjs_dom_space(value.data[pos])) pos++;
            if (qjs_slice_equal(value.data + start, pos - start,
                                selector->value, selector->value_length,
                                selector->value_case_insensitive)) return 1;
        }
        return 0;
    }
    if (selector->kind == QJS_SELECTOR_ATTRIBUTE_DASH)
        return qjs_slice_equal(value.data, value.length, selector->value,
                               selector->value_length,
                               selector->value_case_insensitive) ||
            (value.length > selector->value_length &&
             value.data[selector->value_length] == '-' &&
             qjs_slice_equal(value.data, selector->value_length,
                             selector->value, selector->value_length,
                             selector->value_case_insensitive));
    if (selector->kind == QJS_SELECTOR_ATTRIBUTE_PREFIX)
        return value.length >= selector->value_length &&
            qjs_slice_equal(value.data, selector->value_length,
                            selector->value, selector->value_length,
                            selector->value_case_insensitive);
    if (selector->kind == QJS_SELECTOR_ATTRIBUTE_SUFFIX)
        return value.length >= selector->value_length &&
            qjs_slice_equal(value.data + value.length - selector->value_length,
                            selector->value_length, selector->value,
                            selector->value_length,
                            selector->value_case_insensitive);
    if (selector->kind == QJS_SELECTOR_ATTRIBUTE_SUBSTRING) {
        if (!selector->value_length || value.length < selector->value_length) return 0;
        for (size_t i = 0; i <= value.length - selector->value_length; ++i)
            if (qjs_slice_equal(value.data + i, selector->value_length,
                                selector->value, selector->value_length,
                                selector->value_case_insensitive)) return 1;
    }
    return 0;
}

static int qjs_dom_matches_selector(const liw_web_document_t *doc,
                                    liw_web_node_id_t node,
                                    const char *text, size_t length);

static int qjs_dom_attr_present(const liw_web_document_t *doc,
                                liw_web_node_id_t node, const char *name) {
    liw_web_slice_t ignored;
    return liw_webdom_get_attribute(doc, node, name, &ignored) == LIW_WEBDOM_OK;
}

static int qjs_dom_is_tag(const liw_web_document_t *doc, liw_web_node_id_t node,
                          const char *tag) {
    liw_web_slice_t name = liw_webdom_name(doc, node);
    return qjs_slice_equal(name.data, name.length, tag, strlen(tag), 1);
}

static int qjs_dom_nth_matches(const liw_web_document_t *doc,
                               liw_web_node_id_t node,
                               const char *expression, size_t length,
                               int from_end, int of_type) {
    while (length && qjs_dom_space(*expression)) { expression++; length--; }
    while (length && qjs_dom_space(expression[length - 1])) length--;
    int a = 0, b = 0, has_n = 0;
    if (length == 3 && qjs_slice_equal(expression, length, "odd", 3, 1)) {
        a = 2; b = 1; has_n = 1;
    } else if (length == 4 && qjs_slice_equal(expression, length, "even", 4, 1)) {
        a = 2; has_n = 1;
    } else {
        size_t npos = 0;
        while (npos < length && expression[npos] != 'n' && expression[npos] != 'N') npos++;
        if (npos == length) {
            if (!length) return 0;
            int sign = 1; size_t i = 0;
            if (expression[i] == '+') i++;
            else if (expression[i] == '-') { sign = -1; i++; }
            if (i == length) return 0;
            int value = 0;
            for (; i < length; ++i) {
                if (expression[i] < '0' || expression[i] > '9') return 0;
                if (value > 1000000) return 0;
                value = value * 10 + (expression[i] - '0');
            }
            b = sign * value;
        } else {
            has_n = 1;
            size_t i = 0;
            if (npos == 0 || (npos == 1 && expression[0] == '+')) a = 1;
            else if (npos == 1 && expression[0] == '-') a = -1;
            else {
                int sign = 1;
                if (expression[i] == '+') i++;
                else if (expression[i] == '-') { sign = -1; i++; }
                if (i == npos) return 0;
                for (; i < npos; ++i) {
                    if (expression[i] < '0' || expression[i] > '9') return 0;
                    a = a * 10 + (expression[i] - '0');
                    if (a > 1000000) return 0;
                }
                a *= sign;
            }
            i = npos + 1;
            if (i < length) {
                int sign = 1;
                if (expression[i] == '+') i++;
                else if (expression[i] == '-') { sign = -1; i++; }
                if (i == length) return 0;
                for (; i < length; ++i) {
                    if (expression[i] < '0' || expression[i] > '9') return 0;
                    b = b * 10 + (expression[i] - '0');
                    if (b > 1000000) return 0;
                }
                b *= sign;
            }
        }
    }
    uint32_t parent = liw_webdom_parent(doc, node);
    if (parent == LIW_WEBDOM_NODE_NONE) return 0;
    uint32_t position = 0, count = 0;
    liw_web_slice_t name = liw_webdom_name(doc, node);
    for (uint32_t child = liw_webdom_first_child(doc, parent);
         child != LIW_WEBDOM_NODE_NONE;
         child = liw_webdom_next_sibling(doc, child)) {
        if (liw_webdom_node_type(doc, child) != LIW_WEBDOM_ELEMENT) continue;
        if (of_type && !qjs_slice_equal(name.data, name.length,
                liw_webdom_name(doc, child).data,
                liw_webdom_name(doc, child).length, 1)) continue;
        count++;
        if (child == node) position = count;
    }
    if (!position) return 0;
    if (from_end) position = count - position + 1;
    if (!has_n) return (int)position == b;
    int diff = (int)position - b;
    if (a == 0) return diff == 0;
    return diff % a == 0 && diff / a >= 0;
}

static int qjs_dom_match_pseudo(const liw_web_document_t *doc,
                                liw_web_node_id_t node,
                                const char *name, size_t name_length,
                                const char *argument, size_t argument_length) {
    uint32_t parent = liw_webdom_parent(doc, node);
    if (qjs_slice_equal(name, name_length, "root", 4, 1))
        return parent == liw_webdom_root(doc);
    if (qjs_slice_equal(name, name_length, "empty", 5, 1)) {
        for (uint32_t c = liw_webdom_first_child(doc, node);
             c != LIW_WEBDOM_NODE_NONE; c = liw_webdom_next_sibling(doc, c))
            if (liw_webdom_node_type(doc, c) == LIW_WEBDOM_ELEMENT ||
                liw_webdom_node_type(doc, c) == LIW_WEBDOM_TEXT) return 0;
        return 1;
    }
    if (qjs_slice_equal(name, name_length, "first-child", 11, 1) ||
        qjs_slice_equal(name, name_length, "last-child", 10, 1) ||
        qjs_slice_equal(name, name_length, "only-child", 10, 1) ||
        qjs_slice_equal(name, name_length, "first-of-type", 13, 1) ||
        qjs_slice_equal(name, name_length, "last-of-type", 12, 1) ||
        qjs_slice_equal(name, name_length, "only-of-type", 12, 1)) {
        int of_type = qjs_slice_equal(name, name_length, "first-of-type", 13, 1) ||
                      qjs_slice_equal(name, name_length, "last-of-type", 12, 1) ||
                      qjs_slice_equal(name, name_length, "only-of-type", 12, 1);
        liw_web_slice_t own_name = liw_webdom_name(doc, node);
        uint32_t first = LIW_WEBDOM_NODE_NONE, last = LIW_WEBDOM_NODE_NONE;
        for (uint32_t c = liw_webdom_first_child(doc, parent);
             c != LIW_WEBDOM_NODE_NONE; c = liw_webdom_next_sibling(doc, c)) {
            if (liw_webdom_node_type(doc, c) != LIW_WEBDOM_ELEMENT) continue;
            if (of_type) {
                liw_web_slice_t candidate = liw_webdom_name(doc, c);
                if (!qjs_slice_equal(own_name.data, own_name.length,
                                     candidate.data, candidate.length, 1)) continue;
            }
            if (first == LIW_WEBDOM_NODE_NONE) first = c;
            last = c;
        }
        if (qjs_slice_equal(name, name_length, "first-child", 11, 1) ||
            qjs_slice_equal(name, name_length, "first-of-type", 13, 1)) return first == node;
        if (qjs_slice_equal(name, name_length, "last-child", 10, 1) ||
            qjs_slice_equal(name, name_length, "last-of-type", 12, 1)) return last == node;
        return first == node && last == node;
    }
    if (qjs_slice_equal(name, name_length, "nth-child", 9, 1))
        return qjs_dom_nth_matches(doc, node, argument, argument_length, 0, 0);
    if (qjs_slice_equal(name, name_length, "nth-last-child", 14, 1))
        return qjs_dom_nth_matches(doc, node, argument, argument_length, 1, 0);
    if (qjs_slice_equal(name, name_length, "nth-of-type", 11, 1))
        return qjs_dom_nth_matches(doc, node, argument, argument_length, 0, 1);
    if (qjs_slice_equal(name, name_length, "nth-last-of-type", 16, 1))
        return qjs_dom_nth_matches(doc, node, argument, argument_length, 1, 1);
    if (qjs_slice_equal(name, name_length, "checked", 7, 1))
        return (qjs_dom_is_tag(doc, node, "input") &&
                qjs_dom_attr_present(doc, node, "checked")) ||
               (qjs_dom_is_tag(doc, node, "option") &&
                qjs_dom_attr_present(doc, node, "selected"));
    if (qjs_slice_equal(name, name_length, "disabled", 8, 1))
        return (qjs_dom_is_tag(doc, node, "button") || qjs_dom_is_tag(doc, node, "input") ||
                qjs_dom_is_tag(doc, node, "select") || qjs_dom_is_tag(doc, node, "textarea") ||
                qjs_dom_is_tag(doc, node, "option") || qjs_dom_is_tag(doc, node, "optgroup") ||
                qjs_dom_is_tag(doc, node, "fieldset") || qjs_dom_is_tag(doc, node, "menuitem")) &&
               qjs_dom_attr_present(doc, node, "disabled");
    if (qjs_slice_equal(name, name_length, "enabled", 7, 1))
        return (qjs_dom_is_tag(doc, node, "input") || qjs_dom_is_tag(doc, node, "button") ||
                qjs_dom_is_tag(doc, node, "select") || qjs_dom_is_tag(doc, node, "textarea") ||
                qjs_dom_is_tag(doc, node, "option") || qjs_dom_is_tag(doc, node, "optgroup") ||
                qjs_dom_is_tag(doc, node, "fieldset")) &&
               !qjs_dom_attr_present(doc, node, "disabled");
    if (qjs_slice_equal(name, name_length, "required", 8, 1))
        return qjs_dom_attr_present(doc, node, "required");
    if (qjs_slice_equal(name, name_length, "optional", 8, 1))
        return (qjs_dom_is_tag(doc, node, "input") || qjs_dom_is_tag(doc, node, "select") ||
                qjs_dom_is_tag(doc, node, "textarea")) &&
               !qjs_dom_attr_present(doc, node, "required");
    if (qjs_slice_equal(name, name_length, "link", 4, 1) ||
        qjs_slice_equal(name, name_length, "any-link", 8, 1))
        return (qjs_dom_is_tag(doc, node, "a") || qjs_dom_is_tag(doc, node, "area") ||
                qjs_dom_is_tag(doc, node, "link")) &&
               qjs_dom_attr_present(doc, node, "href");
    if (qjs_slice_equal(name, name_length, "not", 3, 1) ||
        qjs_slice_equal(name, name_length, "is", 2, 1) ||
        qjs_slice_equal(name, name_length, "where", 5, 1)) {
        int matches = qjs_dom_matches_selector(doc, node, argument, argument_length);
        if (qjs_slice_equal(name, name_length, "not", 3, 1)) return matches == 0;
        return matches > 0;
    }
    return 0;
}

static int qjs_dom_match_compound(const liw_web_document_t *doc,
                                  liw_web_node_id_t node,
                                  const char *text, size_t length) {
    if (liw_webdom_node_type(doc, node) != LIW_WEBDOM_ELEMENT || !length) return 0;
    size_t pos = 0;
    int saw = 0;
    while (pos < length) {
        size_t start = pos, end = pos;
        if (text[pos] == ':') {
            size_t name_start = ++pos;
            if (pos == length || text[pos] == ':') return 0;
            while (pos < length && ((text[pos] >= 'a' && text[pos] <= 'z') ||
                   (text[pos] >= 'A' && text[pos] <= 'Z') ||
                   (text[pos] >= '0' && text[pos] <= '9') || text[pos] == '-')) pos++;
            size_t name_length = pos - name_start;
            const char *argument = NULL;
            size_t argument_length = 0;
            if (pos < length && text[pos] == '(') {
                size_t arg_start = ++pos;
                int depth = 1, quote = 0;
                while (pos < length && depth) {
                    char c = text[pos++];
                    if (quote) { if (c == quote) quote = 0; continue; }
                    if (c == '\'' || c == '"') { quote = c; continue; }
                    if (c == '(') depth++;
                    else if (c == ')') depth--;
                }
                if (depth || quote) return 0;
                argument = text + arg_start;
                argument_length = pos - arg_start - 1;
            }
            if (!name_length || qjs_dom_match_pseudo(doc, node,
                    text + name_start, name_length, argument, argument_length) == 0)
                return 0;
            saw = 1;
            continue;
        } else if (text[pos] == '[') {
            int quote = 0;
            ++end;
            while (end < length) {
                char c = text[end++];
                if (quote) { if (c == quote) quote = 0; }
                else if (c == '\'' || c == '"') quote = c;
                else if (c == ']') break;
            }
            if (end > length || text[end - 1] != ']') return 0;
        } else if (text[pos] == '#' || text[pos] == '.') {
            ++end;
            while (end < length && text[end] != '#' && text[end] != '.' &&
                   text[end] != '[' && text[end] != ':') ++end;
        } else {
            while (end < length && text[end] != '#' && text[end] != '.' &&
                   text[end] != '[' && text[end] != ':') ++end;
        }
        if (end == start) return 0;
        qjs_selector_t simple;
        if (qjs_dom_parse_selector(text + start, end - start, &simple) < 0 ||
            !qjs_dom_matches(doc, node, &simple)) return 0;
        saw = 1;
        pos = end;
    }
    return saw;
}

typedef struct {
    const char *text[16];
    size_t length[16];
    uint8_t relation[16]; /* 0 descendant, 1 child, 2 adjacent, 3 general sibling */
    size_t count;
} qjs_selector_chain_t;

static int qjs_dom_parse_chain(const char *text, size_t length,
                               qjs_selector_chain_t *chain) {
    memset(chain, 0, sizeof(*chain));
    size_t pos = 0;
    int relation = 0;
    while (pos < length) {
        int had_space = 0;
        while (pos < length && qjs_dom_space(text[pos])) { pos++; had_space = 1; }
        if (pos == length) break;
        if (text[pos] == '>' || text[pos] == '+' || text[pos] == '~') {
            if (chain->count == 0) return -1;
            relation = text[pos] == '>' ? 1 : text[pos] == '+' ? 2 : 3;
            pos++;
            while (pos < length && qjs_dom_space(text[pos])) pos++;
            if (pos == length || text[pos] == '>' || text[pos] == '+' ||
                text[pos] == '~') return -1;
        } else if (had_space && chain->count) {
            relation = 0;
        }
        if (chain->count == 16) return -1;
        size_t start = pos;
        int bracket = 0, paren = 0, quote = 0;
        while (pos < length) {
            char c = text[pos];
            if (quote) { if (c == quote) quote = 0; pos++; continue; }
            if ((bracket || paren) && (c == '\'' || c == '"')) { quote = c; pos++; continue; }
            if (c == '[') { bracket++; pos++; continue; }
            if (c == ']') { if (!bracket) return -1; bracket--; pos++; continue; }
            if (!bracket && c == '(') { paren++; pos++; continue; }
            if (!bracket && c == ')') { if (!paren) return -1; paren--; pos++; continue; }
            if (!bracket && !paren && (c == '>' || c == '+' || c == '~' ||
                             qjs_dom_space(c))) break;
            pos++;
        }
        if (bracket || paren || quote || pos == start) return -1;
        qjs_selector_chain_t *out = chain;
        out->text[out->count] = text + start;
        out->length[out->count] = pos - start;
        out->relation[out->count] = (uint8_t)relation;
        out->count++;
        relation = 0;
    }
    return chain->count ? 0 : -1;
}

static int qjs_dom_match_chain_from(const liw_web_document_t *doc,
                                    liw_web_node_id_t node,
                                    const qjs_selector_chain_t *chain,
                                    size_t index) {
    if (!qjs_dom_match_compound(doc, node, chain->text[index], chain->length[index]))
        return 0;
    if (index == 0) return 1;
    liw_web_node_id_t parent = liw_webdom_parent(doc, node);
    if (chain->relation[index] == 1)
        return parent != LIW_WEBDOM_NODE_NONE &&
            qjs_dom_match_chain_from(doc, parent, chain, index - 1);
    if (chain->relation[index] == 2 || chain->relation[index] == 3) {
        liw_web_node_id_t sibling = liw_webdom_previous_sibling(doc, node);
        while (sibling != LIW_WEBDOM_NODE_NONE) {
            if (liw_webdom_node_type(doc, sibling) == LIW_WEBDOM_ELEMENT) {
                if (qjs_dom_match_chain_from(doc, sibling, chain, index - 1)) return 1;
                if (chain->relation[index] == 2) return 0;
            }
            sibling = liw_webdom_previous_sibling(doc, sibling);
        }
        return 0;
    }
    size_t depth = 0;
    while (parent != LIW_WEBDOM_NODE_NONE && depth++ < LIW_WEBDOM_MAX_DEPTH) {
        if (qjs_dom_match_chain_from(doc, parent, chain, index - 1)) return 1;
        parent = liw_webdom_parent(doc, parent);
    }
    return 0;
}

static int qjs_dom_matches_selector(const liw_web_document_t *doc,
                                    liw_web_node_id_t node,
                                    const char *text, size_t length) {
    size_t start = 0;
    int bracket = 0, paren = 0, quote = 0;
    for (size_t pos = 0; pos <= length; ++pos) {
        char c = pos < length ? text[pos] : ',';
        if (quote) { if (c == quote) quote = 0; continue; }
        if ((bracket || paren) && (c == '\'' || c == '"')) { quote = c; continue; }
        if (c == '[') { bracket++; continue; }
        if (c == ']') { if (!bracket) return -1; bracket--; continue; }
        if (!bracket && c == '(') { paren++; continue; }
        if (!bracket && c == ')') { if (!paren) return -1; paren--; continue; }
        if (c != ',' || bracket || paren) continue;
        size_t left = start, right = pos;
        while (left < right && qjs_dom_space(text[left])) left++;
        while (right > left && qjs_dom_space(text[right - 1])) right--;
        qjs_selector_chain_t chain;
        if (left == right || qjs_dom_parse_chain(text + left, right - left, &chain) < 0)
            return -1;
        if (qjs_dom_match_chain_from(doc, node, &chain, chain.count - 1)) return 1;
        start = pos + 1;
    }
    return bracket || paren || quote ? -1 : 0;
}

static liw_web_node_id_t qjs_dom_next_after_subtree(
    const liw_web_document_t *doc, liw_web_node_id_t root,
    liw_web_node_id_t node) {
    while (node != LIW_WEBDOM_NODE_NONE && node != root) {
        liw_web_node_id_t sibling = liw_webdom_next_sibling(doc, node);
        if (sibling != LIW_WEBDOM_NODE_NONE) return sibling;
        node = liw_webdom_parent(doc, node);
    }
    return LIW_WEBDOM_NODE_NONE;
}

static JSValue qjs_dom_query(JSContext *ctx, liw_web_node_id_t root,
                             JSValueConst selector_value, int all) {
    size_t selector_length = 0;
    const char *selector_text = JS_ToCStringLen(ctx, &selector_length,
                                                selector_value);
    if (!selector_text) return JS_EXCEPTION;
    if (qjs_dom_matches_selector(qjs_dom_document, root,
                                 selector_text, selector_length) < 0) {
        JS_FreeCString(ctx, selector_text);
        return JS_ThrowSyntaxError(ctx, "unsupported or invalid CSS selector");
    }
    JSValue matches = all ? JS_NewArray(ctx) : JS_UNDEFINED;
    if (all && JS_IsException(matches)) {
        JS_FreeCString(ctx, selector_text);
        return matches;
    }
    uint32_t match_count = 0;
    liw_web_node_id_t node = liw_webdom_first_child(qjs_dom_document, root);
    size_t visited = 0;
    while (node != LIW_WEBDOM_NODE_NONE &&
           visited++ < LIW_WEBDOM_MAX_NODES) {
        if (qjs_dom_matches_selector(qjs_dom_document, node,
                                     selector_text, selector_length) > 0) {
            if (!all) {
                JSValue result = qjs_dom_wrap_node(ctx, node);
                JS_FreeCString(ctx, selector_text);
                return result;
            }
            JSValue wrapped = qjs_dom_wrap_node(ctx, node);
            if (JS_IsException(wrapped) ||
                JS_SetPropertyUint32(ctx, matches, match_count++, wrapped) < 0) {
                JS_FreeCString(ctx, selector_text);
                JS_FreeValue(ctx, matches);
                return JS_EXCEPTION;
            }
        }
        liw_web_node_id_t child = liw_webdom_first_child(qjs_dom_document, node);
        node = child != LIW_WEBDOM_NODE_NONE
            ? child : qjs_dom_next_after_subtree(qjs_dom_document, root, node);
    }
    JS_FreeCString(ctx, selector_text);
    if (visited >= LIW_WEBDOM_MAX_NODES && node != LIW_WEBDOM_NODE_NONE) {
        if (all) JS_FreeValue(ctx, matches);
        return JS_ThrowRangeError(ctx, "DOM selector traversal limit exceeded");
    }
    return all ? matches : JS_NULL;
}

static JSValue qjs_dom_node_query(JSContext *ctx, JSValueConst this_value,
                                  int argc, JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "selector is required");
    return qjs_dom_query(ctx, node->node, argv[0], 0);
}

static JSValue qjs_dom_node_query_all(JSContext *ctx, JSValueConst this_value,
                                      int argc, JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "selector is required");
    return qjs_dom_query(ctx, node->node, argv[0], 1);
}

static JSValue qjs_dom_matches_method(JSContext *ctx, JSValueConst this_value,
                                     int argc, JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "selector is required");
    size_t length = 0;
    const char *selector = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!selector) return JS_EXCEPTION;
    int matched = qjs_dom_matches_selector(node->document, node->node,
                                            selector, length);
    JS_FreeCString(ctx, selector);
    if (matched < 0) return JS_ThrowSyntaxError(ctx, "unsupported CSS selector");
    return JS_NewBool(ctx, matched);
}

static JSValue qjs_dom_closest(JSContext *ctx, JSValueConst this_value,
                               int argc, JSValueConst *argv) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "selector is required");
    size_t length = 0;
    const char *selector = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!selector) return JS_EXCEPTION;
    for (liw_web_node_id_t current = node->node;
         current != LIW_WEBDOM_NODE_NONE;
         current = liw_webdom_parent(node->document, current)) {
        int matched = qjs_dom_matches_selector(node->document, current,
                                                selector, length);
        if (matched < 0) {
            JS_FreeCString(ctx, selector);
            return JS_ThrowSyntaxError(ctx, "unsupported CSS selector");
        }
        if (matched) {
            JS_FreeCString(ctx, selector);
            return qjs_dom_wrap_node(ctx, current);
        }
    }
    JS_FreeCString(ctx, selector);
    return JS_NULL;
}

static JSValue qjs_dom_document_query(JSContext *ctx, JSValueConst this_value,
                                      int argc, JSValueConst *argv) {
    (void)this_value;
    if (argc < 1) return JS_ThrowTypeError(ctx, "selector is required");
    return qjs_dom_query(ctx, liw_webdom_root(qjs_dom_document), argv[0], 0);
}

static JSValue qjs_dom_document_query_all(JSContext *ctx, JSValueConst this_value,
                                          int argc, JSValueConst *argv) {
    (void)this_value;
    if (argc < 1) return JS_ThrowTypeError(ctx, "selector is required");
    return qjs_dom_query(ctx, liw_webdom_root(qjs_dom_document), argv[0], 1);
}

static JSValue qjs_dom_get_elements_by_tag(JSContext *ctx,
                                           JSValueConst this_value,
                                           int argc, JSValueConst *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "tag name is required");
    liw_web_node_id_t root = liw_webdom_root(qjs_dom_document);
    qjs_dom_node_t *node = (qjs_dom_node_t *)JS_GetOpaque(
        this_value, qjs_dom_node_class);
    if (node) {
        if (node->document != qjs_dom_document)
            return JS_ThrowTypeError(ctx, "DOM node belongs to another document");
        root = node->node;
    }
    return qjs_dom_query(ctx, root, argv[0], 1);
}

static int qjs_dom_has_class_tokens(const liw_web_document_t *doc,
                                   liw_web_node_id_t node,
                                   const char *tokens, size_t length) {
    liw_web_slice_t classes;
    if (liw_webdom_get_attribute(doc, node, "class", &classes) != LIW_WEBDOM_OK)
        return 0;
    size_t offset = 0;
    while (offset < length) {
        while (offset < length && qjs_dom_space(tokens[offset])) offset++;
        size_t start = offset;
        while (offset < length && !qjs_dom_space(tokens[offset])) offset++;
        if (start == offset) continue;
        int found = 0;
        size_t class_offset = 0;
        while (class_offset < classes.length) {
            while (class_offset < classes.length && qjs_dom_space(classes.data[class_offset])) class_offset++;
            size_t class_start = class_offset;
            while (class_offset < classes.length && !qjs_dom_space(classes.data[class_offset])) class_offset++;
            if (qjs_slice_equal(classes.data + class_start,
                                class_offset - class_start, tokens + start,
                                offset - start, 0)) { found = 1; break; }
        }
        if (!found) return 0;
    }
    return 1;
}

static JSValue qjs_dom_get_elements_by_class(JSContext *ctx,
                                             JSValueConst this_value,
                                             int argc, JSValueConst *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "class name is required");
    size_t length = 0;
    const char *tokens = JS_ToCStringLen(ctx, &length, argv[0]);
    if (!tokens) return JS_EXCEPTION;
    liw_web_node_id_t root = liw_webdom_root(qjs_dom_document);
    qjs_dom_node_t *node = (qjs_dom_node_t *)JS_GetOpaque(
        this_value, qjs_dom_node_class);
    if (node) {
        if (node->document != qjs_dom_document) {
            JS_FreeCString(ctx, tokens);
            return JS_ThrowTypeError(ctx, "DOM node belongs to another document");
        }
        root = node->node;
    }
    size_t first_token = 0;
    while (first_token < length && qjs_dom_space(tokens[first_token])) first_token++;
    if (first_token == length) { JS_FreeCString(ctx, tokens); return JS_NewArray(ctx); }
    JSValue matches = JS_NewArray(ctx);
    if (JS_IsException(matches)) { JS_FreeCString(ctx, tokens); return matches; }
    liw_web_node_id_t current = liw_webdom_first_child(qjs_dom_document, root);
    size_t visited = 0;
    uint32_t index = 0;
    while (current != LIW_WEBDOM_NODE_NONE && visited++ < LIW_WEBDOM_MAX_NODES) {
        if (liw_webdom_node_type(qjs_dom_document, current) == LIW_WEBDOM_ELEMENT &&
            qjs_dom_has_class_tokens(qjs_dom_document, current, tokens, length)) {
            JSValue wrapped = qjs_dom_wrap_node(ctx, current);
            if (JS_IsException(wrapped) ||
                JS_SetPropertyUint32(ctx, matches, index++, wrapped) < 0) {
                JS_FreeCString(ctx, tokens);
                JS_FreeValue(ctx, matches);
                return JS_EXCEPTION;
            }
        }
        liw_web_node_id_t child = liw_webdom_first_child(qjs_dom_document, current);
        current = child != LIW_WEBDOM_NODE_NONE ? child
            : qjs_dom_next_after_subtree(qjs_dom_document, root, current);
    }
    JS_FreeCString(ctx, tokens);
    if (visited >= LIW_WEBDOM_MAX_NODES && current != LIW_WEBDOM_NODE_NONE) {
        JS_FreeValue(ctx, matches);
        return JS_ThrowRangeError(ctx, "DOM traversal limit exceeded");
    }
    return matches;
}

static JSValue qjs_dom_node_text(JSContext *ctx, JSValueConst this_value) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    liw_web_node_type_t node_type = liw_webdom_node_type(node->document, node->node);
    if (node_type == LIW_WEBDOM_TEXT || node_type == LIW_WEBDOM_COMMENT) {
        liw_web_slice_t text = liw_webdom_value(node->document, node->node);
        return qjs_dom_decode_slice(ctx, text,
            liw_webdom_text_needs_entity_decode(node->document, node->node));
    }

    size_t capacity = 64, length = 0, steps = 0;
    char *text = (char *)malloc(capacity);
    if (!text) return JS_ThrowOutOfMemory(ctx);
    liw_web_node_id_t current = liw_webdom_first_child(node->document, node->node);
    while (current != LIW_WEBDOM_NODE_NONE && ++steps <= LIW_WEBDOM_MAX_NODES * 2U) {
        if (liw_webdom_node_type(node->document, current) == LIW_WEBDOM_TEXT) {
            liw_web_slice_t part = liw_webdom_value(node->document, current);
            size_t decoded_length = 0;
            int needs_decode = liw_webdom_text_needs_entity_decode(
                node->document, current);
            decoded_length = part.length;
            if ((needs_decode &&
                 liw_webdom_decode_entities(part.data, part.length, NULL, 0,
                                             &decoded_length) != LIW_WEBDOM_OK) ||
                decoded_length > QJS_MAX_DOM_TEXT_BYTES - length) {
                free(text);
                return JS_ThrowRangeError(ctx, "DOM text limit exceeded");
            }
            size_t required = length + decoded_length + 1;
            if (required > capacity) {
                size_t next = capacity;
                while (next < required) next *= 2;
                if (next > QJS_MAX_DOM_TEXT_BYTES + 1U)
                    next = QJS_MAX_DOM_TEXT_BYTES + 1U;
                char *grown = (char *)realloc(text, next);
                if (!grown) {
                    free(text);
                    return JS_ThrowOutOfMemory(ctx);
                }
                text = grown;
                capacity = next;
            }
            size_t actual_length = 0;
            int decode_rc = needs_decode
                ? liw_webdom_decode_entities(part.data, part.length,
                    text + length, capacity - length, &actual_length)
                : LIW_WEBDOM_OK;
            if (!needs_decode) {
                memcpy(text + length, part.data, part.length);
                actual_length = part.length;
            }
            if (decode_rc != LIW_WEBDOM_OK) {
                free(text);
                return JS_ThrowInternalError(ctx, "cannot decode HTML text");
            }
            length += actual_length;
        }

        liw_web_node_id_t child = liw_webdom_first_child(node->document, current);
        if (child != LIW_WEBDOM_NODE_NONE) {
            current = child;
            continue;
        }
        while (current != node->node &&
               liw_webdom_next_sibling(node->document, current) == LIW_WEBDOM_NODE_NONE) {
            current = liw_webdom_parent(node->document, current);
        }
        if (current == node->node) break;
        current = liw_webdom_next_sibling(node->document, current);
    }
    if (steps > LIW_WEBDOM_MAX_NODES * 2U) {
        free(text);
        return JS_ThrowRangeError(ctx, "DOM traversal limit exceeded");
    }
    text[length] = '\0';
    JSValue result = JS_NewStringLen(ctx, text, length);
    free(text);
    return result;
}

static JSValue qjs_dom_set_node_text(JSContext *ctx, JSValueConst this_value,
                                     JSValueConst value) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    size_t length = 0;
    const char *text = JS_ToCStringLen(ctx, &length, value);
    if (!text) return JS_EXCEPTION;
    int rc = liw_webdom_set_text(node->document, node->node, text, length);
    JS_FreeCString(ctx, text);
    if (rc == LIW_WEBDOM_NO_MEMORY || rc == LIW_WEBDOM_LIMIT)
        return JS_ThrowOutOfMemory(ctx);
    if (rc != LIW_WEBDOM_OK)
        return JS_ThrowTypeError(ctx, "cannot set text on this node");
    return JS_UNDEFINED;
}

static JSValue qjs_dom_inner_text(JSContext *ctx, JSValueConst this_value) {
    return qjs_dom_node_text(ctx, this_value);
}

static JSValue qjs_dom_set_inner_text(JSContext *ctx, JSValueConst this_value,
                                      JSValueConst value) {
    return qjs_dom_set_node_text(ctx, this_value, value);
}

static JSValue qjs_dom_node_value(JSContext *ctx, JSValueConst this_value) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    liw_web_node_type_t type = liw_webdom_node_type(node->document, node->node);
    if (type != LIW_WEBDOM_TEXT && type != LIW_WEBDOM_COMMENT) return JS_NULL;
    liw_web_slice_t value = liw_webdom_value(node->document, node->node);
    return qjs_dom_decode_slice(ctx, value,
        liw_webdom_text_needs_entity_decode(node->document, node->node));
}

static JSValue qjs_dom_set_node_value(JSContext *ctx, JSValueConst this_value,
                                      JSValueConst value) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    liw_web_node_type_t type = liw_webdom_node_type(node->document, node->node);
    if (type != LIW_WEBDOM_TEXT && type != LIW_WEBDOM_COMMENT)
        return JS_UNDEFINED;
    size_t length = 0;
    const char *text = JS_IsNull(value) ? "" : JS_ToCStringLen(ctx, &length, value);
    if (!text) return JS_EXCEPTION;
    int rc = liw_webdom_set_text(node->document, node->node, text, length);
    if (!JS_IsNull(value)) JS_FreeCString(ctx, text);
    if (rc != LIW_WEBDOM_OK) return JS_ThrowOutOfMemory(ctx);
    return JS_UNDEFINED;
}

static JSValue qjs_dom_inner_html(JSContext *ctx, JSValueConst this_value) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    size_t total = 0, steps = 0;
    for (liw_web_node_id_t child = liw_webdom_first_child(node->document, node->node);
         child != LIW_WEBDOM_NODE_NONE && steps++ < LIW_WEBDOM_MAX_NODES;
         child = liw_webdom_next_sibling(node->document, child)) {
        size_t length = 0;
        int rc = liw_webdom_serialize_node(node->document, child, NULL, 0, &length);
        if (rc != LIW_WEBDOM_OK || length > QJS_MAX_DOM_TEXT_BYTES - total)
            return JS_ThrowRangeError(ctx, "DOM HTML limit exceeded");
        total += length;
    }
    char *html = (char *)malloc(total + 1);
    if (!html) return JS_ThrowOutOfMemory(ctx);
    size_t offset = 0;
    for (liw_web_node_id_t child = liw_webdom_first_child(node->document, node->node);
         child != LIW_WEBDOM_NODE_NONE;
         child = liw_webdom_next_sibling(node->document, child)) {
        size_t length = 0;
        int rc = liw_webdom_serialize_node(node->document, child,
            html + offset, total - offset + 1, &length);
        if (rc != LIW_WEBDOM_OK) { free(html); return JS_ThrowInternalError(ctx, "cannot serialize DOM"); }
        offset += length;
    }
    html[offset] = '\0';
    JSValue result = JS_NewStringLen(ctx, html, offset);
    free(html);
    return result;
}

static JSValue qjs_dom_set_inner_html(JSContext *ctx, JSValueConst this_value,
                                     JSValueConst value) {
    qjs_dom_node_t *node = qjs_get_dom_node(ctx, this_value);
    if (!node) return JS_EXCEPTION;
    size_t length = 0;
    const char *html = JS_ToCStringLen(ctx, &length, value);
    if (!html) return JS_EXCEPTION;
    int rc = liw_webdom_set_inner_html(node->document, node->node, html, length);
    JS_FreeCString(ctx, html);
    if (rc == LIW_WEBDOM_NO_MEMORY || rc == LIW_WEBDOM_LIMIT ||
        rc == LIW_WEBDOM_TOO_LARGE) return JS_ThrowRangeError(ctx, "DOM HTML limit exceeded");
    if (rc != LIW_WEBDOM_OK) return JS_ThrowTypeError(ctx, "cannot set innerHTML on this node");
    return JS_UNDEFINED;
}

static JSValue qjs_dom_node_value_magic(JSContext *ctx,
                                        JSValueConst this_value, int magic) {
    (void)magic;
    return qjs_dom_node_value(ctx, this_value);
}

static JSValue qjs_dom_set_node_value_magic(JSContext *ctx,
                                           JSValueConst this_value,
                                           JSValueConst value, int magic) {
    (void)magic;
    return qjs_dom_set_node_value(ctx, this_value, value);
}

static JSValue qjs_dom_inner_html_magic(JSContext *ctx,
                                        JSValueConst this_value, int magic) {
    (void)magic;
    return qjs_dom_inner_html(ctx, this_value);
}

static JSValue qjs_dom_set_inner_html_magic(JSContext *ctx,
                                            JSValueConst this_value,
                                            JSValueConst value, int magic) {
    (void)magic;
    return qjs_dom_set_inner_html(ctx, this_value, value);
}

typedef JSValue qjs_dom_event_getter_fn(JSContext *ctx, JSValueConst this_value,
                                        int magic);

static int qjs_define_event_getter(JSContext *ctx, JSValueConst object,
                                   const char *name,
                                   qjs_dom_event_getter_fn *getter, int magic) {
    JSCFunctionType getter_type = { .getter_magic = getter };
    JSValue function = JS_NewCFunction2(ctx, getter_type.generic, name, 0,
                                        JS_CFUNC_getter_magic, magic);
    if (JS_IsException(function)) return -1;
    JSAtom atom = JS_NewAtom(ctx, name);
    int rc = JS_DefinePropertyGetSet(ctx, object, atom, function,
                                     JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_FreeAtom(ctx, atom);
    return rc < 0 ? -1 : 0;
}

static int qjs_install_event(JSContext *ctx) {
    JSRuntime *runtime = JS_GetRuntime(ctx);
    JSClassDef event_class = {
        .class_name = "LIWUSEvent",
        .finalizer = qjs_dom_event_finalizer,
    };
    qjs_dom_event_class = JS_NewClassID(&qjs_dom_event_class);
    if (JS_NewClass(runtime, qjs_dom_event_class, &event_class) < 0) return -1;

    JSValue prototype = JS_NewObject(ctx);
    if (JS_IsException(prototype)) return -1;
    if (JS_SetPropertyStr(ctx, prototype, "preventDefault",
            JS_NewCFunction(ctx, qjs_event_prevent_default, "preventDefault", 0)) < 0 ||
        JS_SetPropertyStr(ctx, prototype, "stopPropagation",
            JS_NewCFunction(ctx, qjs_event_stop_propagation, "stopPropagation", 0)) < 0 ||
        JS_SetPropertyStr(ctx, prototype, "stopImmediatePropagation",
            JS_NewCFunction(ctx, qjs_event_stop_immediate, "stopImmediatePropagation", 0)) < 0 ||
        qjs_define_event_getter(ctx, prototype, "type", qjs_event_get_property, 0) < 0 ||
        qjs_define_event_getter(ctx, prototype, "bubbles", qjs_event_get_property, 1) < 0 ||
        qjs_define_event_getter(ctx, prototype, "cancelable", qjs_event_get_property, 2) < 0 ||
        qjs_define_event_getter(ctx, prototype, "defaultPrevented", qjs_event_get_property, 3) < 0 ||
        qjs_define_event_getter(ctx, prototype, "target", qjs_event_get_property, 4) < 0 ||
        qjs_define_event_getter(ctx, prototype, "currentTarget", qjs_event_get_property, 5) < 0 ||
        qjs_define_event_getter(ctx, prototype, "eventPhase", qjs_event_get_property, 6) < 0) {
        JS_FreeValue(ctx, prototype);
        return -1;
    }
    JS_SetClassProto(ctx, qjs_dom_event_class, JS_DupValue(ctx, prototype));

    JSCFunctionType constructor_type = { .constructor = qjs_event_constructor };
    JSValue constructor = JS_NewCFunction2(ctx, constructor_type.generic,
        "Event", 1, JS_CFUNC_constructor, 0);
    if (JS_IsException(constructor) || JS_SetConstructor(ctx, constructor, prototype) < 0) {
        JS_FreeValue(ctx, constructor);
        JS_FreeValue(ctx, prototype);
        return -1;
    }
    if (JS_SetPropertyStr(ctx, constructor, "NONE", JS_NewInt32(ctx, 0)) < 0 ||
        JS_SetPropertyStr(ctx, constructor, "CAPTURING_PHASE", JS_NewInt32(ctx, 1)) < 0 ||
        JS_SetPropertyStr(ctx, constructor, "AT_TARGET", JS_NewInt32(ctx, 2)) < 0 ||
        JS_SetPropertyStr(ctx, constructor, "BUBBLING_PHASE", JS_NewInt32(ctx, 3)) < 0) {
        JS_FreeValue(ctx, constructor);
        JS_FreeValue(ctx, prototype);
        return -1;
    }
    JSValue global = JS_GetGlobalObject(ctx);
    int rc = JS_SetPropertyStr(ctx, global, "Event", constructor);
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, prototype);
    return rc < 0 ? -1 : 0;
}

static int qjs_install_document(JSContext *ctx) {
    JSRuntime *runtime = JS_GetRuntime(ctx);
    JSClassDef node_class = {
        .class_name = "LIWUSDOMNode",
        .finalizer = qjs_dom_node_finalizer,
    };
    qjs_dom_node_class = JS_NewClassID(&qjs_dom_node_class);
    if (JS_NewClass(runtime, qjs_dom_node_class, &node_class) < 0)
        return -1;
    if (qjs_install_event(ctx) < 0) return -1;

    JSValue prototype = JS_NewObject(ctx);
    if (JS_IsException(prototype)) return -1;
#define QJS_DOM_METHOD(obj, name, fn, nargs) \
    do { \
        if (JS_SetPropertyStr(ctx, (obj), (name), \
                JS_NewCFunction(ctx, (fn), (name), (nargs))) < 0) { \
            JS_FreeValue(ctx, (obj)); \
            return -1; \
        } \
    } while (0)
    QJS_DOM_METHOD(prototype, "getAttribute", qjs_dom_get_attribute, 1);
    QJS_DOM_METHOD(prototype, "setAttribute", qjs_dom_set_attribute, 2);
    QJS_DOM_METHOD(prototype, "removeAttribute", qjs_dom_remove_attribute, 1);
    QJS_DOM_METHOD(prototype, "getAttributeNS", qjs_dom_get_attribute_ns, 2);
    QJS_DOM_METHOD(prototype, "setAttributeNS", qjs_dom_set_attribute_ns, 3);
    QJS_DOM_METHOD(prototype, "removeAttributeNS", qjs_dom_remove_attribute_ns, 2);
    QJS_DOM_METHOD(prototype, "appendChild", qjs_dom_append_child, 1);
    QJS_DOM_METHOD(prototype, "removeChild", qjs_dom_remove_child, 1);
    QJS_DOM_METHOD(prototype, "hasChildNodes", qjs_dom_has_child_nodes, 0);
    QJS_DOM_METHOD(prototype, "hasAttributes", qjs_dom_has_attributes, 0);
    QJS_DOM_METHOD(prototype, "getAttributeNames", qjs_dom_get_attribute_names, 0);
    QJS_DOM_METHOD(prototype, "isSameNode", qjs_dom_is_same_node, 1);
    QJS_DOM_METHOD(prototype, "isEqualNode", qjs_dom_is_equal_node, 1);
    QJS_DOM_METHOD(prototype, "compareDocumentPosition",
                   qjs_dom_compare_document_position, 1);
    QJS_DOM_METHOD(prototype, "getRootNode", qjs_dom_get_root_node, 0);
    QJS_DOM_METHOD(prototype, "normalize", qjs_dom_normalize, 0);
    QJS_DOM_METHOD(prototype, "insertBefore", qjs_dom_insert_before, 2);
    QJS_DOM_METHOD(prototype, "replaceChild", qjs_dom_replace_child, 2);
    QJS_DOM_METHOD(prototype, "remove", qjs_dom_remove, 0);
    QJS_DOM_METHOD(prototype, "contains", qjs_dom_contains, 1);
    QJS_DOM_METHOD(prototype, "hasAttribute", qjs_dom_has_attribute, 1);
    QJS_DOM_METHOD(prototype, "hasAttributeNS", qjs_dom_has_attribute_ns, 2);
    QJS_DOM_METHOD(prototype, "toggleAttribute", qjs_dom_toggle_attribute, 2);
    QJS_DOM_METHOD(prototype, "cloneNode", qjs_dom_clone_node, 1);
    QJS_DOM_METHOD(prototype, "addEventListener", qjs_dom_add_event_listener, 2);
    QJS_DOM_METHOD(prototype, "removeEventListener", qjs_dom_remove_event_listener, 2);
    QJS_DOM_METHOD(prototype, "dispatchEvent", qjs_dom_dispatch_event, 1);
    QJS_DOM_METHOD(prototype, "querySelector", qjs_dom_node_query, 1);
    QJS_DOM_METHOD(prototype, "querySelectorAll", qjs_dom_node_query_all, 1);
    QJS_DOM_METHOD(prototype, "getElementsByTagName", qjs_dom_get_elements_by_tag, 1);
    QJS_DOM_METHOD(prototype, "getElementsByClassName", qjs_dom_get_elements_by_class, 1);
    QJS_DOM_METHOD(prototype, "matches", qjs_dom_matches_method, 1);
    QJS_DOM_METHOD(prototype, "closest", qjs_dom_closest, 1);
    if (qjs_define_dom_accessor(ctx, prototype, "nodeType",
            qjs_dom_node_property, NULL, QJS_NODE_TYPE) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "nodeName",
            qjs_dom_node_property, NULL, QJS_NODE_NAME) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "tagName",
            qjs_dom_node_property, NULL, QJS_TAG_NAME) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "parentNode",
            qjs_dom_node_property, NULL, QJS_PARENT_NODE) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "parentElement",
            qjs_dom_node_property, NULL, QJS_PARENT_ELEMENT) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "firstChild",
            qjs_dom_node_property, NULL, QJS_FIRST_CHILD) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "lastChild",
            qjs_dom_node_property, NULL, QJS_LAST_CHILD) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "nextSibling",
            qjs_dom_node_property, NULL, QJS_NEXT_SIBLING) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "previousSibling",
            qjs_dom_node_property, NULL, QJS_PREVIOUS_SIBLING) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "children",
            qjs_dom_node_property, NULL, QJS_CHILDREN) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "childElementCount",
            qjs_dom_node_property, NULL, QJS_CHILD_ELEMENT_COUNT) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "childNodes",
            qjs_dom_node_property, NULL, QJS_CHILD_NODES) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "firstElementChild",
            qjs_dom_node_property, NULL, QJS_FIRST_ELEMENT_CHILD) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "lastElementChild",
            qjs_dom_node_property, NULL, QJS_LAST_ELEMENT_CHILD) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "nextElementSibling",
            qjs_dom_node_property, NULL, QJS_NEXT_ELEMENT_SIBLING) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "previousElementSibling",
            qjs_dom_node_property, NULL, QJS_PREVIOUS_ELEMENT_SIBLING) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "isConnected",
            qjs_dom_node_property, NULL, QJS_IS_CONNECTED) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "ownerDocument",
            qjs_dom_node_property, NULL, QJS_OWNER_DOCUMENT) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "nodeValue",
            qjs_dom_node_value_magic, qjs_dom_set_node_value_magic, 0) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "innerHTML",
            qjs_dom_inner_html_magic, qjs_dom_set_inner_html_magic, 0) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "outerHTML",
            qjs_dom_outer_html_magic, qjs_dom_set_outer_html_magic, 0) < 0) {
        JS_FreeValue(ctx, prototype);
        return -1;
    }
    JSAtom text_atom = JS_NewAtom(ctx, "textContent");
    JSCFunctionType text_getter_type = { .getter = qjs_dom_node_text };
    JSCFunctionType text_setter_type = { .setter = qjs_dom_set_node_text };
    JSValue text_getter = JS_NewCFunction2(ctx, text_getter_type.generic,
                                           "get textContent", 0,
                                           JS_CFUNC_getter, 0);
    JSValue text_setter = JS_NewCFunction2(ctx, text_setter_type.generic,
                                           "set textContent", 1,
                                           JS_CFUNC_setter, 0);
    if (JS_IsException(text_getter) || JS_IsException(text_setter) ||
        JS_DefinePropertyGetSet(ctx, prototype, text_atom,
                                text_getter, text_setter,
                                JS_PROP_CONFIGURABLE) < 0) {
        JS_FreeAtom(ctx, text_atom);
        JS_FreeValue(ctx, prototype);
        return -1;
    }
    JS_FreeAtom(ctx, text_atom);
    if (qjs_define_dom_accessor(ctx, prototype, "id",
                                qjs_dom_get_attribute_magic,
                                qjs_dom_set_attribute_magic, 0) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "className",
                                qjs_dom_get_attribute_magic,
                                qjs_dom_set_attribute_magic, 1) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "value",
                                qjs_dom_get_attribute_magic,
                                qjs_dom_set_attribute_magic, 2) < 0 ||
        qjs_define_dom_accessor(ctx, prototype, "href",
                                qjs_dom_get_attribute_magic,
                                qjs_dom_set_attribute_magic, 3) < 0) {
        JS_FreeValue(ctx, prototype);
        return -1;
    }
    JSCFunctionType class_list_getter_type = { .getter = qjs_dom_class_list };
    JSValue class_list_getter = JS_NewCFunction2(
        ctx, class_list_getter_type.generic, "get classList", 0,
        JS_CFUNC_getter, 0);
    JSAtom class_list_atom = JS_NewAtom(ctx, "classList");
    if (JS_IsException(class_list_getter) ||
        JS_DefinePropertyGetSet(ctx, prototype, class_list_atom,
                                class_list_getter, JS_UNDEFINED,
                                JS_PROP_CONFIGURABLE) < 0) {
        JS_FreeAtom(ctx, class_list_atom);
        JS_FreeValue(ctx, prototype);
        return -1;
    }
    JS_FreeAtom(ctx, class_list_atom);
    JSCFunctionType inner_getter_type = { .getter = qjs_dom_inner_text };
    JSCFunctionType inner_setter_type = { .setter = qjs_dom_set_inner_text };
    JSAtom inner_atom = JS_NewAtom(ctx, "innerText");
    JSValue inner_getter = JS_NewCFunction2(ctx, inner_getter_type.generic,
                                             "get innerText", 0,
                                             JS_CFUNC_getter, 0);
    JSValue inner_setter = JS_NewCFunction2(ctx, inner_setter_type.generic,
                                             "set innerText", 1,
                                             JS_CFUNC_setter, 0);
    if (JS_IsException(inner_getter) || JS_IsException(inner_setter) ||
        JS_DefinePropertyGetSet(ctx, prototype, inner_atom, inner_getter,
                                inner_setter, JS_PROP_CONFIGURABLE) < 0) {
        JS_FreeAtom(ctx, inner_atom);
        JS_FreeValue(ctx, prototype);
        return -1;
    }
    JS_FreeAtom(ctx, inner_atom);
    JS_SetClassProto(ctx, qjs_dom_node_class, prototype);

    qjs_dom_document_node.document = qjs_dom_document;
    qjs_dom_document_node.node = liw_webdom_root(qjs_dom_document);
    qjs_dom_document_node.is_static = 1;
    JSValue document = JS_NewObjectClass(ctx, qjs_dom_node_class);
    if (JS_IsException(document)) return -1;
    JS_SetOpaque(document, &qjs_dom_document_node);
    if (qjs_define_dom_accessor(ctx, document, "documentElement",
            qjs_dom_document_element_magic, NULL, 0) < 0 ||
        qjs_define_dom_accessor(ctx, document, "head",
            qjs_dom_head_magic, NULL, 0) < 0) {
        JS_FreeValue(ctx, document);
        return -1;
    }
    JSAtom body_atom = JS_NewAtom(ctx, "body");
    JSCFunctionType body_getter_type = { .getter = qjs_dom_body };
    JSValue body_getter = JS_NewCFunction2(ctx, body_getter_type.generic,
                                           "get body", 0,
                                           JS_CFUNC_getter, 0);
    if (JS_IsException(body_getter) ||
        JS_DefinePropertyGetSet(ctx, document, body_atom, body_getter,
                                JS_UNDEFINED, JS_PROP_CONFIGURABLE) < 0) {
        JS_FreeAtom(ctx, body_atom);
        JS_FreeValue(ctx, document);
        return -1;
    }
    JS_FreeAtom(ctx, body_atom);
    JSAtom title_atom = JS_NewAtom(ctx, "title");
    JSCFunctionType title_getter_type = { .getter = qjs_dom_title };
    JSCFunctionType title_setter_type = { .setter = qjs_dom_set_title };
    JSValue title_getter = JS_NewCFunction2(ctx, title_getter_type.generic,
                                             "get title", 0,
                                             JS_CFUNC_getter, 0);
    JSValue title_setter = JS_NewCFunction2(ctx, title_setter_type.generic,
                                             "set title", 1,
                                             JS_CFUNC_setter, 0);
    if (JS_IsException(title_getter) || JS_IsException(title_setter) ||
        JS_DefinePropertyGetSet(ctx, document, title_atom, title_getter,
                                title_setter, JS_PROP_CONFIGURABLE) < 0) {
        JS_FreeAtom(ctx, title_atom);
        JS_FreeValue(ctx, document);
        return -1;
    }
    JS_FreeAtom(ctx, title_atom);
    QJS_DOM_METHOD(document, "getElementById", qjs_dom_get_element_by_id, 1);
    QJS_DOM_METHOD(document, "querySelector", qjs_dom_document_query, 1);
    QJS_DOM_METHOD(document, "querySelectorAll", qjs_dom_document_query_all, 1);
    QJS_DOM_METHOD(document, "getElementsByTagName", qjs_dom_get_elements_by_tag, 1);
    QJS_DOM_METHOD(document, "getElementsByClassName", qjs_dom_get_elements_by_class, 1);
    QJS_DOM_METHOD(document, "createElement", qjs_dom_create_element, 1);
    QJS_DOM_METHOD(document, "createTextNode", qjs_dom_create_text_node, 1);
    QJS_DOM_METHOD(document, "createComment", qjs_dom_create_comment, 1);
    QJS_DOM_METHOD(document, "createDocumentFragment",
                   qjs_dom_create_document_fragment, 0);
    QJS_DOM_METHOD(document, "contains", qjs_dom_contains, 1);
    QJS_DOM_METHOD(document, "addEventListener", qjs_dom_add_event_listener, 2);
    QJS_DOM_METHOD(document, "removeEventListener", qjs_dom_remove_event_listener, 2);
    QJS_DOM_METHOD(document, "dispatchEvent", qjs_dom_dispatch_event, 1);
    qjs_dom_document_object = JS_DupValue(ctx, document);
    qjs_dom_document_object_valid = 1;
    JSValue global = JS_GetGlobalObject(ctx);
    int rc = JS_SetPropertyStr(ctx, global, "document", document);
    JS_FreeValue(ctx, global);
    return rc < 0 ? -1 : 0;
#undef QJS_DOM_METHOD
}

static int qjs_interrupt(JSRuntime *runtime, void *opaque) {
    (void)runtime;
    qjs_runtime_state_t *state = (qjs_runtime_state_t *)opaque;
    if (state->interrupt_callbacks_left == 0) return 1;
    state->interrupt_callbacks_left--;
    return 0;
}

static int install_console(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    if (JS_IsException(console)) {
        JS_FreeValue(ctx, global);
        return -1;
    }

    JSValue log = JS_NewCFunction(ctx, qjs_console_log, "log", 1);
    if (JS_IsException(log)) {
        JS_FreeValue(ctx, console);
        JS_FreeValue(ctx, global);
        return -1;
    }
    if (JS_SetPropertyStr(ctx, console, "log", log) < 0) {
        JS_FreeValue(ctx, console);
        JS_FreeValue(ctx, global);
        return -1;
    }
    const char *extra_console[] = {"info", "warn", "error", "debug", "trace", "dir", "table"};
    for (size_t i = 0; i < sizeof(extra_console) / sizeof(extra_console[0]); ++i) {
        JSValue fn = JS_NewCFunction(ctx, qjs_console_log, extra_console[i], 1);
        if (!JS_IsException(fn)) {
            (void)JS_SetPropertyStr(ctx, console, extra_console[i], fn);
        }
    }
    if (JS_SetPropertyStr(ctx, global, "console", console) < 0) {
        JS_FreeValue(ctx, global);
        return -1;
    }
    JSValue queue_microtask = JS_NewCFunction(ctx, qjs_queue_microtask,
                                               "queueMicrotask", 1);
    if (JS_IsException(queue_microtask) ||
        JS_SetPropertyStr(ctx, global, "queueMicrotask", queue_microtask) < 0) {
        JS_FreeValue(ctx, global);
        return -1;
    }
    const struct {
        const char *name;
        JSCFunction *function;
        int argc;
    } timer_functions[] = {
        {"setTimeout", qjs_set_timeout, 2},
        {"setInterval", qjs_set_interval, 2},
        {"clearTimeout", qjs_clear_timer, 1},
        {"clearInterval", qjs_clear_timer, 1},
    };
    for (size_t i = 0; i < sizeof(timer_functions) / sizeof(timer_functions[0]);
         ++i) {
        JSValue function = JS_NewCFunction(ctx, timer_functions[i].function,
                                           timer_functions[i].name,
                                           timer_functions[i].argc);
        if (JS_IsException(function) ||
            JS_SetPropertyStr(ctx, global, timer_functions[i].name,
                              function) < 0) {
            JS_FreeValue(ctx, global);
            return -1;
        }
    }
    if (qjs_install_fetch(ctx) < 0) {
        JS_FreeValue(ctx, global);
        return -1;
    }
    JS_FreeValue(ctx, global);
    return 0;
}

static char *read_bounded_file(const char *path, size_t maximum,
                               size_t *length_out) {
    int fd = liw_open(path, LIW_O_RDONLY);
    if (fd < 0) return NULL;

    struct liw_stat info;
    if (liw_fstat(fd, &info) < 0 || info.st_size > maximum) {
        (void)liw_close(fd);
        return NULL;
    }

    size_t length = (size_t)info.st_size;
    char *source = (char *)malloc(length + 1);
    if (!source) {
        (void)liw_close(fd);
        return NULL;
    }

    size_t offset = 0;
    while (offset < length) {
        long count = liw_read(fd, source + offset, length - offset);
        if (count <= 0) {
            free(source);
            (void)liw_close(fd);
            return NULL;
        }
        offset += (size_t)count;
    }
    (void)liw_close(fd);
    source[length] = '\0';
    *length_out = length;
    return source;
}

static void print_exception(JSContext *ctx) {
    JSValue exception = JS_GetException(ctx);
    const char *message = JS_ToCString(ctx, exception);
    if (message) {
        (void)fprintf(stderr, "qjs: %s\n", message);
        JS_FreeCString(ctx, message);
    } else {
        (void)fputs("qjs: JavaScript exception\n", stderr);
    }
    JS_FreeValue(ctx, exception);
}

static int qjs_slice_equal_ci(liw_web_slice_t value, const char *literal) {
    size_t length = strlen(literal);
    if (value.length != length) return 0;
    for (size_t i = 0; i < length; ++i) {
        char a = value.data[i], b = literal[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static int qjs_script_is_module(liw_web_slice_t type) {
    size_t start = 0, end = type.length;
    while (start < end && qjs_dom_space(type.data[start])) start++;
    while (end > start && qjs_dom_space(type.data[end - 1])) end--;
    return qjs_slice_equal(type.data + start, end - start, "module", 6, 1);
}

static int qjs_script_is_classic(liw_web_slice_t type, int has_type) {
    if (!has_type || type.length == 0) return 1;
    return qjs_slice_equal_ci(type, "text/javascript") ||
           qjs_slice_equal_ci(type, "application/javascript") ||
           qjs_slice_equal_ci(type, "text/ecmascript") ||
           qjs_slice_equal_ci(type, "application/ecmascript");
}

static int qjs_run_inline_script_pass(JSContext *ctx, int modules,
                                     size_t *total_source) {
    const size_t max_total_source = QJS_MAX_SOURCE_BYTES;
    size_t node_count = liw_webdom_node_count(qjs_dom_document);
    for (liw_web_node_id_t id = 0; id < node_count; ++id) {
        if (liw_webdom_node_type(qjs_dom_document, id) != LIW_WEBDOM_ELEMENT)
            continue;
        liw_web_slice_t name = liw_webdom_name(qjs_dom_document, id);
        if (!qjs_slice_equal_ci(name, "script")) continue;

        liw_web_slice_t ignored;
        if (liw_webdom_get_attribute(qjs_dom_document, id, "src", &ignored) ==
            LIW_WEBDOM_OK) continue;
        liw_web_slice_t type;
        int type_rc = liw_webdom_get_attribute(qjs_dom_document, id, "type", &type);
        int is_module = type_rc == LIW_WEBDOM_OK && qjs_script_is_module(type);
        if (is_module != modules ||
            (!is_module && !qjs_script_is_classic(type, type_rc == LIW_WEBDOM_OK)))
            continue;

        liw_web_node_id_t child = liw_webdom_first_child(qjs_dom_document, id);
        while (child != LIW_WEBDOM_NODE_NONE) {
            if (liw_webdom_node_type(qjs_dom_document, child) == LIW_WEBDOM_TEXT) {
                liw_web_slice_t source = liw_webdom_value(qjs_dom_document, child);
                if (*total_source > max_total_source ||
                    source.length > max_total_source - *total_source) {
                    (void)fputs("qjs: inline script limit exceeded\n", stderr);
                    return 1;
                }
                *total_source += source.length;
                qjs_reset_execution_budget();
                char module_filename[QJS_FETCH_URL_BYTES];
                const char *filename = modules ? "<inline-module>" : "<inline-script>";
                if (modules) {
                    liw_web_slice_t module_url;
                    if (liw_webdom_get_attribute(qjs_dom_document, id,
                            "data-liwus-module-url", &module_url) == LIW_WEBDOM_OK) {
                        size_t decoded_length = 0;
                        int decode_rc = liw_webdom_decode_entities(
                            module_url.data, module_url.length,
                            module_filename, sizeof(module_filename),
                            &decoded_length);
                        if (decode_rc != LIW_WEBDOM_OK ||
                            decoded_length >= sizeof(module_filename)) {
                            JS_ThrowRangeError(ctx, "module URL is invalid");
                            return 1;
                        }
                        module_filename[decoded_length] = '\0';
                        filename = module_filename;
                    }
                }
                JSValue result = JS_Eval(ctx, source.data, source.length,
                    filename,
                    modules ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);
                if (JS_IsException(result)) {
                    /* Browsers keep rendering after a script throws and still
                     * run later script blocks. Preserve mutations made before
                     * the exception instead of discarding the whole document. */
                    (void)fprintf(stderr, "qjs: script failed: %s\n", filename);
                    print_exception(ctx);
                }
                JS_FreeValue(ctx, result);
                /* A browser performs a microtask checkpoint after a script
                 * task. Run Promise reactions and queueMicrotask callbacks
                 * before moving to the next script in document order. */
                qjs_drain_microtasks(ctx);
                /* Once an inlined external bundle has executed in the
                 * persistent browser runtime, drop its raw source text from
                 * the DOM tree so serializing output.html back to the kernel
                 * does not carry megabytes of dead JavaScript text. */
                if (qjs_persistent_page && source.length > 4096U) {
                    (void)liw_webdom_set_text(qjs_dom_document, id, "", 0);
                }
                break;
            }
            child = liw_webdom_next_sibling(qjs_dom_document, child);
        }
    }
    return 0;
}

static void qjs_load_page_import_maps(JSContext *ctx) {
    qjs_import_map_count = 0;
    if (!qjs_dom_document) return;
    size_t node_count = liw_webdom_node_count(qjs_dom_document);
    for (liw_web_node_id_t id = 0; id < node_count; ++id) {
        if (liw_webdom_node_type(qjs_dom_document, id) != LIW_WEBDOM_ELEMENT)
            continue;
        liw_web_slice_t name = liw_webdom_name(qjs_dom_document, id);
        if (!qjs_slice_equal(name.data, name.length, "script", 6, 1)) continue;
        liw_web_slice_t type;
        if (liw_webdom_get_attribute(qjs_dom_document, id, "type", &type) !=
                LIW_WEBDOM_OK ||
            !qjs_slice_equal(type.data, type.length, "importmap", 9, 1))
            continue;

        size_t source_length = 0;
        for (liw_web_node_id_t child = liw_webdom_first_child(qjs_dom_document, id);
             child != LIW_WEBDOM_NODE_NONE;
             child = liw_webdom_next_sibling(qjs_dom_document, child)) {
            if (liw_webdom_node_type(qjs_dom_document, child) != LIW_WEBDOM_TEXT)
                continue;
            liw_web_slice_t text = liw_webdom_value(qjs_dom_document, child);
            if (text.length > QJS_IMPORT_MAP_SOURCE_BYTES - source_length) {
                source_length = QJS_IMPORT_MAP_SOURCE_BYTES + 1U;
                break;
            }
            source_length += text.length;
        }
        if (!source_length || source_length > QJS_IMPORT_MAP_SOURCE_BYTES) {
            if (source_length > QJS_IMPORT_MAP_SOURCE_BYTES)
                (void)fputs("qjs: import map source limit exceeded\n", stderr);
            continue;
        }
        char *source = (char *)malloc(source_length + 1U);
        if (!source) continue;
        size_t offset = 0;
        for (liw_web_node_id_t child = liw_webdom_first_child(qjs_dom_document, id);
             child != LIW_WEBDOM_NODE_NONE;
             child = liw_webdom_next_sibling(qjs_dom_document, child)) {
            if (liw_webdom_node_type(qjs_dom_document, child) != LIW_WEBDOM_TEXT)
                continue;
            liw_web_slice_t text = liw_webdom_value(qjs_dom_document, child);
            memcpy(source + offset, text.data, text.length);
            offset += text.length;
        }
        source[offset] = '\0';

        JSValue map = JS_ParseJSON(ctx, source, source_length, "<importmap>");
        free(source);
        if (JS_IsException(map)) {
            print_exception(ctx);
            continue;
        }
        JSValue imports = JS_GetPropertyStr(ctx, map, "imports");
        JS_FreeValue(ctx, map);
        if (JS_IsException(imports)) {
            print_exception(ctx);
            continue;
        }
        if (!JS_IsObject(imports)) {
            JS_FreeValue(ctx, imports);
            continue;
        }

        JSPropertyEnum *properties = NULL;
        uint32_t property_count = 0;
        if (JS_GetOwnPropertyNames(ctx, &properties, &property_count, imports,
                JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
            JS_FreeValue(ctx, imports);
            print_exception(ctx);
            continue;
        }
        for (uint32_t index = 0;
             index < property_count &&
                 qjs_import_map_count < QJS_IMPORT_MAP_MAX_ENTRIES;
             ++index) {
            size_t specifier_length = 0;
            const char *specifier = JS_AtomToCStringLen(ctx,
                &specifier_length, properties[index].atom);
            if (!specifier || !specifier_length ||
                specifier_length >= QJS_IMPORT_MAP_SPECIFIER_BYTES) {
                if (specifier) JS_FreeCString(ctx, specifier);
                continue;
            }
            JSValue address_value = JS_GetProperty(ctx, imports,
                                                  properties[index].atom);
            if (!JS_IsString(address_value)) {
                JS_FreeValue(ctx, address_value);
                JS_FreeCString(ctx, specifier);
                continue;
            }
            size_t address_length = 0;
            const char *address = JS_ToCStringLen(ctx, &address_length,
                                                   address_value);
            char absolute_address[QJS_FETCH_URL_BYTES];
            bool valid = address && address_length &&
                address_length < sizeof(absolute_address) &&
                qjs_resolve_module_url(qjs_location_href, address,
                    absolute_address, sizeof(absolute_address)) == 0;
            if (valid) {
                size_t slot = qjs_import_map_count++;
                memcpy(qjs_import_map_specifiers[slot], specifier,
                       specifier_length);
                qjs_import_map_specifiers[slot][specifier_length] = '\0';
                memcpy(qjs_import_map_addresses[slot], absolute_address,
                       strlen(absolute_address) + 1U);
            }
            if (address) JS_FreeCString(ctx, address);
            JS_FreeValue(ctx, address_value);
            JS_FreeCString(ctx, specifier);
        }
        JS_FreePropertyEnum(ctx, properties, property_count);
        JS_FreeValue(ctx, imports);
        break;
    }
}

static int qjs_run_inline_scripts(JSContext *ctx) {
    size_t total_source = 0;
    /* Classic scripts run in document order; inline modules run after parsing
     * in their own module scopes. Persistent browser pages also resolve static
     * HTTP(S) import graphs through the bounded worker bridge. */
    qjs_load_page_import_maps(ctx);
    if (qjs_run_inline_script_pass(ctx, 0, &total_source) != 0) return 1;
    if (qjs_run_inline_script_pass(ctx, 1, &total_source) != 0) return 1;
    static const char lifecycle[] =
        "if(typeof document!=='undefined'&&document.dispatchEvent)"
        "document.dispatchEvent(new Event('DOMContentLoaded'));"
        "if(typeof dispatchEvent==='function')dispatchEvent(new Event('load'));";
    JSValue lifecycle_result = JS_Eval(ctx, lifecycle, sizeof(lifecycle) - 1,
                                       "<liwus-lifecycle>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(lifecycle_result)) print_exception(ctx);
    JS_FreeValue(ctx, lifecycle_result);
    qjs_drain_microtasks(ctx);
    if (!qjs_persistent_page) qjs_run_bootstrap_timers(ctx);
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    qjs_persistent_page = argc == 6 &&
                          strcmp(argv[1], "--browser-runtime") == 0;
    if (qjs_persistent_page) {
        qjs_fetch_generation = (uint32_t)strtoul(argv[4], NULL, 10);
        qjs_module_wait_ticks = 0;
        qjs_module_count = 0;
        qjs_module_source_bytes = 0;
        qjs_deferred_fetch_head = 0;
        qjs_deferred_fetch_count = 0;
        qjs_fetch_response_length = 0;
    }
    bool render_file = (argc == 4 && strcmp(argv[1], "--render-html") == 0) ||
                       qjs_persistent_page;
    bool dump_html = (argc == 3 && strcmp(argv[1], "--dump-html") == 0) ||
                     render_file;
    bool page_mode = (argc == 3 && strcmp(argv[1], "--page") == 0) ||
                     dump_html;
    qjs_dump_html = dump_html;
    bool with_dom = page_mode ||
                    (argc == 4 && strcmp(argv[1], "--html") == 0);
    if ((!with_dom && argc != 2) || (with_dom && !page_mode && argc != 4)) {
        (void)fputs("usage: qjs SCRIPT.js\n       qjs --html PAGE.html SCRIPT.js\n"
                    "       qjs --page PAGE.html\n"
                    "       qjs --dump-html PAGE.html\n"
                    "       qjs --render-html PAGE.html OUTPUT.html\n", stderr);
        return 2;
    }

    const char *html_path = page_mode ? argv[2] : (with_dom ? argv[2] : NULL);
    const char *script_path = page_mode ? NULL : (with_dom ? argv[3] : argv[1]);
    if (with_dom) {
        size_t html_length = 0;
        char *html = read_bounded_file(html_path, LIW_WEBDOM_MAX_SOURCE,
                                       &html_length);
        if (!html) {
            (void)fprintf(stderr, "qjs: cannot read HTML (maximum size is %u bytes)\n",
                           (unsigned)LIW_WEBDOM_MAX_SOURCE);
            return 1;
        }
        int parse_rc = liw_webdom_parse(html, html_length, &qjs_dom_document);
        free(html);
        if (parse_rc != LIW_WEBDOM_OK) {
            (void)fprintf(stderr, "qjs: HTML parse failed (%d)\n", parse_rc);
            return 1;
        }
    }

    size_t source_length = 0;
    char *source = page_mode ? NULL :
        read_bounded_file(script_path, QJS_MAX_SOURCE_BYTES, &source_length);
    if (!page_mode && !source) {
        (void)fprintf(stderr,
                      "qjs: cannot read script (maximum size is %u bytes)\n",
                      (unsigned)QJS_MAX_SOURCE_BYTES);
        liw_webdom_destroy(qjs_dom_document);
        qjs_dom_document = NULL;
        return 1;
    }

    qjs_runtime_state_t runtime_state = {
        .interrupt_callbacks_left = QJS_INTERRUPT_CALLBACK_LIMIT,
    };
    JSRuntime *runtime = JS_NewRuntime2(&qjs_allocator, NULL);
    if (!runtime) {
        free(source);
        (void)fputs("qjs: cannot create JavaScript runtime\n", stderr);
        liw_webdom_destroy(qjs_dom_document);
        qjs_dom_document = NULL;
        return 1;
    }
    JS_SetMemoryLimit(runtime, QJS_MEMORY_LIMIT_BYTES);
    JS_SetMaxStackSize(runtime, QJS_STACK_LIMIT_BYTES);
    JS_SetInterruptHandler(runtime, qjs_interrupt, &runtime_state);

    JSContext *ctx = JS_NewContext(runtime);
    if (!ctx) {
        JS_FreeRuntime(runtime);
        free(source);
        (void)fputs("qjs: cannot create JavaScript context\n", stderr);
        liw_webdom_destroy(qjs_dom_document);
        qjs_dom_document = NULL;
        return 1;
    }

    qjs_active_runtime_state = &runtime_state;

    if (qjs_persistent_page)
        JS_SetModuleLoaderFunc(runtime, qjs_module_normalize,
                               qjs_load_browser_module, NULL);

    int status = 0;
    if (qjs_persistent_page) {
        qjs_fetch_request_fd = liw_open("/ipc/webjs-fetch-in",
                                        LIW_O_WRONLY);
        qjs_fetch_response_fd = liw_open("/ipc/webjs-fetch-out",
                                         LIW_O_RDONLY);
        if (qjs_fetch_request_fd < 0 || qjs_fetch_response_fd < 0) {
            (void)fputs("qjs: browser network bridge unavailable\n", stderr);
            status = 1;
        }
    }
    if (status != 0) {
        /* Keep normal startup error handling below. */
    } else if (install_console(ctx) < 0) {
        print_exception(ctx);
        status = 1;
    } else if (with_dom && qjs_install_document(ctx) < 0) {
        print_exception(ctx);
        status = 1;
    } else if (qjs_install_web_globals(ctx,
                   qjs_persistent_page ? argv[5] : "about:blank") < 0) {
        print_exception(ctx);
        status = 1;
    } else if (page_mode) {
        status = qjs_run_inline_scripts(ctx);
    } else {
        JSValue result = JS_Eval(ctx, source, source_length, script_path,
                                 JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            print_exception(ctx);
            status = 1;
        }
        JS_FreeValue(ctx, result);
        qjs_drain_microtasks(ctx);
        qjs_run_bootstrap_timers(ctx);
    }

    if (status == 0 && dump_html) {
        size_t html_length = 0;
        int serialize_rc = liw_webdom_serialize(qjs_dom_document, NULL, 0,
                                                 &html_length);
        char *html = serialize_rc == LIW_WEBDOM_OK &&
                     html_length <= LIW_WEBDOM_MAX_SOURCE
            ? (char *)malloc(html_length + 1) : NULL;
        if (!html || liw_webdom_serialize(qjs_dom_document, html,
                html_length + 1, &html_length) != LIW_WEBDOM_OK) {
            (void)fputs("qjs: cannot serialize modified document\n", stderr);
            status = 1;
        } else {
            /* Open only after execution and serialization succeed. The caller
             * must consume the output only after a successful process exit. */
            FILE *output = render_file ? fopen(argv[3], "wb") : stdout;
            if (!output) {
                status = 1;
            } else {
                if (fwrite(html, 1, html_length, output) != html_length ||
                    fflush(output) == EOF) status = 1;
                if (render_file && fclose(output) == EOF) status = 1;
            }
            if (status) (void)fputs("qjs: cannot write modified document\n", stderr);
        }
        free(html);
    }

    if (status == 0 && qjs_persistent_page)
        status = qjs_browser_event_loop(ctx, argv[3]);

    if (qjs_fetch_request_fd >= 0) (void)liw_close(qjs_fetch_request_fd);
    if (qjs_fetch_response_fd >= 0) (void)liw_close(qjs_fetch_response_fd);
    for (size_t i = 0; i < QJS_MAX_PENDING_FETCHES; ++i) {
        if (!qjs_fetches[i].active) continue;
        JS_FreeValue(ctx, qjs_fetches[i].resolve);
        JS_FreeValue(ctx, qjs_fetches[i].reject);
        qjs_fetches[i].active = 0;
    }
    qjs_clear_all_timers(ctx);
    qjs_dom_clear_event_listeners(ctx);
    for (size_t i = 0; i < LIW_WEBDOM_MAX_NODES; ++i) {
        if (qjs_dom_node_cache_valid[i]) {
            JS_FreeValue(ctx, qjs_dom_node_cache[i]);
            qjs_dom_node_cache_valid[i] = 0;
        }
    }
    if (qjs_dom_document_object_valid) {
        JS_FreeValue(ctx, qjs_dom_document_object);
        qjs_dom_document_object_valid = 0;
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(runtime);
    qjs_active_runtime_state = NULL;
    free(source);
    liw_webdom_destroy(qjs_dom_document);
    qjs_dom_document = NULL;
    return status;
}
