#ifndef _BYTESWAP_H_
#define _BYTESWAP_H_

static inline unsigned short bswap_16(unsigned short x) {
    return (x << 8) | (x >> 8);
}

static inline unsigned int bswap_32(unsigned int x) {
    return ((x & 0xff000000u) >> 24)
         | ((x & 0x00ff0000u) >>  8)
         | ((x & 0x0000ff00u) <<  8)
         | ((x & 0x000000ffu) << 24);
}

static inline unsigned long long bswap_64(unsigned long long x) {
    return ((x & 0xff00000000000000ull) >> 56)
         | ((x & 0x00ff000000000000ull) >> 40)
         | ((x & 0x0000ff0000000000ull) >> 24)
         | ((x & 0x000000ff00000000ull) >>  8)
         | ((x & 0x00000000ff000000ull) <<  8)
         | ((x & 0x0000000000ff0000ull) << 24)
         | ((x & 0x000000000000ff00ull) << 40)
         | ((x & 0x00000000000000ffull) << 56);
}

#endif
