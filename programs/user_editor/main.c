#include "liwus.h"
#include "termios.h"

#define EDIT_CAPACITY 16384
#define SCREEN_ROWS 25
#define SCREEN_COLS 80
#define GUTTER_WIDTH 5
#define O_CREAT 0x0200
#define O_TRUNC 0x0400
#define TCGETS 0x5401
#define TCSETS 0x5402
#define EDITOR_BODY_ROWS (screen_rows > 3 ? screen_rows - 3 : 1)

typedef struct {
    unsigned short rows;
    unsigned short cols;
    unsigned short xpixel;
    unsigned short ypixel;
} winsize_t;

enum { MODE_NORMAL, MODE_INSERT };

static char document[EDIT_CAPACITY];
static unsigned long document_length;
static unsigned long cursor;
static unsigned long first_line;
static int dirty;
static int mode = MODE_NORMAL;
static char message[128];
static unsigned int screen_rows = SCREEN_ROWS;
static unsigned int screen_cols = SCREEN_COLS;

static unsigned long string_length(const char *s) {
    unsigned long n = 0;
    while (s && s[n]) n++;
    return n;
}

static void write_bytes(const char *s, unsigned long n) {
    while (n) {
        long result = liw_write(1, s, n);
        if (result <= 0) return;
        s += result;
        n -= (unsigned long)result;
    }
}

static void write_text(const char *s) {
    write_bytes(s, string_length(s));
}

static void write_number(unsigned long value, unsigned int width) {
    char digits[24];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(digits));
    while (count < width) {
        write_bytes(" ", 1);
        width--;
    }
    while (count) write_bytes(&digits[--count], 1);
}

static void move_screen_cursor(unsigned int row, unsigned int col) {
    char seq[24];
    unsigned int n = 0;
    seq[n++] = '\033'; seq[n++] = '[';
    char digits[10];
    unsigned int count = 0;
    do { digits[count++] = (char)('0' + row % 10); row /= 10; } while (row);
    while (count) seq[n++] = digits[--count];
    seq[n++] = ';';
    count = 0;
    do { digits[count++] = (char)('0' + col % 10); col /= 10; } while (col);
    while (count) seq[n++] = digits[--count];
    seq[n++] = 'H';
    write_bytes(seq, n);
}

static unsigned long line_start(unsigned long line) {
    unsigned long at = 0;
    while (line && at < document_length) {
        if (document[at++] == '\n') line--;
    }
    return at;
}

static unsigned long line_end(unsigned long at) {
    while (at < document_length && document[at] != '\n') at++;
    return at;
}

static unsigned long current_line(void) {
    unsigned long line = 0;
    for (unsigned long i = 0; i < cursor; i++)
        if (document[i] == '\n') line++;
    return line;
}

static unsigned long current_column(void) {
    unsigned long start = cursor;
    while (start && document[start - 1] != '\n') start--;
    return cursor - start;
}

static unsigned long line_count(void) {
    unsigned long lines = 1;
    for (unsigned long i = 0; i < document_length; i++)
        if (document[i] == '\n') lines++;
    return lines;
}

static void set_message(const char *text) {
    unsigned long i = 0;
    while (text[i] && i < sizeof(message) - 1) {
        message[i] = text[i];
        i++;
    }
    message[i] = '\0';
}

static int load_file(const char *path) {
    long fd = liw_open(path, 0);
    if (fd < 0) return 0;
    char chunk[256];
    for (;;) {
        long count = liw_read((int)fd, chunk, sizeof(chunk));
        if (count < 0) {
            liw_close((int)fd);
            set_message("read failed");
            return -1;
        }
        if (count == 0) break;
        if (document_length + (unsigned long)count > sizeof(document)) {
            liw_close((int)fd);
            set_message("file exceeds 16 KiB; refusing to edit");
            return -1;
        }
        for (long i = 0; i < count; i++)
            document[document_length++] = chunk[i];
    }
    liw_close((int)fd);
    return 1;
}

static int save_file(const char *path) {
    long fd = liw_open(path, O_CREAT | O_TRUNC);
    if (fd < 0) {
        set_message("cannot open file for writing");
        return -1;
    }
    unsigned long written_total = 0;
    while (written_total < document_length) {
        long count = liw_write((int)fd, document + written_total,
                               document_length - written_total);
        if (count <= 0) {
            liw_close((int)fd);
            set_message("write failed");
            return -1;
        }
        written_total += (unsigned long)count;
    }
    liw_close((int)fd);
    dirty = 0;
    set_message("written to disk");
    return 0;
}

static void scroll_to_cursor(void) {
    unsigned long line = current_line();
    if (line < first_line) first_line = line;
    else if (line >= first_line + EDITOR_BODY_ROWS)
        first_line = line - EDITOR_BODY_ROWS + 1;
}

static void draw_line(unsigned long line, unsigned int screen_row) {
    move_screen_cursor(screen_row, 1);
    write_text("\033[2K");
    if (line >= line_count()) return;
    unsigned long start = line_start(line);
    unsigned long end = line_end(start);
    write_number(line + 1, 4);
    write_text(" ");
    unsigned long visible = screen_cols > GUTTER_WIDTH
        ? screen_cols - GUTTER_WIDTH : 1;
    unsigned long count = end - start;
    if (count > visible) count = visible;
    write_bytes(document + start, count);
}

static void render(const char *path) {
    scroll_to_cursor();
    write_text("\033[2J\033[H\033[0m");
    write_text("\033[7m LIWUS EDITOR ");
    write_text(mode == MODE_INSERT ? "-- INSERT -- " : "-- NORMAL -- ");
    write_text(dirty ? " [+]" : "     ");
    write_text("\033[0m\033[K");

    move_screen_cursor(2, 1);
    write_text(path);
    write_text("\033[K");
    for (unsigned int row = 0; row < EDITOR_BODY_ROWS; row++)
        draw_line(first_line + row, row + 3);

    move_screen_cursor(screen_rows, 1);
    write_text("\033[7m ");
    write_text(message[0] ? message : "i insert  Esc normal  W save  Q quit  X discard+quit");
    write_text("\033[0m\033[K");

    unsigned long line = current_line();
    unsigned long col = current_column();
    unsigned long display_row = line - first_line + 3;
    unsigned long display_col = GUTTER_WIDTH + col + 1;
    if (display_col > screen_cols) display_col = screen_cols;
    move_screen_cursor((unsigned int)display_row, (unsigned int)display_col);
}

static void insert_byte(char c) {
    if (document_length >= sizeof(document)) {
        set_message("document is full (16 KiB)");
        return;
    }
    for (unsigned long i = document_length; i > cursor; i--)
        document[i] = document[i - 1];
    document[cursor++] = c;
    document_length++;
    dirty = 1;
    message[0] = '\0';
}

static void delete_at_cursor(void) {
    if (cursor >= document_length) return;
    for (unsigned long i = cursor; i + 1 < document_length; i++)
        document[i] = document[i + 1];
    document_length--;
    dirty = 1;
}

static void backspace(void) {
    if (!cursor) return;
    cursor--;
    delete_at_cursor();
}

static void move_vertical(int direction) {
    unsigned long line = current_line();
    unsigned long col = current_column();
    if (direction < 0 && line == 0) return;
    if (direction > 0 && line + 1 >= line_count()) return;
    unsigned long target = line_start(direction < 0 ? line - 1 : line + 1);
    unsigned long end = line_end(target);
    cursor = target + col;
    if (cursor > end) cursor = end;
}

static void open_line(int below) {
    unsigned long start = line_start(current_line());
    unsigned long end = line_end(start);
    if (below) {
        cursor = end;
        insert_byte('\n');
    } else {
        cursor = start;
        insert_byte('\n');
        cursor = start;
    }
    mode = MODE_INSERT;
}

/* Read one key. Raw-mode arrow bytes are converted by the kernel into ANSI
 * sequences; the short VTIME lets a lone Escape key finish without blocking. */
static int read_key(void) {
    char c;
    for (;;) {
        long n = liw_read(0, &c, 1);
        if (n < 0) return -1;
        if (n > 0) break;
    }
    if ((unsigned char)c != 27) return (unsigned char)c;

    char second;
    long n = liw_read(0, &second, 1);
    if (n <= 0 || second != '[') return 27;
    char third;
    n = liw_read(0, &third, 1);
    if (n <= 0) return 27;
    switch (third) {
        case 'A': return 128;
        case 'B': return 129;
        case 'D': return 130;
        case 'C': return 131;
        default: return 0;
    }
}

static void move_horizontal(int direction) {
    if (direction < 0) {
        if (cursor) cursor--;
    } else if (cursor < document_length) {
        cursor++;
    }
}

static int handle_key(int key, const char *path) {
    if (key == 19) { /* Ctrl+S */
        save_file(path);
        return 0;
    }
    if (key == 3) {
        set_message("Ctrl-C ignored; use Shift+Q or Shift+X to leave editor");
        return 0;
    }

    if (mode == MODE_INSERT) {
        if (key == 27) { mode = MODE_NORMAL; return 0; }
        if (key == 128) move_vertical(-1);
        else if (key == 129) move_vertical(1);
        else if (key == 130) move_horizontal(-1);
        else if (key == 131) move_horizontal(1);
        else if (key == 8 || key == 127) backspace();
        else if (key == '\r' || key == '\n') insert_byte('\n');
        else if (key >= 32 && key <= 126) insert_byte((char)key);
        return 0;
    }

    if (key == 'W') { save_file(path); return 0; }
    if (key == 'Q') {
        if (dirty) {
            set_message("unsaved changes: Shift+W saves, Shift+X discards and quits");
            return 0;
        }
        return 1;
    }
    if (key == 'X') return 1;
    if (key == 'i') { mode = MODE_INSERT; return 0; }
    if (key == 'a') { if (cursor < document_length) cursor++; mode = MODE_INSERT; return 0; }
    if (key == 'o') { open_line(1); return 0; }
    if (key == 'O') { open_line(0); return 0; }
    if (key == 'x') { delete_at_cursor(); return 0; }
    if (key == 'h' || key == 130) move_horizontal(-1);
    else if (key == 'l' || key == 131) move_horizontal(1);
    else if (key == 'k' || key == 128) move_vertical(-1);
    else if (key == 'j' || key == 129) move_vertical(1);
    else if (key == '0') cursor = line_start(current_line());
    else if (key == '$') cursor = line_end(line_start(current_line()));
    else if (key == 127 || key == 8) backspace();
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        write_text("usage: edit FILE\n");
        return 2;
    }
    const char *path = argv[1];
    int loaded = load_file(path);
    if (loaded < 0) return 1;
    if (!loaded) set_message("new file");
    else set_message("file loaded");

    struct termios original, raw;
    if (liw_ioctl(0, TCGETS, &original) < 0) {
        write_text("edit: cannot read terminal settings\n");
        return 1;
    }
    raw = original;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (liw_ioctl(0, TCSETS, &raw) < 0) {
        write_text("edit: cannot enable raw terminal mode\n");
        return 1;
    }

    winsize_t size;
    if (liw_ioctl(0, 0x5413, &size) == 0 && size.rows >= 8 && size.cols >= 24) {
        screen_rows = size.rows;
        screen_cols = size.cols;
    }
    int quit = 0;
    while (!quit) {
        render(path);
        int key = read_key();
        if (key < 0) { set_message("input error"); break; }
        quit = handle_key(key, path);
    }

    liw_ioctl(0, TCSETS, &original);
    write_text("\033[0m\033[2J\033[H");
    return 0;
}
