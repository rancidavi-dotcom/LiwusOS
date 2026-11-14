#include "liwus/stdio.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/syscall.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint64_t ticks = liw_timer_ticks();
    uint32_t secs = (uint32_t)(ticks / 100ULL);
    uint32_t mins = secs / 60U;
    uint32_t hrs  = mins / 60U;

    liw_net_info_t net;
    memset(&net, 0, sizeof(net));
    long net_ok = liw_net_info(&net);

    char cwd[128];
    if (liw_getcwd(cwd, sizeof(cwd)) < 0) {
        strcpy(cwd, "/");
    }

    char os_ver[32];
    strcpy(os_ver, "2.0.0");
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp) {
        char buf[128];
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        fclose(fp);
        char *v = strstr(buf, "VERSION=");
        if (v) {
            v += 8;
            size_t i = 0;
            while (v[i] && v[i] != '\n' && v[i] != '\r' && i < sizeof(os_ver) - 1) {
                os_ver[i] = v[i];
                i++;
            }
            os_ver[i] = '\0';
        }
    }

    printf("\n");
    printf("      /\\          root@liwusos\n");
    printf("     /  \\         ------------\n");
    printf("    / /\\ \\        OS: LiwusOS v%s x86_64\n", os_ver);
    printf("   / /  \\ \\       Kernel: Liwus Monolithic 64-bit (SMP/Ring-3)\n");
    printf("  / /    \\ \\      Uptime: %uh %um %us (%lu ticks)\n",
           hrs, mins % 60U, secs % 60U, (unsigned long)ticks);
    printf(" / /  /\\  \\ \\     Packages: lpkg (GitHub HTTPS OTA)\n");
    printf("/_/  /__\\  \\_\\    Shell: Konsole / liwsh (PID %d)\n", liw_getpid());
    printf("                  DE: KDE Plasma (Breeze Dark LVGL 8.3)\n");
    printf("                  FS: SDFS v2 Journaled (cwd: %s)\n", cwd);
    printf("                  Compiler: TinyCC (tcc) 0.9.27 x86_64\n");
    if (net_ok == 0 && net.link_up) {
        printf("                  Network: %s (GW %s, MAC %s)\n",
               net.ip[0] ? net.ip : "DHCP",
               net.gateway[0] ? net.gateway : "-",
               net.mac);
        printf("                  Traffic: RX %u pkts / TX %u pkts\n",
               net.packets_rx, net.packets_tx);
    } else {
        printf("                  Network: Offline\n");
    }
    printf("\n");
    return 0;
}
