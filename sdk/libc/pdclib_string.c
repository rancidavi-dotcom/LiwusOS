#include <string.h>

/*
 * LiwusOS userland libc reuses a small, platform-independent subset of PDCLib
 * for pure C string primitives. The OS-specific ABI stays in our own layer.
 */

#include "../../libc/functions/string/memchr.c"
#include "../../libc/functions/string/memcmp.c"
#include "../../libc/functions/string/memcpy.c"
#include "../../libc/functions/string/memmove.c"
#include "../../libc/functions/string/memset.c"
#include "../../libc/functions/string/strcat.c"
#include "../../libc/functions/string/strchr.c"
#include "../../libc/functions/string/strcmp.c"
#include "../../libc/functions/string/strcpy.c"
#include "../../libc/functions/string/strcspn.c"
#include "../../libc/functions/string/strlen.c"
#include "../../libc/functions/string/strncmp.c"
#include "../../libc/functions/string/strncpy.c"
#include "../../libc/functions/string/strrchr.c"
#include "../../libc/functions/string/strspn.c"
#include "../../libc/functions/string/strstr.c"
