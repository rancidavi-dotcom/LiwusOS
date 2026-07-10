#ifndef _ARPA_INET_H_
#define _ARPA_INET_H_

#include <sys/types.h>
#include <netinet/in.h>

in_addr_t inet_addr(const char *);
int inet_aton(const char *, struct in_addr *);
char *inet_ntoa(struct in_addr);
const char *inet_ntop(int, const void *, char *, unsigned);
int inet_pton(int, const char *, void *);

#endif
