#ifndef LIWUS_CRAFT_TINYCTHREAD_H
#define LIWUS_CRAFT_TINYCTHREAD_H

/* The initial LiwusOS port runs Craft's chunk jobs on the main loop. */
typedef int thrd_t;
typedef int mtx_t;
typedef int cnd_t;
#define mtx_plain 0
#define thrd_success 0
static inline int mtx_init(mtx_t *m, int type) { (void)type; *m = 0; return 0; }
static inline int mtx_lock(mtx_t *m) { (void)m; return 0; }
static inline int mtx_unlock(mtx_t *m) { (void)m; return 0; }
static inline int mtx_destroy(mtx_t *m) { (void)m; return 0; }
static inline int cnd_init(cnd_t *c) { *c = 0; return 0; }
static inline int cnd_signal(cnd_t *c) { (void)c; return 0; }
static inline int cnd_wait(cnd_t *c, mtx_t *m) { (void)c; (void)m; return 0; }
static inline int cnd_destroy(cnd_t *c) { (void)c; return 0; }
static inline int thrd_create(thrd_t *t, int (*fn)(void *), void *arg) {
    (void)t; (void)fn; (void)arg; return -1;
}
static inline int thrd_join(thrd_t t, int *result) {
    (void)t; (void)result; return 0;
}

#endif
