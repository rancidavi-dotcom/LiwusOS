#include "liwus/webdom.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"

typedef struct {
    uint32_t parent;
    uint32_t first_child;
    uint32_t last_child;
    uint32_t next_sibling;
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t value_offset;
    uint32_t value_length;
    uint32_t first_attribute;
    uint32_t last_attribute;
    uint32_t attribute_count;
    uint8_t type;
    uint8_t value_has_html_entities;
} webdom_node_t;

typedef struct {
    uint32_t next;
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t value_offset;
    uint32_t value_length;
    uint8_t value_has_html_entities;
} webdom_attribute_t;

struct liw_web_document {
    char *strings;
    size_t strings_length;
    size_t strings_capacity;
    webdom_node_t *nodes;
    size_t node_count;
    size_t node_capacity;
    webdom_attribute_t *attributes;
    size_t attribute_count;
    size_t attribute_capacity;
};

typedef struct {
    const char *html;
    size_t length;
    size_t position;
    liw_web_document_t *document;
    uint32_t stack[LIW_WEBDOM_MAX_DEPTH];
    size_t depth;
} webdom_parser_t;

static int wd_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static char wd_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static int wd_equal_ci(const char *a, size_t alen, const char *b, size_t blen) {
    if (alen != blen) return 0;
    for (size_t i = 0; i < alen; ++i) {
        if (wd_lower(a[i]) != wd_lower(b[i])) return 0;
    }
    return 1;
}

static int wd_equal_cstr_ci(const char *a, size_t alen, const char *b) {
    return b && wd_equal_ci(a, alen, b, strlen(b));
}

static int wd_reserve_strings(liw_web_document_t *doc, size_t extra) {
    /* The DOM owns a copy of the source and stores parsed text/attribute
     * slices in the same arena. In the worst case these consume nearly two
     * source lengths; keep the input cap while budgeting for both copies. */
    const size_t max_arena = 2U * (size_t)LIW_WEBDOM_MAX_SOURCE +
                             LIW_WEBDOM_MAX_NODES +
                             LIW_WEBDOM_MAX_ATTRS + 1;
    if (doc->strings_length > max_arena) return LIW_WEBDOM_LIMIT;
    if (extra > max_arena - doc->strings_length) return LIW_WEBDOM_LIMIT;
    size_t needed = doc->strings_length + extra;
    if (needed <= doc->strings_capacity) return LIW_WEBDOM_OK;
    size_t cap = doc->strings_capacity ? doc->strings_capacity : 256;
    while (cap < needed) {
        size_t next = cap * 2;
        if (next > max_arena) next = max_arena;
        if (next <= cap) return LIW_WEBDOM_LIMIT;
        cap = next;
    }
    char *p = (char *)realloc(doc->strings, cap);
    if (!p) return LIW_WEBDOM_NO_MEMORY;
    doc->strings = p;
    doc->strings_capacity = cap;
    return LIW_WEBDOM_OK;
}

static int wd_store_string(liw_web_document_t *doc, const char *value,
                           size_t length, uint32_t *offset) {
    if ((!value && length) || length > UINT32_MAX) return LIW_WEBDOM_INVALID;
    if (length > SIZE_MAX - 1) return LIW_WEBDOM_LIMIT;
    int rc = wd_reserve_strings(doc, length + 1);
    if (rc != LIW_WEBDOM_OK) return rc;
    if (doc->strings_length > UINT32_MAX ||
        length > UINT32_MAX - doc->strings_length - 1) return LIW_WEBDOM_LIMIT;
    *offset = (uint32_t)doc->strings_length;
    if (length) memcpy(doc->strings + doc->strings_length, value, length);
    doc->strings[doc->strings_length + length] = '\0';
    doc->strings_length += length + 1;
    return LIW_WEBDOM_OK;
}

static int wd_reserve_nodes(liw_web_document_t *doc, size_t count) {
    if (count > LIW_WEBDOM_MAX_NODES) return LIW_WEBDOM_LIMIT;
    if (count <= doc->node_capacity) return LIW_WEBDOM_OK;
    size_t cap = doc->node_capacity ? doc->node_capacity : 32;
    while (cap < count) {
        cap *= 2;
        if (cap > LIW_WEBDOM_MAX_NODES) cap = LIW_WEBDOM_MAX_NODES;
    }
    webdom_node_t *p = (webdom_node_t *)realloc(doc->nodes, cap * sizeof(*p));
    if (!p) return LIW_WEBDOM_NO_MEMORY;
    doc->nodes = p;
    doc->node_capacity = cap;
    return LIW_WEBDOM_OK;
}

static int wd_reserve_attributes(liw_web_document_t *doc, size_t count) {
    if (count > LIW_WEBDOM_MAX_ATTRS) return LIW_WEBDOM_LIMIT;
    if (count <= doc->attribute_capacity) return LIW_WEBDOM_OK;
    size_t cap = doc->attribute_capacity ? doc->attribute_capacity : 32;
    while (cap < count) {
        cap *= 2;
        if (cap > LIW_WEBDOM_MAX_ATTRS) cap = LIW_WEBDOM_MAX_ATTRS;
    }
    webdom_attribute_t *p = (webdom_attribute_t *)realloc(
        doc->attributes, cap * sizeof(*p));
    if (!p) return LIW_WEBDOM_NO_MEMORY;
    doc->attributes = p;
    doc->attribute_capacity = cap;
    return LIW_WEBDOM_OK;
}

static uint32_t wd_new_node(liw_web_document_t *doc, uint8_t type,
                            uint32_t name_offset, uint32_t name_length,
                            uint32_t value_offset, uint32_t value_length) {
    if (wd_reserve_nodes(doc, doc->node_count + 1) != LIW_WEBDOM_OK)
        return LIW_WEBDOM_NODE_NONE;
    uint32_t id = (uint32_t)doc->node_count++;
    webdom_node_t *node = &doc->nodes[id];
    memset(node, 0, sizeof(*node));
    node->parent = LIW_WEBDOM_NODE_NONE;
    node->first_child = LIW_WEBDOM_NODE_NONE;
    node->last_child = LIW_WEBDOM_NODE_NONE;
    node->next_sibling = LIW_WEBDOM_NODE_NONE;
    node->first_attribute = LIW_WEBDOM_NODE_NONE;
    node->last_attribute = LIW_WEBDOM_NODE_NONE;
    node->name_offset = name_offset;
    node->name_length = name_length;
    node->value_offset = value_offset;
    node->value_length = value_length;
    node->type = type;
    return id;
}

static int wd_append_child_raw(liw_web_document_t *doc, uint32_t parent_id,
                               uint32_t child_id) {
    webdom_node_t *parent = &doc->nodes[parent_id];
    webdom_node_t *child = &doc->nodes[child_id];
    if (parent->type == LIW_WEBDOM_TEXT || parent->type == LIW_WEBDOM_COMMENT)
        return LIW_WEBDOM_INVALID;
    child->parent = parent_id;
    child->next_sibling = LIW_WEBDOM_NODE_NONE;
    if (parent->last_child == LIW_WEBDOM_NODE_NONE) {
        parent->first_child = child_id;
    } else {
        doc->nodes[parent->last_child].next_sibling = child_id;
    }
    parent->last_child = child_id;
    return LIW_WEBDOM_OK;
}

static int wd_add_source_node(webdom_parser_t *parser, uint8_t type,
                              size_t name_offset, size_t name_length,
                              size_t value_offset, size_t value_length,
                              uint32_t *out_id) {
    liw_web_document_t *doc = parser->document;
    uint32_t stored_value_offset;
    if (name_offset > UINT32_MAX || name_length > UINT32_MAX ||
        value_offset > parser->length ||
        value_length > parser->length - value_offset ||
        value_length > UINT32_MAX)
        return LIW_WEBDOM_LIMIT;
    int rc = wd_store_string(doc, parser->html + value_offset,
                             value_length, &stored_value_offset);
    if (rc != LIW_WEBDOM_OK) return rc;
    uint32_t id = wd_new_node(doc, type,
                              (uint32_t)name_offset, (uint32_t)name_length,
                              stored_value_offset, (uint32_t)value_length);
    if (id == LIW_WEBDOM_NODE_NONE) {
        return doc->node_count >= LIW_WEBDOM_MAX_NODES
            ? LIW_WEBDOM_LIMIT : LIW_WEBDOM_NO_MEMORY;
    }
    doc->nodes[id].value_has_html_entities = type == LIW_WEBDOM_TEXT;
    if (doc->nodes[id].value_has_html_entities && parser->depth > 0) {
        const webdom_node_t *parent =
            &doc->nodes[parser->stack[parser->depth - 1]];
        const char *parent_name = doc->strings + parent->name_offset;
        if (parent->type == LIW_WEBDOM_ELEMENT &&
            (wd_equal_cstr_ci(parent_name, parent->name_length, "script") ||
             wd_equal_cstr_ci(parent_name, parent->name_length, "style")))
            doc->nodes[id].value_has_html_entities = 0;
    }
    rc = wd_append_child_raw(doc, parser->stack[parser->depth - 1], id);
    if (rc != LIW_WEBDOM_OK) return rc;
    if (out_id) *out_id = id;
    return LIW_WEBDOM_OK;
}

static int wd_add_attribute_source(webdom_parser_t *parser, uint32_t node_id,
                                   size_t name_offset, size_t name_length,
                                   size_t value_offset, size_t value_length) {
    liw_web_document_t *doc = parser->document;
    int rc = wd_reserve_attributes(doc, doc->attribute_count + 1);
    if (rc != LIW_WEBDOM_OK) return rc;
    if (name_offset > UINT32_MAX || name_length > UINT32_MAX ||
        value_offset > UINT32_MAX || value_length > UINT32_MAX)
        return LIW_WEBDOM_LIMIT;
    uint32_t id = (uint32_t)doc->attribute_count++;
    webdom_attribute_t *attr = &doc->attributes[id];
    attr->next = LIW_WEBDOM_NODE_NONE;
    attr->name_offset = (uint32_t)name_offset;
    attr->name_length = (uint32_t)name_length;
    attr->value_offset = (uint32_t)value_offset;
    attr->value_length = (uint32_t)value_length;
    attr->value_has_html_entities = 1;
    webdom_node_t *node = &doc->nodes[node_id];
    if (node->last_attribute == LIW_WEBDOM_NODE_NONE) {
        node->first_attribute = id;
    } else {
        doc->attributes[node->last_attribute].next = id;
    }
    node->last_attribute = id;
    node->attribute_count++;
    return LIW_WEBDOM_OK;
}

static int wd_is_void(const char *name, size_t length) {
    static const char *const names[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (wd_equal_cstr_ci(name, length, names[i])) return 1;
    }
    return 0;
}

static int wd_tag_on_stack(const webdom_parser_t *parser, const char *name,
                           size_t length, const char *candidate) {
    for (size_t i = parser->depth; i > 1; --i) {
        const webdom_node_t *node = &parser->document->nodes[parser->stack[i - 1]];
        if (node->type == LIW_WEBDOM_ELEMENT &&
            wd_equal_cstr_ci(parser->document->strings + node->name_offset,
                             node->name_length, candidate)) return (int)(i - 1);
    }
    (void)name;
    (void)length;
    return -1;
}

static void wd_autoclose(webdom_parser_t *parser, const char *name, size_t length) {
    const char *close_name = NULL;
    if (wd_equal_cstr_ci(name, length, "li")) close_name = "li";
    else if (wd_equal_cstr_ci(name, length, "p")) close_name = "p";
    else if (wd_equal_cstr_ci(name, length, "dt") ||
             wd_equal_cstr_ci(name, length, "dd")) {
        if (wd_tag_on_stack(parser, name, length, "dt") >= 0) close_name = "dt";
        else close_name = "dd";
    } else if (wd_equal_cstr_ci(name, length, "tr")) close_name = "tr";
    else if (wd_equal_cstr_ci(name, length, "td") ||
             wd_equal_cstr_ci(name, length, "th")) {
        if (wd_tag_on_stack(parser, name, length, "td") >= 0) close_name = "td";
        else close_name = "th";
    } else if (wd_equal_cstr_ci(name, length, "option")) close_name = "option";
    if (!close_name) return;

    int found = wd_tag_on_stack(parser, name, length, close_name);
    if (found >= 0) parser->depth = (size_t)found;
}

static int wd_find_tag_end(const char *html, size_t length, size_t start,
                           size_t *out_end) {
    char quote = 0;
    for (size_t i = start; i < length; ++i) {
        char c = html[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '>') {
            *out_end = i;
            return 1;
        }
    }
    return 0;
}

static size_t wd_find_raw_close(const char *html, size_t length, size_t start,
                                const char *tag, size_t tag_length) {
    for (size_t i = start; i + tag_length + 2 < length; ++i) {
        if (html[i] != '<' || html[i + 1] != '/') continue;
        if (!wd_equal_ci(html + i + 2, tag_length, tag, tag_length)) continue;
        char after = html[i + 2 + tag_length];
        if (wd_space(after) || after == '>' || after == '/') return i;
    }
    return length;
}

static int wd_parse_attributes(webdom_parser_t *parser, uint32_t node_id,
                               size_t position, size_t end) {
    const char *html = parser->html;
    while (position < end) {
        while (position < end && (wd_space(html[position]) || html[position] == '/'))
            position++;
        if (position >= end) break;
        size_t name_start = position;
        while (position < end && !wd_space(html[position]) &&
               html[position] != '=' && html[position] != '/' && html[position] != '>')
            position++;
        if (position == name_start) {
            position++;
            continue;
        }
        size_t name_length = position - name_start;
        while (position < end && wd_space(html[position])) position++;
        size_t value_start = position;
        size_t value_length = 0;
        if (position < end && html[position] == '=') {
            position++;
            while (position < end && wd_space(html[position])) position++;
            if (position < end && (html[position] == '\'' || html[position] == '"')) {
                char quote = html[position++];
                value_start = position;
                while (position < end && html[position] != quote) position++;
                value_length = position - value_start;
                if (position < end) position++;
            } else {
                value_start = position;
                while (position < end && !wd_space(html[position]) && html[position] != '>')
                    position++;
                value_length = position - value_start;
            }
        }
        int rc = wd_add_attribute_source(parser, node_id, name_start, name_length,
                                         value_start, value_length);
        if (rc != LIW_WEBDOM_OK) return rc;
    }
    return LIW_WEBDOM_OK;
}

static int wd_parse(webdom_parser_t *parser) {
    const char *html = parser->html;
    size_t length = parser->length;
    liw_web_document_t *doc = parser->document;
    uint32_t root = wd_new_node(doc, LIW_WEBDOM_DOCUMENT, 0, 0, 0, 0);
    if (root == LIW_WEBDOM_NODE_NONE) return LIW_WEBDOM_NO_MEMORY;
    parser->stack[0] = root;
    parser->depth = 1;

    while (parser->position < length) {
        size_t pos = parser->position;
        if (html[pos] != '<') {
            size_t end = pos;
            while (end < length && html[end] != '<') end++;
            int rc = wd_add_source_node(parser, LIW_WEBDOM_TEXT, 0, 0,
                                        pos, end - pos, NULL);
            if (rc != LIW_WEBDOM_OK) return rc;
            parser->position = end;
            continue;
        }

        if (pos + 3 < length && html[pos + 1] == '!' &&
            html[pos + 2] == '-' && html[pos + 3] == '-') {
            size_t end = pos + 4;
            while (end + 2 < length &&
                   !(html[end] == '-' && html[end + 1] == '-' && html[end + 2] == '>'))
                end++;
            size_t value_end = (end + 2 < length) ? end : length;
            int rc = wd_add_source_node(parser, LIW_WEBDOM_COMMENT, 0, 0,
                                        pos + 4, value_end - (pos + 4), NULL);
            if (rc != LIW_WEBDOM_OK) return rc;
            parser->position = (end + 2 < length) ? end + 3 : length;
            continue;
        }

        if (pos + 1 < length && (html[pos + 1] == '!' || html[pos + 1] == '?')) {
            size_t end = pos + 2;
            while (end < length && html[end] != '>') end++;
            parser->position = end < length ? end + 1 : length;
            continue;
        }

        size_t name_start = pos + 1;
        int closing = 0;
        if (name_start < length && html[name_start] == '/') {
            closing = 1;
            name_start++;
        }
        while (name_start < length && wd_space(html[name_start])) name_start++;
        size_t name_end = name_start;
        while (name_end < length && !wd_space(html[name_end]) &&
               html[name_end] != '>' && html[name_end] != '/') name_end++;
        if (name_end == name_start ||
            !((html[name_start] >= 'A' && html[name_start] <= 'Z') ||
              (html[name_start] >= 'a' && html[name_start] <= 'z'))) {
            int rc = wd_add_source_node(parser, LIW_WEBDOM_TEXT, 0, 0,
                                        pos, 1, NULL);
            if (rc != LIW_WEBDOM_OK) return rc;
            parser->position = pos + 1;
            continue;
        }
        size_t tag_end;
        if (!wd_find_tag_end(html, length, name_end, &tag_end)) {
            int rc = wd_add_source_node(parser, LIW_WEBDOM_TEXT, 0, 0,
                                        pos, length - pos, NULL);
            if (rc != LIW_WEBDOM_OK) return rc;
            parser->position = length;
            continue;
        }

        if (closing) {
            for (size_t i = parser->depth; i > 1; --i) {
                uint32_t id = parser->stack[i - 1];
                const webdom_node_t *node = &doc->nodes[id];
                if (node->type == LIW_WEBDOM_ELEMENT &&
                    wd_equal_ci(doc->strings + node->name_offset, node->name_length,
                                html + name_start, name_end - name_start)) {
                    parser->depth = i - 1;
                    break;
                }
            }
            parser->position = tag_end + 1;
            continue;
        }

        size_t slash = tag_end;
        while (slash > name_end && wd_space(html[slash - 1])) slash--;
        int self_closing = slash > name_end && html[slash - 1] == '/';
        size_t name_length = name_end - name_start;
        wd_autoclose(parser, html + name_start, name_length);
        if (!wd_is_void(html + name_start, name_length) && !self_closing &&
            parser->depth >= LIW_WEBDOM_MAX_DEPTH) return LIW_WEBDOM_LIMIT;

        uint32_t id = wd_new_node(doc, LIW_WEBDOM_ELEMENT,
                                  (uint32_t)name_start, (uint32_t)name_length, 0, 0);
        if (id == LIW_WEBDOM_NODE_NONE) {
            return doc->node_count >= LIW_WEBDOM_MAX_NODES
                ? LIW_WEBDOM_LIMIT : LIW_WEBDOM_NO_MEMORY;
        }
        int rc = wd_append_child_raw(doc, parser->stack[parser->depth - 1], id);
        if (rc != LIW_WEBDOM_OK) return rc;
        rc = wd_parse_attributes(parser, id, name_end, tag_end);
        if (rc != LIW_WEBDOM_OK) return rc;
        parser->position = tag_end + 1;

        if (wd_is_void(html + name_start, name_length) || self_closing) continue;
        parser->stack[parser->depth++] = id;

        if (wd_equal_cstr_ci(html + name_start, name_length, "script") ||
            wd_equal_cstr_ci(html + name_start, name_length, "style")) {
            size_t raw_end = wd_find_raw_close(html, length, parser->position,
                                               html + name_start, name_length);
            if (raw_end > parser->position) {
                rc = wd_add_source_node(parser, LIW_WEBDOM_TEXT, 0, 0,
                                        parser->position, raw_end - parser->position, NULL);
                if (rc != LIW_WEBDOM_OK) return rc;
            }
            parser->position = raw_end;
        }
    }
    return LIW_WEBDOM_OK;
}

int liw_webdom_parse(const void *html, size_t length,
                     liw_web_document_t **out_document) {
    if (!out_document || (!html && length)) return LIW_WEBDOM_INVALID;
    *out_document = NULL;
    if (length > LIW_WEBDOM_MAX_SOURCE) return LIW_WEBDOM_TOO_LARGE;

    liw_web_document_t *doc = (liw_web_document_t *)calloc(1, sizeof(*doc));
    if (!doc) return LIW_WEBDOM_NO_MEMORY;
    int rc = wd_reserve_strings(doc, length + 1);
    if (rc != LIW_WEBDOM_OK) {
        liw_webdom_destroy(doc);
        return rc;
    }
    if (length) memcpy(doc->strings, html, length);
    doc->strings[length] = '\0';
    doc->strings_length = length + 1;

    webdom_parser_t parser;
    memset(&parser, 0, sizeof(parser));
    /* Keep the parser's input pointer independent from the mutable string
     * arena. wd_add_source_node() appends text to doc->strings and may grow
     * that allocation with realloc(). If parser.html aliases doc->strings,
     * the first growth invalidates the parser input and later raw-text nodes
     * (notably large inline scripts) are copied from an unmapped pointer. */
    parser.html = (const char *)html;
    parser.length = length;
    parser.document = doc;
    rc = wd_parse(&parser);
    if (rc != LIW_WEBDOM_OK) {
        liw_webdom_destroy(doc);
        return rc;
    }
    *out_document = doc;
    return LIW_WEBDOM_OK;
}

void liw_webdom_destroy(liw_web_document_t *doc) {
    if (!doc) return;
    free(doc->strings);
    free(doc->nodes);
    free(doc->attributes);
    free(doc);
}

static int wd_valid_node(const liw_web_document_t *doc, uint32_t node) {
    return doc && node < doc->node_count;
}

liw_web_node_id_t liw_webdom_root(const liw_web_document_t *doc) {
    return wd_valid_node(doc, 0) ? 0 : LIW_WEBDOM_NODE_NONE;
}

size_t liw_webdom_node_count(const liw_web_document_t *doc) {
    return doc ? doc->node_count : 0;
}

liw_web_node_type_t liw_webdom_node_type(const liw_web_document_t *doc,
                                          liw_web_node_id_t node) {
    return wd_valid_node(doc, node) ? (liw_web_node_type_t)doc->nodes[node].type : 0;
}

liw_web_node_id_t liw_webdom_parent(const liw_web_document_t *doc,
                                    liw_web_node_id_t node) {
    return wd_valid_node(doc, node) ? doc->nodes[node].parent : LIW_WEBDOM_NODE_NONE;
}

liw_web_node_id_t liw_webdom_first_child(const liw_web_document_t *doc,
                                        liw_web_node_id_t node) {
    return wd_valid_node(doc, node) ? doc->nodes[node].first_child : LIW_WEBDOM_NODE_NONE;
}

liw_web_node_id_t liw_webdom_last_child(const liw_web_document_t *doc,
                                       liw_web_node_id_t node) {
    return wd_valid_node(doc, node) ? doc->nodes[node].last_child : LIW_WEBDOM_NODE_NONE;
}

liw_web_node_id_t liw_webdom_previous_sibling(const liw_web_document_t *doc,
                                             liw_web_node_id_t node) {
    if (!wd_valid_node(doc, node)) return LIW_WEBDOM_NODE_NONE;
    uint32_t parent = doc->nodes[node].parent;
    if (!wd_valid_node(doc, parent)) return LIW_WEBDOM_NODE_NONE;
    uint32_t current = doc->nodes[parent].first_child;
    uint32_t previous = LIW_WEBDOM_NODE_NONE;
    size_t steps = 0;
    while (current != LIW_WEBDOM_NODE_NONE && current != node &&
           steps++ < LIW_WEBDOM_MAX_NODES) {
        previous = current;
        current = doc->nodes[current].next_sibling;
    }
    return current == node ? previous : LIW_WEBDOM_NODE_NONE;
}

liw_web_node_id_t liw_webdom_next_sibling(const liw_web_document_t *doc,
                                         liw_web_node_id_t node) {
    return wd_valid_node(doc, node) ? doc->nodes[node].next_sibling : LIW_WEBDOM_NODE_NONE;
}

int liw_webdom_is_connected(const liw_web_document_t *doc,
                            liw_web_node_id_t node) {
    if (!wd_valid_node(doc, node)) return 0;
    size_t steps = 0;
    while (node != LIW_WEBDOM_NODE_NONE && steps++ <= LIW_WEBDOM_MAX_DEPTH) {
        if (node == 0) return 1;
        node = doc->nodes[node].parent;
    }
    return 0;
}

static liw_web_slice_t wd_slice(const liw_web_document_t *doc,
                                uint32_t offset, uint32_t length) {
    liw_web_slice_t slice = { NULL, 0 };
    if (doc && offset <= doc->strings_length &&
        length <= doc->strings_length - offset) {
        slice.data = doc->strings + offset;
        slice.length = length;
    }
    return slice;
}

liw_web_slice_t liw_webdom_name(const liw_web_document_t *doc,
                                liw_web_node_id_t node) {
    if (!wd_valid_node(doc, node)) return wd_slice(NULL, 0, 0);
    return wd_slice(doc, doc->nodes[node].name_offset, doc->nodes[node].name_length);
}

liw_web_slice_t liw_webdom_value(const liw_web_document_t *doc,
                                 liw_web_node_id_t node) {
    if (!wd_valid_node(doc, node)) return wd_slice(NULL, 0, 0);
    return wd_slice(doc, doc->nodes[node].value_offset, doc->nodes[node].value_length);
}

int liw_webdom_text_needs_entity_decode(const liw_web_document_t *doc,
                                        liw_web_node_id_t node) {
    return wd_valid_node(doc, node) && doc->nodes[node].type == LIW_WEBDOM_TEXT
        ? doc->nodes[node].value_has_html_entities : 0;
}

size_t liw_webdom_attribute_count(const liw_web_document_t *doc,
                                  liw_web_node_id_t node) {
    return wd_valid_node(doc, node) ? doc->nodes[node].attribute_count : 0;
}

int liw_webdom_attribute_at(const liw_web_document_t *doc,
                            liw_web_node_id_t node, size_t index,
                            liw_web_slice_t *name, liw_web_slice_t *value) {
    if (!wd_valid_node(doc, node) || !name || !value) return LIW_WEBDOM_INVALID;
    uint32_t attr_id = doc->nodes[node].first_attribute;
    while (attr_id != LIW_WEBDOM_NODE_NONE && index) {
        attr_id = doc->attributes[attr_id].next;
        index--;
    }
    if (attr_id == LIW_WEBDOM_NODE_NONE) return LIW_WEBDOM_NOT_FOUND;
    const webdom_attribute_t *attr = &doc->attributes[attr_id];
    *name = wd_slice(doc, attr->name_offset, attr->name_length);
    *value = wd_slice(doc, attr->value_offset, attr->value_length);
    return name->data && value->data ? LIW_WEBDOM_OK : LIW_WEBDOM_INVALID;
}

int liw_webdom_get_attribute(const liw_web_document_t *doc,
                             liw_web_node_id_t node, const char *name,
                             liw_web_slice_t *value) {
    if (!wd_valid_node(doc, node) || !name || !value) return LIW_WEBDOM_INVALID;
    uint32_t attr_id = doc->nodes[node].first_attribute;
    while (attr_id != LIW_WEBDOM_NODE_NONE) {
        const webdom_attribute_t *attr = &doc->attributes[attr_id];
        if (wd_equal_cstr_ci(doc->strings + attr->name_offset,
                             attr->name_length, name)) {
            *value = wd_slice(doc, attr->value_offset, attr->value_length);
            return value->data ? LIW_WEBDOM_OK : LIW_WEBDOM_INVALID;
        }
        attr_id = attr->next;
    }
    return LIW_WEBDOM_NOT_FOUND;
}

int liw_webdom_attribute_needs_entity_decode(
    const liw_web_document_t *doc, liw_web_node_id_t node,
    const char *name) {
    if (!wd_valid_node(doc, node) || !name) return 0;
    for (uint32_t id = doc->nodes[node].first_attribute;
         id != LIW_WEBDOM_NODE_NONE; id = doc->attributes[id].next) {
        const webdom_attribute_t *attr = &doc->attributes[id];
        if (wd_equal_cstr_ci(doc->strings + attr->name_offset,
                             attr->name_length, name))
            return attr->value_has_html_entities;
    }
    return 0;
}

typedef struct {
    const char *name;
    uint32_t first_codepoint;
    uint32_t second_codepoint;
} webdom_entity_t;

static const webdom_entity_t wd_entities[] = {
#include "webdom_entities.inc"
};

static size_t wd_encode_utf8(uint32_t codepoint, char *out) {
    if (codepoint <= 0x7f) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7ff) {
        out[0] = (char)(0xc0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3f));
        return 2;
    }
    if (codepoint <= 0xffff) {
        out[0] = (char)(0xe0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        out[2] = (char)(0x80 | (codepoint & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    out[3] = (char)(0x80 | (codepoint & 0x3f));
    return 4;
}

static int wd_compare_entity_name(const char *entry, const char *name,
                                  size_t length) {
    size_t i = 0;
    while (entry[i] && i < length && entry[i] == name[i]) i++;
    if (i == length && entry[i] == '\0') return 0;
    if (entry[i] == '\0') return -1;
    if (i == length) return 1;
    return (unsigned char)entry[i] < (unsigned char)name[i] ? -1 : 1;
}

static const webdom_entity_t *wd_find_named_entity(const char *name,
                                                    size_t length) {
    size_t low = 0;
    size_t high = sizeof(wd_entities) / sizeof(wd_entities[0]);
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int cmp = wd_compare_entity_name(wd_entities[middle].name, name, length);
        if (cmp < 0) low = middle + 1;
        else if (cmp > 0) high = middle;
        else return &wd_entities[middle];
    }
    return NULL;
}

static int wd_numeric_entity(const char *name, size_t length, uint32_t *out) {
    if (!length || name[0] != '#') return 0;
    size_t pos = 1;
    unsigned base = 10;
    if (pos < length && (name[pos] == 'x' || name[pos] == 'X')) {
        base = 16;
        pos++;
    }
    if (pos == length) return 0;
    uint32_t value = 0;
    for (; pos < length; ++pos) {
        char c = name[pos];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else return 0;
        if (value > (0x10ffffU - digit) / base) return 0;
        value = value * base + digit;
    }
    if (value == 0 || value > 0x10ffffU ||
        (value >= 0xd800U && value <= 0xdfffU)) value = 0xfffdU;
    *out = value;
    return 1;
}

static size_t wd_entity_reference(const char *input, size_t length, size_t pos,
                                  uint32_t *first, uint32_t *second) {
    if (pos >= length || input[pos] != '&') return 0;
    size_t end = pos + 1;
    while (end < length && end - pos <= 64 && input[end] != ';' &&
           input[end] != '&' && !wd_space(input[end])) end++;
    if (end >= length || input[end] != ';' || end == pos + 1) return 0;

    size_t name_length = end - pos - 1;
    const char *name = input + pos + 1;
    *second = 0;
    if (name[0] == '#') {
        if (!wd_numeric_entity(name, name_length, first)) return 0;
    } else {
        const webdom_entity_t *entity = wd_find_named_entity(name, name_length);
        if (!entity) return 0;
        *first = entity->first_codepoint;
        *second = entity->second_codepoint;
    }
    return end - pos + 1;
}

static size_t wd_utf8_length(uint32_t codepoint) {
    if (codepoint <= 0x7f) return 1;
    if (codepoint <= 0x7ff) return 2;
    if (codepoint <= 0xffff) return 3;
    return 4;
}

int liw_webdom_decode_entities(const char *input, size_t length,
                              char *output, size_t capacity,
                              size_t *output_length) {
    if ((!input && length) || !output_length || (!output && capacity))
        return LIW_WEBDOM_INVALID;
    *output_length = 0;
    size_t needed = 0;
    for (size_t in = 0; in < length;) {
        uint32_t first, second;
        size_t consumed = wd_entity_reference(input, length, in, &first, &second);
        if (consumed) {
            size_t bytes = wd_utf8_length(first);
            if (second) bytes += wd_utf8_length(second);
            if (bytes > SIZE_MAX - needed) return LIW_WEBDOM_TOO_LARGE;
            needed += bytes;
            in += consumed;
        } else {
            if (needed == SIZE_MAX) return LIW_WEBDOM_TOO_LARGE;
            needed++;
            in++;
        }
    }
    *output_length = needed;
    if (!output && capacity == 0) return LIW_WEBDOM_OK;
    if (!output || capacity <= needed) return LIW_WEBDOM_TOO_LARGE;

    size_t out = 0;
    for (size_t in = 0; in < length;) {
        uint32_t first, second;
        size_t consumed = wd_entity_reference(input, length, in, &first, &second);
        if (consumed) {
            out += wd_encode_utf8(first, output + out);
            if (second) out += wd_encode_utf8(second, output + out);
            in += consumed;
        } else {
            output[out++] = input[in++];
        }
    }
    output[out] = '\0';
    return LIW_WEBDOM_OK;
}

liw_web_node_id_t liw_webdom_get_element_by_id(const liw_web_document_t *doc,
                                                const char *id) {
    if (!doc || !id) return LIW_WEBDOM_NODE_NONE;
    size_t id_len = strlen(id);
    for (uint32_t i = 1; i < doc->node_count; ++i) {
        const webdom_node_t *node = &doc->nodes[i];
        if (node->type != LIW_WEBDOM_ELEMENT) continue;
        uint32_t attr_id = node->first_attribute;
        while (attr_id != LIW_WEBDOM_NODE_NONE) {
            const webdom_attribute_t *attr = &doc->attributes[attr_id];
            if (wd_equal_cstr_ci(doc->strings + attr->name_offset,
                                 attr->name_length, "id") &&
                attr->value_length == id_len &&
                memcmp(doc->strings + attr->value_offset, id, id_len) == 0)
                return i;
            attr_id = attr->next;
        }
    }
    return LIW_WEBDOM_NODE_NONE;
}

liw_web_node_id_t liw_webdom_create_element(liw_web_document_t *doc,
                                            const char *name, size_t length) {
    if (!doc || !name || !length || length > UINT32_MAX) return LIW_WEBDOM_NODE_NONE;
    uint32_t name_offset;
    if (wd_store_string(doc, name, length, &name_offset) != LIW_WEBDOM_OK)
        return LIW_WEBDOM_NODE_NONE;
    return wd_new_node(doc, LIW_WEBDOM_ELEMENT, name_offset, (uint32_t)length, 0, 0);
}

liw_web_node_id_t liw_webdom_create_text(liw_web_document_t *doc,
                                        const char *text, size_t length) {
    if (!doc || (!text && length) || length > UINT32_MAX) return LIW_WEBDOM_NODE_NONE;
    uint32_t value_offset;
    if (wd_store_string(doc, text ? text : "", length, &value_offset) != LIW_WEBDOM_OK)
        return LIW_WEBDOM_NODE_NONE;
    return wd_new_node(doc, LIW_WEBDOM_TEXT, 0, 0, value_offset, (uint32_t)length);
}

liw_web_node_id_t liw_webdom_create_comment(liw_web_document_t *doc,
                                           const char *text, size_t length) {
    if (!doc || (!text && length) || length > UINT32_MAX) return LIW_WEBDOM_NODE_NONE;
    uint32_t value_offset;
    if (wd_store_string(doc, text ? text : "", length, &value_offset) != LIW_WEBDOM_OK)
        return LIW_WEBDOM_NODE_NONE;
    return wd_new_node(doc, LIW_WEBDOM_COMMENT, 0, 0, value_offset,
                       (uint32_t)length);
}

liw_web_node_id_t liw_webdom_create_fragment(liw_web_document_t *doc) {
    if (!doc) return LIW_WEBDOM_NODE_NONE;
    return wd_new_node(doc, LIW_WEBDOM_FRAGMENT, 0, 0, 0, 0);
}

int liw_webdom_append_child(liw_web_document_t *doc,
                            liw_web_node_id_t parent_id, liw_web_node_id_t child_id) {
    return liw_webdom_insert_before(doc, parent_id, child_id,
                                    LIW_WEBDOM_NODE_NONE);
}

static int wd_subtree_depth(const liw_web_document_t *doc, uint32_t root,
                            size_t *max_depth) {
    uint32_t node = root;
    size_t depth = 0, steps = 0;
    *max_depth = 0;
    for (;;) {
        if (++steps > LIW_WEBDOM_MAX_NODES) return LIW_WEBDOM_LIMIT;
        if (depth > *max_depth) *max_depth = depth;
        uint32_t child = doc->nodes[node].first_child;
        if (child != LIW_WEBDOM_NODE_NONE) {
            if (++depth > LIW_WEBDOM_MAX_DEPTH) return LIW_WEBDOM_LIMIT;
            node = child;
            continue;
        }
        if (node == root) break;
        uint32_t sibling = doc->nodes[node].next_sibling;
        if (sibling != LIW_WEBDOM_NODE_NONE) node = sibling;
        else {
            uint32_t parent = doc->nodes[node].parent;
            if (depth) depth--;
            if (parent == LIW_WEBDOM_NODE_NONE) return LIW_WEBDOM_INVALID;
            if (parent == root) break;
            node = parent;
        }
    }
    return LIW_WEBDOM_OK;
}

int liw_webdom_insert_before(liw_web_document_t *doc,
                             liw_web_node_id_t parent_id,
                             liw_web_node_id_t child_id,
                             liw_web_node_id_t reference_id) {
    if (!wd_valid_node(doc, parent_id) || !wd_valid_node(doc, child_id) ||
        child_id == 0 || parent_id == child_id) return LIW_WEBDOM_INVALID;
    if (reference_id != LIW_WEBDOM_NODE_NONE &&
        (!wd_valid_node(doc, reference_id) ||
         doc->nodes[reference_id].parent != parent_id)) return LIW_WEBDOM_NOT_FOUND;
    if (reference_id == child_id) return LIW_WEBDOM_OK;
    size_t ancestor_steps = 0;
    for (uint32_t p = parent_id; p != LIW_WEBDOM_NODE_NONE &&
         ancestor_steps++ <= LIW_WEBDOM_MAX_DEPTH; p = doc->nodes[p].parent) {
        if (p == child_id) return LIW_WEBDOM_INVALID;
    }
    if (ancestor_steps > LIW_WEBDOM_MAX_DEPTH)
        return LIW_WEBDOM_LIMIT;
    if (doc->nodes[parent_id].type == LIW_WEBDOM_TEXT ||
        doc->nodes[parent_id].type == LIW_WEBDOM_COMMENT) return LIW_WEBDOM_INVALID;
    size_t parent_depth = 0;
    for (uint32_t p = parent_id; p != LIW_WEBDOM_NODE_NONE;
         p = doc->nodes[p].parent) {
        if (++parent_depth > LIW_WEBDOM_MAX_DEPTH) return LIW_WEBDOM_LIMIT;
        if (p == 0) break;
    }
    size_t subtree_depth = 0;
    int depth_rc = wd_subtree_depth(doc, child_id, &subtree_depth);
    if (depth_rc != LIW_WEBDOM_OK ||
        parent_depth + 1 + subtree_depth > LIW_WEBDOM_MAX_DEPTH)
        return LIW_WEBDOM_LIMIT;
    uint32_t old_parent = doc->nodes[child_id].parent;
    if (old_parent != LIW_WEBDOM_NODE_NONE) {
        int rc = liw_webdom_remove_child(doc, old_parent, child_id);
        if (rc != LIW_WEBDOM_OK) return rc;
    }
    if (reference_id == LIW_WEBDOM_NODE_NONE)
        return wd_append_child_raw(doc, parent_id, child_id);

    webdom_node_t *parent = &doc->nodes[parent_id];
    uint32_t previous = LIW_WEBDOM_NODE_NONE;
    uint32_t current = parent->first_child;
    size_t steps = 0;
    while (current != LIW_WEBDOM_NODE_NONE && current != reference_id &&
           steps++ < LIW_WEBDOM_MAX_NODES) {
        previous = current;
        current = doc->nodes[current].next_sibling;
    }
    if (current != reference_id) return LIW_WEBDOM_NOT_FOUND;
    webdom_node_t *child = &doc->nodes[child_id];
    child->parent = parent_id;
    child->next_sibling = reference_id;
    if (previous == LIW_WEBDOM_NODE_NONE) parent->first_child = child_id;
    else doc->nodes[previous].next_sibling = child_id;
    return LIW_WEBDOM_OK;
}

int liw_webdom_remove_child(liw_web_document_t *doc,
                            liw_web_node_id_t parent_id, liw_web_node_id_t child_id) {
    if (!wd_valid_node(doc, parent_id) || !wd_valid_node(doc, child_id) || child_id == 0)
        return LIW_WEBDOM_INVALID;
    webdom_node_t *parent = &doc->nodes[parent_id];
    uint32_t previous = LIW_WEBDOM_NODE_NONE;
    uint32_t current = parent->first_child;
    while (current != LIW_WEBDOM_NODE_NONE && current != child_id) {
        previous = current;
        current = doc->nodes[current].next_sibling;
    }
    if (current == LIW_WEBDOM_NODE_NONE) return LIW_WEBDOM_NOT_FOUND;
    uint32_t next = doc->nodes[current].next_sibling;
    if (previous == LIW_WEBDOM_NODE_NONE) parent->first_child = next;
    else doc->nodes[previous].next_sibling = next;
    if (parent->last_child == current) parent->last_child = previous;
    doc->nodes[current].parent = LIW_WEBDOM_NODE_NONE;
    doc->nodes[current].next_sibling = LIW_WEBDOM_NODE_NONE;
    return LIW_WEBDOM_OK;
}

static void wd_clear_children(liw_web_document_t *doc, webdom_node_t *node) {
    uint32_t child = node->first_child;
    while (child != LIW_WEBDOM_NODE_NONE) {
        uint32_t next = doc->nodes[child].next_sibling;
        doc->nodes[child].parent = LIW_WEBDOM_NODE_NONE;
        doc->nodes[child].next_sibling = LIW_WEBDOM_NODE_NONE;
        child = next;
    }
    node->first_child = LIW_WEBDOM_NODE_NONE;
    node->last_child = LIW_WEBDOM_NODE_NONE;
}

int liw_webdom_set_text(liw_web_document_t *doc, liw_web_node_id_t node_id,
                        const char *text, size_t length) {
    if (!wd_valid_node(doc, node_id) || (!text && length)) return LIW_WEBDOM_INVALID;
    webdom_node_t *node = &doc->nodes[node_id];
    if (node->type == LIW_WEBDOM_TEXT || node->type == LIW_WEBDOM_COMMENT) {
        uint32_t offset;
        int rc = wd_store_string(doc, text ? text : "", length, &offset);
        if (rc != LIW_WEBDOM_OK) return rc;
        node = &doc->nodes[node_id];
        node->value_offset = offset;
        node->value_length = (uint32_t)length;
        node->value_has_html_entities = 0;
        return LIW_WEBDOM_OK;
    }
    uint32_t text_id = LIW_WEBDOM_NODE_NONE;
    if (length) {
        text_id = liw_webdom_create_text(doc, text, length);
        if (text_id == LIW_WEBDOM_NODE_NONE)
            return doc->node_count >= LIW_WEBDOM_MAX_NODES
                ? LIW_WEBDOM_LIMIT : LIW_WEBDOM_NO_MEMORY;
    }
    node = &doc->nodes[node_id];
    wd_clear_children(doc, node);
    if (text_id != LIW_WEBDOM_NODE_NONE) return wd_append_child_raw(doc, node_id, text_id);
    return LIW_WEBDOM_OK;
}

int liw_webdom_set_attribute(liw_web_document_t *doc,
                             liw_web_node_id_t node_id, const char *name,
                             size_t name_length, const char *value,
                             size_t value_length) {
    if (!wd_valid_node(doc, node_id) || doc->nodes[node_id].type != LIW_WEBDOM_ELEMENT ||
        !name || !name_length || (!value && value_length)) return LIW_WEBDOM_INVALID;
    uint32_t found = LIW_WEBDOM_NODE_NONE;
    for (uint32_t attr = doc->nodes[node_id].first_attribute;
         attr != LIW_WEBDOM_NODE_NONE; attr = doc->attributes[attr].next) {
        if (wd_equal_ci(doc->strings + doc->attributes[attr].name_offset,
                        doc->attributes[attr].name_length, name, name_length)) {
            found = attr;
            break;
        }
    }
    uint32_t value_offset;
    int rc = wd_store_string(doc, value ? value : "", value_length, &value_offset);
    if (rc != LIW_WEBDOM_OK) return rc;
    if (found != LIW_WEBDOM_NODE_NONE) {
        doc->attributes[found].value_offset = value_offset;
        doc->attributes[found].value_length = (uint32_t)value_length;
        doc->attributes[found].value_has_html_entities = 0;
        return LIW_WEBDOM_OK;
    }
    uint32_t name_offset;
    rc = wd_store_string(doc, name, name_length, &name_offset);
    if (rc != LIW_WEBDOM_OK) return rc;
    rc = wd_reserve_attributes(doc, doc->attribute_count + 1);
    if (rc != LIW_WEBDOM_OK) return rc;
    uint32_t attr_id = (uint32_t)doc->attribute_count++;
    webdom_attribute_t *attr = &doc->attributes[attr_id];
    attr->next = LIW_WEBDOM_NODE_NONE;
    attr->name_offset = name_offset;
    attr->name_length = (uint32_t)name_length;
    attr->value_offset = value_offset;
    attr->value_length = (uint32_t)value_length;
    attr->value_has_html_entities = 0;
    webdom_node_t *node = &doc->nodes[node_id];
    if (node->last_attribute == LIW_WEBDOM_NODE_NONE) node->first_attribute = attr_id;
    else doc->attributes[node->last_attribute].next = attr_id;
    node->last_attribute = attr_id;
    node->attribute_count++;
    return LIW_WEBDOM_OK;
}

int liw_webdom_remove_attribute(liw_web_document_t *doc,
                                liw_web_node_id_t node_id, const char *name) {
    if (!wd_valid_node(doc, node_id) || !name) return LIW_WEBDOM_INVALID;
    webdom_node_t *node = &doc->nodes[node_id];
    uint32_t previous = LIW_WEBDOM_NODE_NONE;
    uint32_t current = node->first_attribute;
    while (current != LIW_WEBDOM_NODE_NONE) {
        webdom_attribute_t *attr = &doc->attributes[current];
        if (wd_equal_cstr_ci(doc->strings + attr->name_offset, attr->name_length, name)) {
            if (previous == LIW_WEBDOM_NODE_NONE) node->first_attribute = attr->next;
            else doc->attributes[previous].next = attr->next;
            if (node->last_attribute == current) node->last_attribute = previous;
            node->attribute_count--;
            attr->next = LIW_WEBDOM_NODE_NONE;
            return LIW_WEBDOM_OK;
        }
        previous = current;
        current = attr->next;
    }
    return LIW_WEBDOM_NOT_FOUND;
}

typedef struct {
    char *output;
    size_t capacity;
    size_t length;
} webdom_writer_t;

static int wd_write(webdom_writer_t *writer, const char *bytes, size_t length) {
    if (length > SIZE_MAX - writer->length) return LIW_WEBDOM_LIMIT;
    if (writer->output) {
        if (writer->length > writer->capacity ||
            length > writer->capacity - writer->length)
            return LIW_WEBDOM_TOO_LARGE;
        if (length) memcpy(writer->output + writer->length, bytes, length);
    }
    writer->length += length;
    return LIW_WEBDOM_OK;
}

static int wd_write_escaped(webdom_writer_t *writer, const char *text,
                            size_t length, int attribute,
                            int preserve_source_entities) {
    for (size_t i = 0; i < length; ++i) {
        if (preserve_source_entities && text[i] == '&') {
            size_t end = i + 1;
            while (end < length && end - i <= 64 && text[end] != ';' &&
                   ((text[end] >= 'a' && text[end] <= 'z') ||
                    (text[end] >= 'A' && text[end] <= 'Z') ||
                    (text[end] >= '0' && text[end] <= '9') ||
                    text[end] == '#')) end++;
            if (end < length && text[end] == ';' && end > i + 1) {
                int rc = wd_write(writer, text + i, end - i + 1);
                if (rc != LIW_WEBDOM_OK) return rc;
                i = end;
                continue;
            }
        }
        const char *replacement = NULL;
        size_t replacement_length = 0;
        switch (text[i]) {
            case '&': replacement = "&amp;"; replacement_length = 5; break;
            case '<': replacement = "&lt;"; replacement_length = 4; break;
            case '>': replacement = "&gt;"; replacement_length = 4; break;
            case '"':
                if (attribute) { replacement = "&quot;"; replacement_length = 6; }
                break;
            default: break;
        }
        int rc = wd_write(writer, replacement ? replacement : text + i,
                          replacement ? replacement_length : 1);
        if (rc != LIW_WEBDOM_OK) return rc;
    }
    return LIW_WEBDOM_OK;
}

static int wd_valid_serialized_name(const char *name, size_t length) {
    if (!length) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c <= 0x20 || c == 0x7f || c == '<' || c == '>' || c == '"' ||
            c == '\'' || c == '/' || c == '=') return 0;
    }
    return 1;
}

static int wd_serialize_node(const liw_web_document_t *doc, uint32_t id,
                             unsigned depth, webdom_writer_t *writer) {
    if (depth > LIW_WEBDOM_MAX_DEPTH || !wd_valid_node(doc, id))
        return LIW_WEBDOM_INVALID;
    const webdom_node_t *node = &doc->nodes[id];
    if (node->type == LIW_WEBDOM_COMMENT) {
        int rc = wd_write(writer, "<!--", 4);
        if (rc == LIW_WEBDOM_OK)
            rc = wd_write(writer, doc->strings + node->value_offset,
                          node->value_length);
        if (rc == LIW_WEBDOM_OK) rc = wd_write(writer, "-->", 3);
        return rc;
    }
    if (node->type == LIW_WEBDOM_TEXT)
        return wd_write_escaped(writer, doc->strings + node->value_offset,
                                node->value_length, 0,
                                node->value_has_html_entities);

    int is_document = node->type == LIW_WEBDOM_DOCUMENT ||
                      node->type == LIW_WEBDOM_FRAGMENT;
    const char *name = doc->strings + node->name_offset;
    if (!is_document) {
        if (node->type != LIW_WEBDOM_ELEMENT ||
            !wd_valid_serialized_name(name, node->name_length))
            return LIW_WEBDOM_INVALID;
        int rc = wd_write(writer, "<", 1);
        if (rc == LIW_WEBDOM_OK) rc = wd_write(writer, name, node->name_length);
        if (rc != LIW_WEBDOM_OK) return rc;
        for (uint32_t attr_id = node->first_attribute;
             attr_id != LIW_WEBDOM_NODE_NONE;
             attr_id = doc->attributes[attr_id].next) {
            const webdom_attribute_t *attr = &doc->attributes[attr_id];
            const char *attr_name = doc->strings + attr->name_offset;
            if (!wd_valid_serialized_name(attr_name, attr->name_length))
                return LIW_WEBDOM_INVALID;
            rc = wd_write(writer, " ", 1);
            if (rc == LIW_WEBDOM_OK)
                rc = wd_write(writer, attr_name, attr->name_length);
            if (rc == LIW_WEBDOM_OK) rc = wd_write(writer, "=\"", 2);
            if (rc == LIW_WEBDOM_OK)
                rc = wd_write_escaped(writer,
                    doc->strings + attr->value_offset, attr->value_length, 1,
                    attr->value_has_html_entities);
            if (rc == LIW_WEBDOM_OK) rc = wd_write(writer, "\"", 1);
            if (rc != LIW_WEBDOM_OK) return rc;
        }
        rc = wd_write(writer, ">", 1);
        if (rc != LIW_WEBDOM_OK) return rc;
        if (wd_is_void(name, node->name_length)) return LIW_WEBDOM_OK;
    }

    for (uint32_t child = node->first_child;
         child != LIW_WEBDOM_NODE_NONE; child = doc->nodes[child].next_sibling) {
        int rc = wd_serialize_node(doc, child, depth + 1, writer);
        if (rc != LIW_WEBDOM_OK) return rc;
    }
    if (!is_document) {
        int rc = wd_write(writer, "</", 2);
        if (rc == LIW_WEBDOM_OK) rc = wd_write(writer, name, node->name_length);
        if (rc == LIW_WEBDOM_OK) rc = wd_write(writer, ">", 1);
        if (rc != LIW_WEBDOM_OK) return rc;
    }
    return LIW_WEBDOM_OK;
}

int liw_webdom_serialize(const liw_web_document_t *doc, char *output,
                         size_t capacity, size_t *output_length) {
    if (!doc || !output_length || (output && capacity == 0) ||
        (!output && capacity != 0)) return LIW_WEBDOM_INVALID;
    webdom_writer_t measure = {0};
    int rc = wd_serialize_node(doc, 0, 0, &measure);
    if (rc != LIW_WEBDOM_OK) return rc;
    *output_length = measure.length;
    if (!output) return LIW_WEBDOM_OK;
    if (measure.length == SIZE_MAX || capacity <= measure.length)
        return LIW_WEBDOM_TOO_LARGE;
    webdom_writer_t writer = { .output = output, .capacity = capacity };
    rc = wd_serialize_node(doc, 0, 0, &writer);
    if (rc != LIW_WEBDOM_OK) return rc;
    output[writer.length] = '\0';
    return LIW_WEBDOM_OK;
}

int liw_webdom_serialize_node(const liw_web_document_t *doc,
                              liw_web_node_id_t node, char *output,
                              size_t capacity, size_t *output_length) {
    if (!wd_valid_node(doc, node) || !output_length ||
        (output && capacity == 0) || (!output && capacity != 0))
        return LIW_WEBDOM_INVALID;
    webdom_writer_t measure = {0};
    int rc = wd_serialize_node(doc, node, 0, &measure);
    if (rc != LIW_WEBDOM_OK) return rc;
    *output_length = measure.length;
    if (!output) return LIW_WEBDOM_OK;
    if (measure.length == SIZE_MAX || capacity <= measure.length)
        return LIW_WEBDOM_TOO_LARGE;
    webdom_writer_t writer = { .output = output, .capacity = capacity };
    rc = wd_serialize_node(doc, node, 0, &writer);
    if (rc == LIW_WEBDOM_OK) output[writer.length] = '\0';
    return rc;
}

static int wd_copy_parsed_node(liw_web_document_t *destination,
                               const liw_web_document_t *source,
                               uint32_t source_id, uint32_t *mapping) {
    const webdom_node_t *src = &source->nodes[source_id];
    uint32_t name_offset = 0, value_offset = 0;
    int rc;
    if (src->name_length) {
        rc = wd_store_string(destination,
            source->strings + src->name_offset, src->name_length,
            &name_offset);
        if (rc != LIW_WEBDOM_OK) return rc;
    }
    if (src->value_length) {
        rc = wd_store_string(destination,
            source->strings + src->value_offset, src->value_length,
            &value_offset);
        if (rc != LIW_WEBDOM_OK) return rc;
    }
    uint32_t id = wd_new_node(destination, src->type, name_offset,
        src->name_length, value_offset, src->value_length);
    if (id == LIW_WEBDOM_NODE_NONE)
        return destination->node_count >= LIW_WEBDOM_MAX_NODES
            ? LIW_WEBDOM_LIMIT : LIW_WEBDOM_NO_MEMORY;
    destination->nodes[id].value_has_html_entities = src->value_has_html_entities;
    mapping[source_id] = id;
    for (uint32_t attr_id = src->first_attribute;
         attr_id != LIW_WEBDOM_NODE_NONE; attr_id = source->attributes[attr_id].next) {
        const webdom_attribute_t *src_attr = &source->attributes[attr_id];
        uint32_t attr_name_offset, attr_value_offset;
        rc = wd_store_string(destination,
            source->strings + src_attr->name_offset, src_attr->name_length,
            &attr_name_offset);
        if (rc != LIW_WEBDOM_OK) return rc;
        rc = wd_store_string(destination,
            source->strings + src_attr->value_offset, src_attr->value_length,
            &attr_value_offset);
        if (rc != LIW_WEBDOM_OK) return rc;
        rc = wd_reserve_attributes(destination, destination->attribute_count + 1);
        if (rc != LIW_WEBDOM_OK) return rc;
        uint32_t new_attr_id = (uint32_t)destination->attribute_count++;
        webdom_attribute_t *new_attr = &destination->attributes[new_attr_id];
        new_attr->next = LIW_WEBDOM_NODE_NONE;
        new_attr->name_offset = attr_name_offset;
        new_attr->name_length = src_attr->name_length;
        new_attr->value_offset = attr_value_offset;
        new_attr->value_length = src_attr->value_length;
        new_attr->value_has_html_entities = src_attr->value_has_html_entities;
        webdom_node_t *dst_node = &destination->nodes[id];
        if (dst_node->last_attribute == LIW_WEBDOM_NODE_NONE)
            dst_node->first_attribute = new_attr_id;
        else destination->attributes[dst_node->last_attribute].next = new_attr_id;
        dst_node->last_attribute = new_attr_id;
        dst_node->attribute_count++;
    }
    if (src->parent != LIW_WEBDOM_NODE_NONE && src->parent != 0) {
        uint32_t parent = mapping[src->parent];
        if (parent == LIW_WEBDOM_NODE_NONE) return LIW_WEBDOM_INVALID;
        return wd_append_child_raw(destination, parent, id);
    }
    return LIW_WEBDOM_OK;
}

int liw_webdom_set_inner_html(liw_web_document_t *doc,
                              liw_web_node_id_t node_id,
                              const char *html, size_t length) {
    if (!wd_valid_node(doc, node_id) || (!html && length) ||
        (doc->nodes[node_id].type != LIW_WEBDOM_DOCUMENT &&
         doc->nodes[node_id].type != LIW_WEBDOM_FRAGMENT &&
         doc->nodes[node_id].type != LIW_WEBDOM_ELEMENT)) return LIW_WEBDOM_INVALID;
    liw_web_document_t *fragment = NULL;
    int rc = liw_webdom_parse(html ? html : "", length, &fragment);
    if (rc != LIW_WEBDOM_OK) return rc;
    size_t count = fragment->node_count;
    uint32_t *mapping = (uint32_t *)malloc(count * sizeof(*mapping));
    if (!mapping) { liw_webdom_destroy(fragment); return LIW_WEBDOM_NO_MEMORY; }
    for (size_t i = 0; i < count; ++i) mapping[i] = LIW_WEBDOM_NODE_NONE;
    mapping[0] = LIW_WEBDOM_NODE_NONE;
    size_t old_nodes = doc->node_count, old_attrs = doc->attribute_count;
    size_t old_strings = doc->strings_length;
    for (uint32_t i = 1; i < count; ++i) {
        rc = wd_copy_parsed_node(doc, fragment, i, mapping);
        if (rc != LIW_WEBDOM_OK) break;
    }
    if (rc == LIW_WEBDOM_OK) {
        webdom_node_t *target = &doc->nodes[node_id];
        wd_clear_children(doc, target);
        liw_web_node_id_t child = fragment->nodes[0].first_child;
        while (child != LIW_WEBDOM_NODE_NONE) {
            uint32_t copied = mapping[child];
            if (copied == LIW_WEBDOM_NODE_NONE ||
                wd_append_child_raw(doc, node_id, copied) != LIW_WEBDOM_OK) {
                rc = LIW_WEBDOM_INVALID;
                break;
            }
            child = fragment->nodes[child].next_sibling;
        }
    }
    if (rc != LIW_WEBDOM_OK) {
        doc->node_count = old_nodes;
        doc->attribute_count = old_attrs;
        doc->strings_length = old_strings;
    }
    free(mapping);
    liw_webdom_destroy(fragment);
    return rc;
}

liw_web_node_id_t liw_webdom_clone_node(liw_web_document_t *doc,
                                        liw_web_node_id_t node_id,
                                        int deep) {
    if (!wd_valid_node(doc, node_id)) return LIW_WEBDOM_NODE_NONE;
    if (doc->nodes[node_id].type == LIW_WEBDOM_FRAGMENT) {
        size_t old_nodes = doc->node_count, old_attrs = doc->attribute_count;
        size_t old_strings = doc->strings_length;
        uint32_t clone = liw_webdom_create_fragment(doc);
        if (clone == LIW_WEBDOM_NODE_NONE) return LIW_WEBDOM_NODE_NONE;
        if (deep) {
            uint32_t child = doc->nodes[node_id].first_child;
            while (child != LIW_WEBDOM_NODE_NONE) {
                uint32_t next = doc->nodes[child].next_sibling;
                uint32_t child_clone = liw_webdom_clone_node(doc, child, 1);
                if (child_clone == LIW_WEBDOM_NODE_NONE ||
                    liw_webdom_append_child(doc, clone, child_clone) != LIW_WEBDOM_OK) {
                    doc->node_count = old_nodes;
                    doc->attribute_count = old_attrs;
                    doc->strings_length = old_strings;
                    return LIW_WEBDOM_NODE_NONE;
                }
                child = next;
            }
        }
        return clone;
    }
    size_t html_length = 0;
    if (liw_webdom_serialize_node(doc, node_id, NULL, 0, &html_length) != LIW_WEBDOM_OK ||
        html_length > LIW_WEBDOM_MAX_SOURCE) return LIW_WEBDOM_NODE_NONE;
    char *html = (char *)malloc(html_length + 1);
    if (!html) return LIW_WEBDOM_NODE_NONE;
    if (liw_webdom_serialize_node(doc, node_id, html, html_length + 1,
                                 &html_length) != LIW_WEBDOM_OK) {
        free(html);
        return LIW_WEBDOM_NODE_NONE;
    }
    liw_web_document_t *fragment = NULL;
    int rc = liw_webdom_parse(html, html_length, &fragment);
    free(html);
    if (rc != LIW_WEBDOM_OK || !fragment) {
        liw_webdom_destroy(fragment);
        return LIW_WEBDOM_NODE_NONE;
    }
    uint32_t root = fragment->nodes[0].first_child;
    if (root == LIW_WEBDOM_NODE_NONE) {
        liw_webdom_destroy(fragment);
        return LIW_WEBDOM_NODE_NONE;
    }
    size_t count = fragment->node_count;
    uint32_t *mapping = (uint32_t *)malloc(count * sizeof(*mapping));
    if (!mapping) { liw_webdom_destroy(fragment); return LIW_WEBDOM_NODE_NONE; }
    for (size_t i = 0; i < count; ++i) mapping[i] = LIW_WEBDOM_NODE_NONE;
    size_t old_nodes = doc->node_count, old_attrs = doc->attribute_count;
    size_t old_strings = doc->strings_length;
    if (deep) {
        for (uint32_t i = 1; i < count; ++i) {
            rc = wd_copy_parsed_node(doc, fragment, i, mapping);
            if (rc != LIW_WEBDOM_OK) break;
        }
    } else {
        rc = wd_copy_parsed_node(doc, fragment, root, mapping);
    }
    liw_web_node_id_t result = rc == LIW_WEBDOM_OK
        ? mapping[root] : LIW_WEBDOM_NODE_NONE;
    if (rc != LIW_WEBDOM_OK) {
        doc->node_count = old_nodes;
        doc->attribute_count = old_attrs;
        doc->strings_length = old_strings;
    }
    free(mapping);
    liw_webdom_destroy(fragment);
    return result;
}

int liw_webdom_set_outer_html(liw_web_document_t *doc,
                              liw_web_node_id_t node_id,
                              const char *html, size_t length) {
    if (!wd_valid_node(doc, node_id) || node_id == 0 || (!html && length) ||
        doc->nodes[node_id].type != LIW_WEBDOM_ELEMENT)
        return LIW_WEBDOM_INVALID;
    uint32_t parent_id = doc->nodes[node_id].parent;
    if (!wd_valid_node(doc, parent_id)) return LIW_WEBDOM_INVALID;
    liw_web_document_t *fragment = NULL;
    int rc = liw_webdom_parse(html ? html : "", length, &fragment);
    if (rc != LIW_WEBDOM_OK) return rc;
    size_t count = fragment->node_count;
    uint32_t *mapping = (uint32_t *)malloc(count * sizeof(*mapping));
    if (!mapping) { liw_webdom_destroy(fragment); return LIW_WEBDOM_NO_MEMORY; }
    for (size_t i = 0; i < count; ++i) mapping[i] = LIW_WEBDOM_NODE_NONE;
    size_t old_nodes = doc->node_count, old_attrs = doc->attribute_count;
    size_t old_strings = doc->strings_length;
    for (uint32_t i = 1; i < count; ++i) {
        rc = wd_copy_parsed_node(doc, fragment, i, mapping);
        if (rc != LIW_WEBDOM_OK) break;
    }
    size_t parent_depth = 0;
    for (uint32_t p = parent_id; rc == LIW_WEBDOM_OK && p != 0;
         p = doc->nodes[p].parent) {
        if (p == LIW_WEBDOM_NODE_NONE || ++parent_depth > LIW_WEBDOM_MAX_DEPTH)
            rc = LIW_WEBDOM_LIMIT;
    }
    uint32_t child = fragment->nodes[0].first_child;
    while (rc == LIW_WEBDOM_OK && child != LIW_WEBDOM_NODE_NONE) {
        uint32_t copied = mapping[child];
        size_t subtree_depth = 0;
        if (copied == LIW_WEBDOM_NODE_NONE ||
            wd_subtree_depth(doc, copied, &subtree_depth) != LIW_WEBDOM_OK ||
            parent_depth + 1 + subtree_depth > LIW_WEBDOM_MAX_DEPTH) {
            rc = LIW_WEBDOM_LIMIT;
            break;
        }
        rc = liw_webdom_insert_before(doc, parent_id, copied, node_id);
        if (rc != LIW_WEBDOM_OK) break;
        child = fragment->nodes[child].next_sibling;
    }
    if (rc == LIW_WEBDOM_OK)
        rc = liw_webdom_remove_child(doc, parent_id, node_id);
    if (rc != LIW_WEBDOM_OK) {
        child = fragment->nodes[0].first_child;
        while (child != LIW_WEBDOM_NODE_NONE) {
            uint32_t copied = mapping[child];
            if (copied != LIW_WEBDOM_NODE_NONE &&
                liw_webdom_parent(doc, copied) == parent_id)
                (void)liw_webdom_remove_child(doc, parent_id, copied);
            child = fragment->nodes[child].next_sibling;
        }
        doc->node_count = old_nodes;
        doc->attribute_count = old_attrs;
        doc->strings_length = old_strings;
    }
    free(mapping);
    liw_webdom_destroy(fragment);
    return rc;
}
