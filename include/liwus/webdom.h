#ifndef LIWUS_WEBDOM_H
#define LIWUS_WEBDOM_H

#include <stddef.h>
#include <stdint.h>

/* Bounded, mutable DOM core for untrusted web content in ring-3 programs. */
#define LIW_WEBDOM_MAX_SOURCE (4U * 1024U * 1024U)
#define LIW_WEBDOM_MAX_NODES  32768U
#define LIW_WEBDOM_MAX_ATTRS  32768U
#define LIW_WEBDOM_MAX_DEPTH  256U
#define LIW_WEBDOM_NODE_NONE  UINT32_MAX

typedef struct liw_web_document liw_web_document_t;
typedef uint32_t liw_web_node_id_t;

typedef enum {
    LIW_WEBDOM_DOCUMENT = 1,
    LIW_WEBDOM_ELEMENT = 2,
    LIW_WEBDOM_TEXT = 3,
    LIW_WEBDOM_COMMENT = 4,
    LIW_WEBDOM_FRAGMENT = 5
} liw_web_node_type_t;

typedef struct {
    const char *data;
    size_t length;
} liw_web_slice_t;

typedef enum {
    LIW_WEBDOM_OK = 0,
    LIW_WEBDOM_INVALID = -1,
    LIW_WEBDOM_TOO_LARGE = -2,
    LIW_WEBDOM_NO_MEMORY = -3,
    LIW_WEBDOM_LIMIT = -4,
    LIW_WEBDOM_NOT_FOUND = -5
} liw_webdom_result_t;

/* Parse HTML into a ring-3-owned DOM. Script/style bodies remain text nodes. */
int liw_webdom_parse(const void *html, size_t length,
                     liw_web_document_t **out_document);
void liw_webdom_destroy(liw_web_document_t *document);

liw_web_node_id_t liw_webdom_root(const liw_web_document_t *document);
size_t liw_webdom_node_count(const liw_web_document_t *document);
liw_web_node_type_t liw_webdom_node_type(const liw_web_document_t *document,
                                          liw_web_node_id_t node);
liw_web_node_id_t liw_webdom_parent(const liw_web_document_t *document,
                                    liw_web_node_id_t node);
liw_web_node_id_t liw_webdom_first_child(const liw_web_document_t *document,
                                        liw_web_node_id_t node);
liw_web_node_id_t liw_webdom_last_child(const liw_web_document_t *document,
                                        liw_web_node_id_t node);
liw_web_node_id_t liw_webdom_previous_sibling(const liw_web_document_t *document,
                                              liw_web_node_id_t node);
liw_web_node_id_t liw_webdom_next_sibling(const liw_web_document_t *document,
                                         liw_web_node_id_t node);
int liw_webdom_is_connected(const liw_web_document_t *document,
                            liw_web_node_id_t node);
liw_web_slice_t liw_webdom_name(const liw_web_document_t *document,
                                liw_web_node_id_t node);
liw_web_slice_t liw_webdom_value(const liw_web_document_t *document,
                                 liw_web_node_id_t node);
int liw_webdom_text_needs_entity_decode(const liw_web_document_t *document,
                                        liw_web_node_id_t node);

size_t liw_webdom_attribute_count(const liw_web_document_t *document,
                                  liw_web_node_id_t node);
int liw_webdom_attribute_at(const liw_web_document_t *document,
                            liw_web_node_id_t node, size_t index,
                            liw_web_slice_t *name, liw_web_slice_t *value);
int liw_webdom_get_attribute(const liw_web_document_t *document,
                             liw_web_node_id_t node, const char *name,
                             liw_web_slice_t *value);
int liw_webdom_attribute_needs_entity_decode(
    const liw_web_document_t *document, liw_web_node_id_t node,
    const char *name);
/* Decode semicolon-terminated HTML character references into UTF-8. Passing
 * output=NULL, capacity=0 measures the required output length. Otherwise,
 * capacity includes the final NUL and input/output must not overlap. */
int liw_webdom_decode_entities(const char *input, size_t length,
                              char *output, size_t capacity,
                              size_t *output_length);
liw_web_node_id_t liw_webdom_get_element_by_id(const liw_web_document_t *document,
                                                const char *id);

/* Mutations allocate in the document arena. All returned slices are borrowed
 * from that arena and remain valid only until its next mutation or destruction. */
liw_web_node_id_t liw_webdom_create_element(liw_web_document_t *document,
                                            const char *name, size_t length);
liw_web_node_id_t liw_webdom_create_text(liw_web_document_t *document,
                                        const char *text, size_t length);
liw_web_node_id_t liw_webdom_create_comment(liw_web_document_t *document,
                                           const char *text, size_t length);
liw_web_node_id_t liw_webdom_create_fragment(liw_web_document_t *document);
int liw_webdom_append_child(liw_web_document_t *document,
                            liw_web_node_id_t parent, liw_web_node_id_t child);
int liw_webdom_insert_before(liw_web_document_t *document,
                             liw_web_node_id_t parent, liw_web_node_id_t child,
                             liw_web_node_id_t reference_child);
int liw_webdom_remove_child(liw_web_document_t *document,
                            liw_web_node_id_t parent, liw_web_node_id_t child);
int liw_webdom_set_text(liw_web_document_t *document, liw_web_node_id_t node,
                        const char *text, size_t length);
int liw_webdom_set_inner_html(liw_web_document_t *document,
                              liw_web_node_id_t node,
                              const char *html, size_t length);
int liw_webdom_set_outer_html(liw_web_document_t *document,
                              liw_web_node_id_t node,
                              const char *html, size_t length);
liw_web_node_id_t liw_webdom_clone_node(liw_web_document_t *document,
                                        liw_web_node_id_t node, int deep);
int liw_webdom_set_attribute(liw_web_document_t *document,
                             liw_web_node_id_t node, const char *name,
                             size_t name_length, const char *value,
                             size_t value_length);
int liw_webdom_remove_attribute(liw_web_document_t *document,
                                liw_web_node_id_t node, const char *name);

/* Serialize the current DOM tree as bounded HTML. Passing output=NULL and
 * capacity=0 measures the required byte count (excluding the trailing NUL).
 * A non-NULL output needs capacity > required; no partial output is returned
 * when the buffer is too small. Comments are preserved. */
int liw_webdom_serialize(const liw_web_document_t *document,
                         char *output, size_t capacity,
                         size_t *output_length);
int liw_webdom_serialize_node(const liw_web_document_t *document,
                              liw_web_node_id_t node, char *output,
                              size_t capacity, size_t *output_length);

#endif
