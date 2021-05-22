#include "opts.h"

#include <slirp/libslirp.h>

#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <arpa/inet.h>

#include <sys/time.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>

#define MAX_FDS_COUNT 1024

struct poll_fds {
    struct pollfd fds[MAX_FDS_COUNT];
    size_t fds_count;
};

struct timer {
    int64_t when;
    SlirpTimerCb cb;
    void *cb_opaque;

    struct timer* next;
};

struct slirp_ctx {
    struct poll_fds fds;
    struct timer* timers;
    SlirpConfig cfg;

    Slirp* slirp;
};

// Both callbacks return 0 if everything is OK, and -1 on error or if we need to stop
typedef int (*before_poll_callback)(Slirp* slirp, struct slirp_ctx* ctx, void *opaque);
typedef int (*after_poll_callback)(Slirp* slirp, struct slirp_ctx* ctx, void *opaque);

#define SLIRP() struct slirp_ctx* slirp = (struct slirp_ctx*)opaque

void poll_fds_init(struct poll_fds* fds) {
    fds->fds_count = 0;
}

int poll_fds_add(struct poll_fds *fds, int fd, short events) {
    if (fds->fds_count >= MAX_FDS_COUNT)
        return -1;
    fds->fds[fds->fds_count].fd = fd;
    fds->fds[fds->fds_count].events = events;
    fds->fds[fds->fds_count].revents = 0;

    return fds->fds_count++;
}

int poll_fds_poll(struct poll_fds* fds, int timeout) {
    return poll(fds->fds, fds->fds_count, timeout);
}

static int slirp_add_poll_cb(int fd, int mask, void *opaque) {
    struct poll_fds* fds = (struct poll_fds*)opaque;
    short events = 0;

    if (mask & SLIRP_POLL_IN) {
        events |= POLLIN;
    }

    if (mask & SLIRP_POLL_OUT) {
        events |= POLLOUT;
    }

    if (mask & SLIRP_POLL_PRI) {
        events |= POLLPRI;
    }

    if (mask & SLIRP_POLL_HUP) {
        events |= POLLHUP;
    }

    return poll_fds_add(fds, fd, events);
}

static int slirp_get_revents_cb(int idx, void *opaque) {
    struct poll_fds* fds = (struct poll_fds*)opaque;
    int res = 0;
    short mask; 

    if ((size_t)idx >= fds->fds_count) {
        return 0;
    }

    mask = fds->fds[idx].revents;

    if (mask & POLLIN) {
        res |= SLIRP_POLL_IN;
    }

    if (mask & POLLOUT) {
        res |= SLIRP_POLL_OUT;
    }

    if (mask & POLLPRI) {
        res |= SLIRP_POLL_PRI;
    }

    if (mask & POLLHUP) {
        res |= SLIRP_POLL_HUP;
    }

    if (mask & POLLERR) {
        res |= SLIRP_POLL_ERR;
    }

    return res;
}

static ssize_t slirp_send_packet_cb(const void* buf, size_t len, void *opaque) {
    SLIRP();
	(void)slirp;
	(void)buf;

    return len;
}

static void slirp_guest_error_cb(const char* msg, void *opaque) {
    (void)opaque;
    fprintf(stderr, "SLIRP ERROR: %s\n", msg);
}

int64_t time_start = 0;
int64_t curtime_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t res = (tv.tv_sec - time_start) * 1000;
    return res + tv.tv_usec;
}

static int64_t slirp_clock_get_ns_cb(void *opaque) {
    (void)opaque;
    return curtime_ms() * 1000000;
}

struct timer* timer_add(struct timer** timers, SlirpTimerCb cb, void *cb_opaque) {
    struct timer *t = (struct timer*)malloc(sizeof(struct timer));
    if (!t) {
        return NULL;
    }

    t->when = 0;
    t->cb = cb;
    t->cb_opaque = cb_opaque;
    t->next = *timers;
    *timers = t;

    return t;
}

void timer_free(struct timer** timers, struct timer* t) {
    if (!t)
        return;

    if (*timers == t) {
        *timers = t->next;
        free(t);
    } else {
        struct timer* p;
        for (p = *timers; p; p = p->next) {
            if (p->next == t) {
                p->next = t->next;
                free(t);
                return;
            }
        }
    }
}

void timers_free(struct timer** timers) {
    struct timer *p, *t = NULL;

    if (!timers) {
        return;
    }

    for (p = *timers; p; ) {
        t = p;
        p = p->next;

        free(t);
    }

    *timers = NULL;
}

void timers_check(struct timer* timers, int64_t time) {
    struct timer* t;
    for (t = timers; t; t = t->next) {
        if (t->when != 0 && t->when <= time) {
            t->cb(t->cb_opaque);
            t->when = 0;
        }
    }
}

static void* slirp_timer_new_cb(SlirpTimerCb cb, void *cb_opaque, void *opaque) {
    SLIRP();

    return timer_add(&slirp->timers, cb, cb_opaque);
}

static void slirp_timer_free_cb(void *timer, void *opaque) {
    SLIRP();

    timer_free(&slirp->timers, (struct timer*)timer);
}

static void slirp_timer_mod_cb(void *timer, int64_t expire_time, void *opaque) {
    (void)opaque;
    expire_time /= 1000000;
    if (!expire_time) ++expire_time;

    ((struct timer*)timer)->when = expire_time;
}

static void slirp_register_poll_fd_cb(int fd, void *opaque) {
    (void)opaque;
    fprintf(stderr, "Register poll fd: %d\n", fd);
}

static void slirp_unregister_poll_fd_cb(int fd, void *opaque) {
    (void)opaque;
    fprintf(stderr, "Unregister poll fd: %d\n", fd);
}

static void slirp_notify_cb(void *opaque) {
    (void)opaque;
    fprintf(stderr, "Notify\n");
}

static SlirpCb slirp_callbacks = {
    .send_packet = slirp_send_packet_cb,
    .guest_error = slirp_guest_error_cb,
    .clock_get_ns = slirp_clock_get_ns_cb,
    .timer_new = slirp_timer_new_cb,
    .timer_free = slirp_timer_free_cb,
    .timer_mod = slirp_timer_mod_cb,
    .register_poll_fd = slirp_register_poll_fd_cb,
    .unregister_poll_fd = slirp_unregister_poll_fd_cb,
    .notify = slirp_notify_cb,
};

void slirp_ctx_init(struct slirp_ctx* ctx) {
    memset(ctx, 0, sizeof(*ctx));

    poll_fds_init(&ctx->fds);
    ctx->cfg.vnetwork.s_addr = htonl(0x0a000200); /* 10.0.2.0 */
    ctx->cfg.vnetmask.s_addr = htonl(0xffffff00); /* 255.255.255.0 */
    ctx->cfg.vhost.s_addr = htonl(0x0a000202); /* 10.0.2.2 */
    ctx->cfg.vdhcp_start.s_addr = htonl(0x0a00020f); /* 10.0.2.15 */
    ctx->cfg.vnameserver.s_addr = htonl(0x0a000203); /* 10.0.2.3 */
    ctx->cfg.in_enabled = 1;
//    ctx->cfg.enable_emu = 1;
    ctx->cfg.version = 1;
}

int slirp_run(struct slirp_ctx* ctx, before_poll_callback before_poll, after_poll_callback after_poll, void *opaque) {
    Slirp* slirp = slirp_new(&ctx->cfg, &slirp_callbacks, ctx);

    for (;;) {
        uint32_t timeout = 1;
        int rv;
        poll_fds_init(&ctx->fds);

        // Process timers:
        timers_check(ctx->timers, curtime_ms());

        // Call user callback:
        if (before_poll(slirp, ctx, opaque))
            break;

        // Fill slirp fds:
        slirp_pollfds_fill(slirp, &timeout, slirp_add_poll_cb, &ctx->fds);

        // Actual poll:
        rv = poll_fds_poll(&ctx->fds, 1);
        if (rv < 0) {
            fprintf(stderr, "Error: poll failed.\n");
            slirp_cleanup(slirp);
            break;
        }

        // Allow slirp to process events:
        slirp_pollfds_poll(slirp, rv <= 0, slirp_get_revents_cb, &ctx->fds);

        // Call user post actions:
        if (after_poll(slirp, ctx, opaque))
            break;
    }

    return 0;
}

int create_packet_socket(const char* iface) {
    // int sock = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    struct ifreq req;
    struct packet_mreq mreq;
    int action = PACKET_ADD_MEMBERSHIP;
    if (sock < 0) {
        return -1;
    }

    // Request interface index:
    if (strlen(iface) + 1 > sizeof(req.ifr_name)) {
        close(sock);
        fprintf(stderr, "Interface name is too long\n");
        return -1;
    }

    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, iface);
    if (ioctl(sock, SIOCGIFINDEX, &req) < 0) {
        close(sock);
        fprintf(stderr, "ioctl failed: %s\n", strerror(errno));
        return -1;
    }

    // Fill sockaddr and bind:
    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));

    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = req.ifr_ifindex;
    addr.sll_halen = sizeof(addr);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        fprintf(stderr, "bind failed: %s\n", strerror(errno));
        return -1;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.mr_ifindex = req.ifr_ifindex;
    mreq.mr_type = PACKET_MR_PROMISC;
    if (setsockopt(sock, SOL_PACKET, action, &mreq, sizeof(mreq)) < 0) {
        fprintf(stderr, "WARNING: putting interface into promiscuous mode failed: %s\n", strerror(errno));
    }

    return sock;
}

static int psocket = -1;
static int psocket_index = -1;
static int before_poll(Slirp* slirp, struct slirp_ctx* ctx, void *opaque) {
    psocket_index = poll_fds_add(&ctx->fds, psocket, POLLIN);
    (void)opaque;
    (void)slirp;
	return 0;
}

static int after_poll(Slirp* slirp, struct slirp_ctx* ctx, void *opaque) {
    (void)opaque;
    if (psocket_index < 0)
        return 0;

    if (ctx->fds.fds[psocket_index].revents & POLLIN) {
        uint8_t buf[8192];

        int len = recv(psocket, buf, sizeof(buf), 0);
        if (len <= 0) {
            fprintf(stderr, "Error: recv failed: %s\n", strerror(errno));
            return -1;
        }

		// fprintf(stderr, "INPUT PACKET: %d\n", len);
        slirp_input(slirp, buf, len);
    }
    psocket_index = -1;

    return 0;
}

ssize_t send_packet(const void* buf, size_t len, void *opaque) {
    (void)opaque;
	// fprintf(stderr, "OUTPUT PACKET: %d\n", (int)len);
    return send(psocket, buf, len, 0);
}

void print_addr(const char* name, const void* addr) {
	char buf[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, addr, buf, sizeof(buf))) {
		printf("%s: %s\n", name, buf);
	}
}

int main(int argc, const char* argv[]) {
    time_start = curtime_ms();
    const char* iface = NULL;
    const char* bootfile = NULL;
    const char* tftp_server = NULL;
	const char* vnetwork = "10.0.2.0";
	const char* vnetmask = "255.255.255.0";
	const char* vhost = NULL;
	const char* vdhcp = NULL;
	const char* vnameserver = NULL;

    OPT_PARSE_START(argc, argv, "-i <iface> ...");
        OPT_STR(iface, "-i", "--interface", "Interface to read and write packets");
        OPT_STR(tftp_server, "-t", "--tftp-path", "Path to TFTP server");
        OPT_STR(bootfile, "-b", "--bootfile", "File to boot from for TFTP");
		OPT_STR(vnetwork, "-n", "--network", "Virtual network (default: 10.0.2.0)");
		OPT_STR(vnetmask, "-m", "--netmask", "Virtual network mask (default: 255.255.255.0)");
		OPT_STR(vhost, "-H", "--host", "Virtual host (default: second IP in the network)");
		OPT_STR(vnameserver, "-d", "--nameserver", "Nameserver in the virtual network (default: 15th host in the network)");
    OPT_PARSE_END();

    if (!iface) {
        fprintf(stderr, "Error: interface name is not specified in command line.\n");
        fprintf(stderr, "Try %s -h for help.\n", argv[0]);
        exit(1);
    }

    psocket = create_packet_socket(iface);
    if (psocket < 0) {
        fprintf(stderr, "Can't create packet socket!\n");
        return 1;
    }

    struct slirp_ctx slirp_ctx;
    slirp_ctx_init(&slirp_ctx);

    slirp_ctx.cfg.bootfile = bootfile;
    slirp_ctx.cfg.tftp_path = tftp_server;
    slirp_ctx.cfg.tftp_server_name = "slirp";
    slirp_callbacks.send_packet = send_packet;

	if (inet_pton(AF_INET, vnetwork, &slirp_ctx.cfg.vnetwork) <= 0) {
		fprintf(stderr, "Invalid address: %s\n", vnetwork);
		return 1;
	}

	if (inet_pton(AF_INET, vnetmask, &slirp_ctx.cfg.vnetmask) <= 0) {
		fprintf(stderr, "Invalid address: %s\n", vnetmask);
		return 1;
	}

	if (vhost) {
		if (inet_pton(AF_INET, vhost, &slirp_ctx.cfg.vhost) <= 0) {
			fprintf(stderr, "Invalid address: %s\n", vhost);
			return 1;
		}
	} else {
		slirp_ctx.cfg.vhost.s_addr = htonl(ntohl(slirp_ctx.cfg.vnetwork.s_addr & slirp_ctx.cfg.vnetmask.s_addr) + 2);
	}

	if (vdhcp) {
		if (inet_pton(AF_INET, vhost, &slirp_ctx.cfg.vdhcp_start) <= 0) {
			fprintf(stderr, "Invalid address: %s\n", vdhcp);
			return 1;
		}
	} else {
		slirp_ctx.cfg.vdhcp_start.s_addr = htonl(ntohl(slirp_ctx.cfg.vnetwork.s_addr & slirp_ctx.cfg.vnetmask.s_addr) + 15);
	}

	if (vnameserver) {
		if (inet_pton(AF_INET, vnameserver, &slirp_ctx.cfg.vnameserver) <= 0) {
			fprintf(stderr, "Invalid address: %s\n", vnameserver);
			return 1;
		}
	} else {
		slirp_ctx.cfg.vnameserver.s_addr = htonl(ntohl(slirp_ctx.cfg.vnetwork.s_addr & slirp_ctx.cfg.vnetmask.s_addr) + 3);
	}

	printf("Configuration:\n");
	print_addr("Network", &slirp_ctx.cfg.vnetwork.s_addr);
	print_addr("Netmask", &slirp_ctx.cfg.vnetmask.s_addr);
	print_addr("VHost", &slirp_ctx.cfg.vhost.s_addr);
	print_addr("DHCP", &slirp_ctx.cfg.vdhcp_start.s_addr);
	print_addr("Nameserver", &slirp_ctx.cfg.vnameserver.s_addr);
	if (tftp_server) {
		printf("TFTP server: %s\n", tftp_server);
	}
	if (bootfile) {
		printf("Boot file: %s\n", bootfile);
	}

    slirp_run(&slirp_ctx, before_poll, after_poll, NULL);
}

