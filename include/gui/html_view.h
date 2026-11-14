#ifndef GUI_HTML_VIEW_H
#define GUI_HTML_VIEW_H

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*html_link_click_cb_t)(const char *href, void *user_data);
typedef bool (*html_fetch_resource_cb_t)(const char *url,
                                         uint8_t **out_data, size_t *out_len,
                                         bool *out_caller_free,
                                         void *user_data);
typedef void (*html_input_focus_cb_t)(lv_obj_t *ta, void *user_data);
typedef void (*html_render_status_cb_t)(const char *msg, void *user_data);
typedef void (*html_dom_event_cb_t)(const char *element_id, const char *event,
                                    const char *value, void *user_data);

typedef struct {
    html_link_click_cb_t on_link;
    html_fetch_resource_cb_t fetch_resource;
    html_input_focus_cb_t on_input_focus;
    html_dom_event_cb_t on_dom_event;
    html_render_status_cb_t on_status;
    int viewport_w;
    int viewport_h;
    void *user_data;
} html_view_opts_t;

/* Renders HTML/CSS (or plain text) inside a scrollable LVGL container.
 * Optionally writes the <title> text into `title_out`.
 */
void html_view_render(lv_obj_t *container, const char *content, size_t len,
                      const char *base_url,
                      char *title_out, size_t title_out_sz,
                      const html_view_opts_t *opts);

/* Expands a bounded set of external classic and module scripts to inline text
 * for the isolated page-script runner, preserving module script type. Returns
 * 1 with an allocated result, 0 if no scripts were expanded, or -1 on invalid
 * input/allocation/size failure. Module import graphs still need a loader. */
int html_view_inline_external_scripts(const char *content, size_t len,
                                     const char *base_url,
                                     html_fetch_resource_cb_t fetch_resource,
                                     void *user_data,
                                     uint8_t **out_content,
                                     size_t *out_len);

/* Shows a clean status/error banner in the browser viewport. */
void html_view_show_message(lv_obj_t *container, const char *title,
                            const char *message, lv_color_t accent);

#endif /* GUI_HTML_VIEW_H */
