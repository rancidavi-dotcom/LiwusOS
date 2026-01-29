#ifndef _NETDB_H_
#define _NETDB_H_

#include <sys/types.h>
#include <stdint.h>

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

struct servent {
    char *s_name;
    char **s_aliases;
    int s_port;
    char *s_proto;
};

struct protoent {
    char *p_name;
    char **p_aliases;
    int p_proto;
};

#define h_addr h_addr_list[0]

struct sockaddr;

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    unsigned ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

#define NI_MAXHOST 1025
#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define NI_NOFQDN 4
#define NI_NAMEREQD 8
#define NI_DGRAM 16
#define AI_PASSIVE 1
#define AI_CANONNAME 2
#define AI_NUMERICHOST 4
#define AI_NUMERICSERV 8
#define AI_V4MAPPED 8
#define AI_ALL 16
#define AI_ADDRCONFIG 32

struct hostent *gethostbyname(const char *);
struct hostent *gethostbyaddr(const void *, int, int);
struct servent *getservbyname(const char *, const char *);
struct servent *getservbyport(int, const char *);
struct protoent *getprotobyname(const char *);
struct protoent *getprotobynumber(int);
int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void freeaddrinfo(struct addrinfo *);
int getnameinfo(const struct sockaddr *, unsigned, char *, unsigned, char *, unsigned, int);

extern int h_errno;
const char *hstrerror(int err);

#endif
