#ifndef CONFIG_H
#define CONFIG_H

#define TCC_VERSION "0.9.28-liwus"

#define TCC_TARGET_X86_64 1
#define TARGETOS_LIWUS 1
#define ONE_SOURCE 1
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_PREDEFS 1
#define CONFIG_TCC_SEMLOCK 0
#define CONFIG_TCC_BACKTRACE 0

#define CONFIG_TCCDIR "/sys/lib/tcc"
#define CONFIG_TCC_SYSINCLUDEPATHS "{B}/include:/sys/lib/tcc/include:/sys/include:/include"
#define CONFIG_TCC_LIBPATHS "{B}:/sys/lib:/lib:/sys/lib/tcc"
#define CONFIG_TCC_CRTPREFIX "/sys/lib:/lib:{B}:/sys/lib/tcc"
#define CONFIG_TCC_ELFINTERP ""
#define TCC_LIBTCC1 ""

#endif
