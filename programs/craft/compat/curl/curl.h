#ifndef LIWUS_CRAFT_CURL_H
#define LIWUS_CRAFT_CURL_H
#define CURL_GLOBAL_DEFAULT 0
static inline int curl_global_init(long flags) { (void)flags; return 0; }
static inline void curl_global_cleanup(void) {}
#endif
