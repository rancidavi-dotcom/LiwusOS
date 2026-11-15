#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"
#include <liwus/fcntl.h>
#include <liwus/stat.h>
#include <liwus/syscall.h>
#include <liwus/unistd.h>

typedef struct {
    sqlite3_file base;
    int fd;
    int delete_on_close;
    char path[256];
} liw_sqlite_file;

static unsigned temp_file_id;

static int sqlite_file_close(sqlite3_file *base) {
    liw_sqlite_file *file = (liw_sqlite_file *)base;
    int rc = SQLITE_OK;
    if (file->fd >= 0 && liw_close(file->fd) < 0) rc = SQLITE_IOERR_CLOSE;
    file->fd = -1;
    if (file->delete_on_close && file->path[0]) (void)liw_unlink(file->path);
    file->base.pMethods = NULL;
    return rc;
}

static int sqlite_file_read(sqlite3_file *base, void *buffer, int amount,
                            sqlite3_int64 offset) {
    liw_sqlite_file *file = (liw_sqlite_file *)base;
    if (offset < 0 || offset > INT32_MAX ||
        liw_lseek(file->fd, (int32_t)offset, SEEK_SET) < 0)
        return SQLITE_IOERR_SEEK;
    int done = 0;
    while (done < amount) {
        long n = liw_read(file->fd, (char *)buffer + done, (size_t)(amount - done));
        if (n < 0) return SQLITE_IOERR_READ;
        if (n == 0) break;
        done += (int)n;
    }
    if (done == amount) return SQLITE_OK;
    memset((char *)buffer + done, 0, (size_t)(amount - done));
    return SQLITE_IOERR_SHORT_READ;
}

static int sqlite_file_write(sqlite3_file *base, const void *buffer, int amount,
                             sqlite3_int64 offset) {
    liw_sqlite_file *file = (liw_sqlite_file *)base;
    if (offset < 0 || offset > INT32_MAX ||
        liw_lseek(file->fd, (int32_t)offset, SEEK_SET) < 0)
        return SQLITE_IOERR_SEEK;
    int done = 0;
    while (done < amount) {
        long n = liw_write(file->fd, (const char *)buffer + done,
                           (size_t)(amount - done));
        if (n <= 0) return SQLITE_IOERR_WRITE;
        done += (int)n;
    }
    return SQLITE_OK;
}

static int sqlite_file_truncate(sqlite3_file *base, sqlite3_int64 size) {
    liw_sqlite_file *file = (liw_sqlite_file *)base;
    if (size < 0 || size > INT32_MAX) return SQLITE_IOERR_TRUNCATE;
    if (size == 0) {
        int next = liw_open(file->path, LIW_O_RDWR | LIW_O_CREAT | LIW_O_TRUNC);
        if (next < 0) return SQLITE_IOERR_TRUNCATE;
        int old = file->fd;
        file->fd = next;
        if (old >= 0) (void)liw_close(old);
        return SQLITE_OK;
    }
    struct liw_stat st;
    if (liw_fstat(file->fd, &st) < 0) return SQLITE_IOERR_FSTAT;
    if ((uint64_t)size == st.st_size) return SQLITE_OK;
    if ((uint64_t)size > st.st_size) {
        if (liw_lseek(file->fd, (int32_t)st.st_size, SEEK_SET) < 0)
            return SQLITE_IOERR_SEEK;
        static const uint8_t zeros[512] = {0};
        uint64_t remaining = (uint64_t)size - st.st_size;
        while (remaining) {
            size_t nbytes = remaining > sizeof(zeros) ? sizeof(zeros) : (size_t)remaining;
            if (liw_write(file->fd, zeros, nbytes) != (long)nbytes)
                return SQLITE_IOERR_TRUNCATE;
            remaining -= nbytes;
        }
        return SQLITE_OK;
    }
    return SQLITE_IOERR_TRUNCATE;
}

static int sqlite_file_sync(sqlite3_file *base, int flags) {
    (void)base; (void)flags;
    /* SDFS writes are committed by the VFS write path; there is no fsync syscall yet. */
    return SQLITE_OK;
}

static int sqlite_file_size(sqlite3_file *base, sqlite3_int64 *size) {
    struct liw_stat st;
    if (liw_fstat(((liw_sqlite_file *)base)->fd, &st) < 0)
        return SQLITE_IOERR_FSTAT;
    *size = (sqlite3_int64)st.st_size;
    return SQLITE_OK;
}

static int sqlite_file_lock(sqlite3_file *base, int level) {
    (void)base; (void)level;
    return SQLITE_OK;
}

static int sqlite_file_unlock(sqlite3_file *base, int level) {
    (void)base; (void)level;
    return SQLITE_OK;
}

static int sqlite_file_check_lock(sqlite3_file *base, int *out) {
    (void)base;
    *out = 0;
    return SQLITE_OK;
}

static int sqlite_file_control(sqlite3_file *base, int op, void *arg) {
    (void)base; (void)op; (void)arg;
    return SQLITE_NOTFOUND;
}

static int sqlite_file_sector_size(sqlite3_file *base) {
    (void)base;
    return 512;
}

static int sqlite_file_characteristics(sqlite3_file *base) {
    (void)base;
    return 0;
}

static const sqlite3_io_methods sqlite_methods = {
    1,
    sqlite_file_close,
    sqlite_file_read,
    sqlite_file_write,
    sqlite_file_truncate,
    sqlite_file_sync,
    sqlite_file_size,
    sqlite_file_lock,
    sqlite_file_unlock,
    sqlite_file_check_lock,
    sqlite_file_control,
    sqlite_file_sector_size,
    sqlite_file_characteristics,
    NULL, NULL, NULL, NULL, NULL, NULL
};

static int sqlite_vfs_open(sqlite3_vfs *vfs, const char *name,
                           sqlite3_file *base, int flags, int *out_flags) {
    (void)vfs;
    liw_sqlite_file *file = (liw_sqlite_file *)base;
    memset(file, 0, sizeof(*file));
    file->fd = -1;
    if (name && *name) {
        size_t n = strlen(name);
        if (n >= sizeof(file->path)) return SQLITE_CANTOPEN;
        memcpy(file->path, name, n + 1);
    } else {
        ++temp_file_id;
        snprintf(file->path, sizeof(file->path), "/sqlite-temp-%u", temp_file_id);
        flags |= SQLITE_OPEN_DELETEONCLOSE | SQLITE_OPEN_CREATE | SQLITE_OPEN_EXCLUSIVE;
    }

    int open_flags = (flags & SQLITE_OPEN_READONLY) ? LIW_O_RDONLY : LIW_O_RDWR;
    if (flags & SQLITE_OPEN_CREATE) open_flags |= LIW_O_CREAT;
    if (flags & SQLITE_OPEN_EXCLUSIVE) open_flags |= LIW_O_EXCL;
    file->fd = liw_open(file->path, open_flags);
    if (file->fd < 0 && (flags & SQLITE_OPEN_EXCLUSIVE)) {
        open_flags &= ~LIW_O_EXCL;
        file->fd = liw_open(file->path, open_flags);
    }
    if (file->fd < 0) return SQLITE_CANTOPEN;
    file->delete_on_close = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
    file->base.pMethods = &sqlite_methods;
    if (out_flags) *out_flags = flags;
    return SQLITE_OK;
}

static int sqlite_vfs_delete(sqlite3_vfs *vfs, const char *name, int sync_dir) {
    (void)vfs; (void)sync_dir;
    if (liw_unlink(name) == 0) return SQLITE_OK;
    struct liw_stat st;
    return liw_stat(name, &st) < 0 ? SQLITE_OK : SQLITE_IOERR_DELETE;
}

static int sqlite_vfs_access(sqlite3_vfs *vfs, const char *name,
                             int flags, int *result) {
    (void)vfs; (void)flags;
    struct liw_stat st;
    *result = liw_stat(name, &st) == 0;
    return SQLITE_OK;
}

static int sqlite_vfs_full_pathname(sqlite3_vfs *vfs, const char *name,
                                    int size, char *out) {
    (void)vfs;
    size_t n = strlen(name);
    if ((int)n >= size) return SQLITE_CANTOPEN_FULLPATH;
    memcpy(out, name, n + 1);
    return SQLITE_OK;
}

static void *sqlite_vfs_dlopen(sqlite3_vfs *vfs, const char *name) {
    (void)vfs; (void)name;
    return NULL;
}

static void sqlite_vfs_dlerror(sqlite3_vfs *vfs, int size, char *message) {
    (void)vfs;
    if (size > 0) message[0] = '\0';
}

static void (*sqlite_vfs_dlsym(sqlite3_vfs *vfs, void *handle,
                               const char *symbol))(void) {
    (void)vfs; (void)handle; (void)symbol;
    return NULL;
}

static void sqlite_vfs_dlclose(sqlite3_vfs *vfs, void *handle) {
    (void)vfs; (void)handle;
}

static int sqlite_vfs_randomness(sqlite3_vfs *vfs, int size, char *out) {
    (void)vfs;
    uint64_t state = liw_timer_ticks() ^ (uint64_t)(uintptr_t)out;
    for (int i = 0; i < size; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        out[i] = (char)(state >> 32);
    }
    return size;
}

static int sqlite_vfs_sleep(sqlite3_vfs *vfs, int microseconds) {
    (void)vfs;
    if (microseconds >= 1000) liw_sleep_ms((uint32_t)(microseconds / 1000));
    return microseconds;
}

static int sqlite_vfs_current_time(sqlite3_vfs *vfs, double *julian) {
    (void)vfs;
    struct liw_timeval now;
    if (liw_gettimeofday(&now, NULL) < 0) return SQLITE_ERROR;
    *julian = 2440587.5 + ((double)now.tv_sec + (double)now.tv_usec / 1000000.0) / 86400.0;
    return SQLITE_OK;
}

static int sqlite_vfs_last_error(sqlite3_vfs *vfs, int size, char *message) {
    (void)vfs;
    if (size > 0) message[0] = '\0';
    return 0;
}

static sqlite3_vfs liw_sqlite_vfs = {
    1,
    sizeof(liw_sqlite_file),
    255,
    NULL,
    "liwus",
    NULL,
    sqlite_vfs_open,
    sqlite_vfs_delete,
    sqlite_vfs_access,
    sqlite_vfs_full_pathname,
    sqlite_vfs_dlopen,
    sqlite_vfs_dlerror,
    sqlite_vfs_dlsym,
    sqlite_vfs_dlclose,
    sqlite_vfs_randomness,
    sqlite_vfs_sleep,
    sqlite_vfs_current_time,
    sqlite_vfs_last_error,
    NULL,
    NULL,
    NULL
};

int sqlite3_os_init(void) {
    return sqlite3_vfs_register(&liw_sqlite_vfs, 1);
}

int sqlite3_os_end(void) {
    return SQLITE_OK;
}
