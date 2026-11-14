#include "liwus/liwus.h"
#include "liwus/fcntl.h"
#include "liwus/stdlib.h"
#include "liwus/unistd.h"
#include "liwus/syscall.h"
#include <stdbool.h>

#define WEBTEXT_MAX_PAGE LIW_WEBDOM_MAX_SOURCE
#define WEBTEXT_PIPE_DRAIN_BYTES 512U

static int write_all(int fd, const char *text, size_t length) {
    while (length) {
        long n = liw_write(fd, text, length);
        if (n <= 0) return -1;
        text += (size_t)n;
        length -= (size_t)n;
    }
    return 0;
}

static char *execute_page_scripts(const char *path, size_t *length_out) {
    int pipefd[2] = {-1, -1};
    if (liw_pipe(pipefd) < 0) return NULL;

    int child = liw_fork();
    if (child < 0) {
        (void)liw_close(pipefd[0]);
        (void)liw_close(pipefd[1]);
        return NULL;
    }
    if (child == 0) {
        (void)liw_close(pipefd[0]);
        if (liw_dup2(pipefd[1], 1) != 1) liw_exit(126);
        (void)liw_close(pipefd[1]);
        char *const args[] = {"qjs", "--dump-html", (char *)path, NULL};
        (void)liw_execve("/bin/qjs.lxe", args, NULL);
        liw_exit(127);
    }

    (void)liw_close(pipefd[1]);
    char *output = (char *)malloc(WEBTEXT_MAX_PAGE + 1);
    if (!output) {
        (void)liw_close(pipefd[0]);
        int ignored_status = 0;
        (void)liw_waitpid(child, &ignored_status, 0);
        return NULL;
    }

    size_t used = 0;
    int too_large = 0;
    char drain[WEBTEXT_PIPE_DRAIN_BYTES];
    for (;;) {
        char *destination = drain;
        size_t capacity = sizeof(drain);
        if (!too_large && used <= WEBTEXT_MAX_PAGE) {
            destination = output + used;
            capacity = WEBTEXT_MAX_PAGE + 1 - used;
        }
        long count = liw_read(pipefd[0], destination, capacity);
        if (count < 0) {
            too_large = 1;
            break;
        }
        if (count == 0) break;
        if (destination == drain) continue;
        used += (size_t)count;
        if (used > WEBTEXT_MAX_PAGE) too_large = 1;
    }
    (void)liw_close(pipefd[0]);
    int child_status = -1;
    int waited = liw_waitpid(child, &child_status, 0);
    if (too_large || waited != child || child_status != 0) {
        free(output);
        return NULL;
    }
    output[used] = '\0';
    *length_out = used;
    return output;
}

static int slice_equal_ci(liw_web_slice_t slice, const char *word) {
    size_t length = liw_strlen(word);
    if (!slice.data || slice.length != length) return 0;
    for (size_t i = 0; i < length; ++i) {
        char a = slice.data[i];
        char b = word[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static liw_web_node_id_t next_after_subtree(const liw_web_document_t *doc,
                                            liw_web_node_id_t node) {
    liw_web_node_id_t root = liw_webdom_root(doc);
    while (node != LIW_WEBDOM_NODE_NONE && node != root) {
        liw_web_node_id_t next = liw_webdom_next_sibling(doc, node);
        if (next != LIW_WEBDOM_NODE_NONE) return next;
        node = liw_webdom_parent(doc, node);
    }
    return LIW_WEBDOM_NODE_NONE;
}

static liw_web_node_id_t find_element(const liw_web_document_t *doc,
                                      const char *name) {
    liw_web_node_id_t root = liw_webdom_root(doc);
    liw_web_node_id_t node = liw_webdom_first_child(doc, root);
    while (node != LIW_WEBDOM_NODE_NONE) {
        if (liw_webdom_node_type(doc, node) == LIW_WEBDOM_ELEMENT &&
            slice_equal_ci(liw_webdom_name(doc, node), name)) return node;
        liw_web_node_id_t child = liw_webdom_first_child(doc, node);
        node = child != LIW_WEBDOM_NODE_NONE ? child : next_after_subtree(doc, node);
    }
    return LIW_WEBDOM_NODE_NONE;
}

static int decode_slice(liw_web_slice_t value, char **scratch,
                        size_t *scratch_capacity, size_t *decoded_length) {
    size_t required = 0;
    if (liw_webdom_decode_entities(value.data, value.length, NULL, 0,
                                   &required) != LIW_WEBDOM_OK ||
        required == SIZE_MAX) return -1;
    if (required + 1 > *scratch_capacity) {
        char *new_scratch = (char *)realloc(*scratch, required + 1);
        if (!new_scratch) return -1;
        *scratch = new_scratch;
        *scratch_capacity = required + 1;
    }
    return liw_webdom_decode_entities(value.data, value.length, *scratch,
                                      *scratch_capacity, decoded_length);
}

static int write_title(int fd, const liw_web_document_t *doc, char **scratch,
                       size_t *scratch_capacity) {
    liw_web_node_id_t title = find_element(doc, "title");
    if (title == LIW_WEBDOM_NODE_NONE) return 0;
    liw_web_node_id_t node = liw_webdom_first_child(doc, title);
    while (node != LIW_WEBDOM_NODE_NONE && node != next_after_subtree(doc, title)) {
        if (liw_webdom_node_type(doc, node) == LIW_WEBDOM_TEXT) {
            liw_web_slice_t value = liw_webdom_value(doc, node);
            size_t decoded = 0;
            if (decode_slice(value, scratch, scratch_capacity, &decoded) < 0 ||
                write_all(fd, *scratch, decoded) < 0) return -1;
        }
        liw_web_node_id_t child = liw_webdom_first_child(doc, node);
        node = child != LIW_WEBDOM_NODE_NONE ? child : next_after_subtree(doc, node);
    }
    return 0;
}

static int hidden_element(const liw_web_document_t *doc, liw_web_node_id_t node) {
    static const char *const hidden[] = {
        "head", "script", "style", "noscript", "template", "svg"
    };
    if (liw_webdom_node_type(doc, node) != LIW_WEBDOM_ELEMENT) return 0;
    liw_web_slice_t name = liw_webdom_name(doc, node);
    for (size_t i = 0; i < sizeof(hidden) / sizeof(hidden[0]); ++i)
        if (slice_equal_ci(name, hidden[i])) return 1;
    return 0;
}

static int block_element(const liw_web_document_t *doc, liw_web_node_id_t node) {
    static const char *const blocks[] = {
        "address", "article", "blockquote", "br", "dd", "div", "dl",
        "dt", "fieldset", "footer", "form", "h1", "h2", "h3", "h4",
        "h5", "h6", "header", "hr", "li", "main", "ol", "p", "pre",
        "section", "table", "tr", "ul"
    };
    if (liw_webdom_node_type(doc, node) != LIW_WEBDOM_ELEMENT) return 0;
    liw_web_slice_t name = liw_webdom_name(doc, node);
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i)
        if (slice_equal_ci(name, blocks[i])) return 1;
    return 0;
}

static int text_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

typedef struct {
    int fd;
    char bytes[512];
    size_t length;
} text_writer_t;

static int writer_flush(text_writer_t *writer) {
    if (!writer->length) return 0;
    if (write_all(writer->fd, writer->bytes, writer->length) < 0) return -1;
    writer->length = 0;
    return 0;
}

static int writer_put(text_writer_t *writer, char byte) {
    if (writer->length == sizeof(writer->bytes) && writer_flush(writer) < 0)
        return -1;
    writer->bytes[writer->length++] = byte;
    return 0;
}

static int write_page_text(int fd, const liw_web_document_t *doc,
                           char **scratch, size_t *scratch_capacity) {
    text_writer_t writer = { .fd = fd, .length = 0 };
    int line_start = 1;
    int pending_space = 0;
    liw_web_node_id_t root = liw_webdom_root(doc);
    liw_web_node_id_t node = liw_webdom_first_child(doc, root);
    while (node != LIW_WEBDOM_NODE_NONE) {
        if (hidden_element(doc, node)) {
            node = next_after_subtree(doc, node);
            continue;
        }
        if (block_element(doc, node)) {
            if (!line_start && writer_put(&writer, '\n') < 0) return -1;
            line_start = 1;
            pending_space = 0;
        }
        if (liw_webdom_node_type(doc, node) == LIW_WEBDOM_TEXT) {
            liw_web_slice_t value = liw_webdom_value(doc, node);
            size_t decoded = 0;
            if (decode_slice(value, scratch, scratch_capacity, &decoded) < 0)
                return -1;
            for (size_t i = 0; i < decoded; ++i) {
                char c = (*scratch)[i];
                if (text_whitespace(c)) {
                    pending_space = !line_start;
                    continue;
                }
                if (pending_space) {
                    if (writer_put(&writer, ' ') < 0) return -1;
                    pending_space = 0;
                }
                if (writer_put(&writer, c) < 0) return -1;
                line_start = 0;
            }
        }
        if (block_element(doc, node)) {
            liw_web_slice_t name = liw_webdom_name(doc, node);
            if (slice_equal_ci(name, "br") || slice_equal_ci(name, "hr") ||
                liw_webdom_first_child(doc, node) == LIW_WEBDOM_NODE_NONE) {
                if (writer_put(&writer, '\n') < 0) return -1;
                line_start = 1;
            }
        }
        liw_web_node_id_t child = liw_webdom_first_child(doc, node);
        node = child != LIW_WEBDOM_NODE_NONE ? child : next_after_subtree(doc, node);
    }
    if (!line_start && writer_put(&writer, '\n') < 0) return -1;
    return writer_flush(&writer);
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    bool execute_scripts = argc >= 2 && liw_strcmp(argv[1], "--js") == 0;
    if ((!execute_scripts && (argc < 2 || argc > 3)) ||
        (execute_scripts && (argc < 3 || argc > 4))) {
        static const char usage[] = "usage: webtext URL [SAVE_PATH]\n"
                                    "       webtext --js URL [SAVE_PATH]\n";
        (void)write_all(2, usage, sizeof(usage) - 1);
        return 2;
    }
    int url_index = execute_scripts ? 2 : 1;
    bool has_save_path = execute_scripts ? argc == 4 : argc == 3;
    const char *path = has_save_path ? argv[execute_scripts ? 3 : 2]
                                     : "/webtext-page.html";
    static const char downloading[] = "webtext: downloading page...\n";
    (void)write_all(1, downloading, sizeof(downloading) - 1);
    long downloaded = liw_net_http_get(argv[url_index], path);
    if (downloaded <= 0) {
        static const char error[] = "webtext: download failed\n";
        (void)write_all(2, error, sizeof(error) - 1);
        return 1;
    }

    int fd = liw_open(path, LIW_O_RDONLY);
    if (fd < 0) {
        static const char error[] = "webtext: cannot open downloaded page\n";
        (void)write_all(2, error, sizeof(error) - 1);
        return 1;
    }
    struct liw_stat info;
    if (liw_fstat(fd, &info) < 0 || info.st_size == 0 ||
        info.st_size > WEBTEXT_MAX_PAGE) {
        (void)liw_close(fd);
        static const char error[] = "webtext: page is empty or exceeds the 4 MiB limit\n";
        (void)write_all(2, error, sizeof(error) - 1);
        return 1;
    }
    size_t length = (size_t)info.st_size;
    char *html = NULL;
    char *scratch = NULL;
    size_t scratch_capacity = 0;
    if (execute_scripts) {
        (void)liw_close(fd);
        static const char running[] =
            "webtext: executing bounded inline JavaScript in qjs...\n";
        (void)write_all(1, running, sizeof(running) - 1);
        html = execute_page_scripts(path, &length);
        if (!html) {
            static const char error[] =
                "webtext: script execution failed or output exceeded 4 MiB\n";
            (void)write_all(2, error, sizeof(error) - 1);
            return 1;
        }
    } else {
        html = (char *)malloc(length + 1);
        if (!html) {
            (void)liw_close(fd);
            static const char error[] = "webtext: out of memory\n";
            (void)write_all(2, error, sizeof(error) - 1);
            return 1;
        }
        size_t done = 0;
        while (done < length) {
            long count = liw_read(fd, html + done, length - done);
            if (count <= 0) break;
            done += (size_t)count;
        }
        (void)liw_close(fd);
        if (done != length) {
            free(html);
            static const char error[] = "webtext: incomplete read\n";
            (void)write_all(2, error, sizeof(error) - 1);
            return 1;
        }
        html[length] = '\0';
    }

    liw_web_document_t *doc = NULL;
    int rc = liw_webdom_parse(html, length, &doc);
    free(html);
    if (rc != LIW_WEBDOM_OK) {
        static const char error[] = "webtext: HTML parse failed or page exceeds parser limits\n";
        (void)write_all(2, error, sizeof(error) - 1);
        return 1;
    }

    static const char title_prefix[] = "\nTitle: ";
    static const char title_suffix[] = "\n\n";
    static const char saved_prefix[] = "Saved HTML to ";
    static const char saved_suffix[] = "\n";
    (void)write_all(1, title_prefix, sizeof(title_prefix) - 1);
    if (write_title(1, doc, &scratch, &scratch_capacity) < 0)
        (void)write_all(1, "(unavailable)", 13);
    (void)write_all(1, title_suffix, sizeof(title_suffix) - 1);
    int output_rc = write_page_text(1, doc, &scratch, &scratch_capacity);
    (void)write_all(1, saved_prefix, sizeof(saved_prefix) - 1);
    (void)write_all(1, path, liw_strlen(path));
    (void)write_all(1, saved_suffix, sizeof(saved_suffix) - 1);
    liw_webdom_destroy(doc);
    free(scratch);
    if (output_rc < 0) {
        static const char error[] = "webtext: terminal output failed\n";
        (void)write_all(2, error, sizeof(error) - 1);
        return 1;
    }
    return 0;
}
