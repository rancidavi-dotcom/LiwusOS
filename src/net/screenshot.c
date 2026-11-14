#include "screenshot.h"
#include "net_manager.h"
#include "task.h"
#include "timer.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include <string.h>

typedef struct {
    struct tcp_pcb *pcb;
    volatile int state; /* 0 connecting, 1 connected, 2 receiver ACK, -1 error */
    volatile uint32_t acknowledged;
} screenshot_tcp_t;

static screenshot_tcp_t transfer;

static err_t screenshot_recv(void *arg, struct tcp_pcb *pcb,
                             struct pbuf *p, err_t err) {
    screenshot_tcp_t *s = (screenshot_tcp_t *)arg;
    (void)err;
    if (!p) {
        if (s->state != 2) s->state = -1;
        return ERR_OK;
    }

    uint8_t response[8] = {0};
    uint16_t copied = pbuf_copy_partial(p, response,
        p->tot_len < sizeof(response) ? p->tot_len : sizeof(response), 0);
    tcp_recved(pcb, p->tot_len);
    if (copied >= 2 && response[0] == 'O' && response[1] == 'K')
        s->state = 2;
    pbuf_free(p);
    return ERR_OK;
}

static err_t screenshot_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    screenshot_tcp_t *s = (screenshot_tcp_t *)arg;
    (void)pcb;
    s->acknowledged += len;
    return ERR_OK;
}

static err_t screenshot_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    screenshot_tcp_t *s = (screenshot_tcp_t *)arg;
    if (err != ERR_OK) {
        s->state = -1;
        return err;
    }
    s->state = 1;
    tcp_recv(pcb, screenshot_recv);
    tcp_sent(pcb, screenshot_sent);
    return ERR_OK;
}

static void screenshot_error(void *arg, err_t err) {
    screenshot_tcp_t *s = (screenshot_tcp_t *)arg;
    (void)err;
    s->pcb = NULL;
    s->state = -1;
}

static bool screenshot_wait(screenshot_tcp_t *s, int required_state,
                            uint32_t timeout_ticks) {
    uint32_t start = timer_ticks;
    while (timer_ticks - start < timeout_ticks) {
        if (s->state < 0 || s->state == required_state) return s->state == required_state;
        net_poll();
        task_sleep_ms(2);
    }
    return s->state == required_state;
}

int net_send_screenshot_bmp(const char *host_ip, uint16_t port,
                            const uint8_t *data, uint32_t size) {
    if (!net_is_active() || !net_has_ip() || !host_ip || !data || size < 54 || !port)
        return -1;
    if (data[0] != 'B' || data[1] != 'M') return -2;

    ip4_addr_t ipv4;
    ip_addr_t address;
    if (!ip4addr_aton(host_ip, &ipv4)) return -3;
    ip_addr_copy_from_ip4(address, ipv4);

    transfer.pcb = NULL;
    transfer.state = 0;
    transfer.acknowledged = 0;

    uint64_t flags = net_lock_acquire();
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        net_lock_release(flags);
        return -4;
    }
    transfer.pcb = pcb;
    tcp_arg(pcb, &transfer);
    tcp_err(pcb, screenshot_error);
    err_t err = tcp_connect(pcb, &address, port, screenshot_connected);
    if (err != ERR_OK) {
        tcp_abort(pcb);
        transfer.pcb = NULL;
        transfer.state = -1;
        net_lock_release(flags);
        return -5;
    }
    net_lock_release(flags);

    if (!screenshot_wait(&transfer, 1, 1500)) goto fail;

    uint32_t queued = 0;
    uint32_t last_progress = timer_ticks;
    while (queued < size) {
        flags = net_lock_acquire();
        pcb = transfer.pcb;
        if (transfer.state < 0 || !pcb) {
            net_lock_release(flags);
            goto fail;
        }

        uint16_t available = tcp_sndbuf(pcb);
        uint16_t amount = available > 1024 ? 1024 : available;
        if (amount > size - queued) amount = (uint16_t)(size - queued);
        err = amount ? tcp_write(pcb, data + queued, amount, TCP_WRITE_FLAG_COPY) : ERR_MEM;
        if (err == ERR_OK) {
            queued += amount;
            tcp_output(pcb);
        }
        net_lock_release(flags);

        if (err == ERR_OK) {
            last_progress = timer_ticks;
        } else if (err != ERR_MEM) {
            goto fail;
        }
        if (timer_ticks - last_progress >= 6000) goto fail;
        net_poll();
        task_sleep_ms(2);
    }

    if (!screenshot_wait(&transfer, 2, 6000)) goto fail;
    flags = net_lock_acquire();
    pcb = transfer.pcb;
    if (pcb) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_err(pcb, NULL);
        err = tcp_close(pcb);
        if (err == ERR_OK) transfer.pcb = NULL;
    } else {
        err = ERR_CONN;
    }
    net_lock_release(flags);
    if (err != ERR_OK) goto fail;
    return 0;

fail:
    flags = net_lock_acquire();
    pcb = transfer.pcb;
    if (pcb) {
        transfer.pcb = NULL;
        tcp_abort(pcb);
    }
    transfer.state = -1;
    net_lock_release(flags);
    return -6;
}
