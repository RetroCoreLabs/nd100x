/*
 * reth-tap.c - put a gateway ethernet segment on the host network.
 *
 * The gateway (tools/nd100-gateway/gateway.js) opens a RETH server per
 * emulated segment, and everything on that segment sees everything else: a
 * browser NDIX, a native nd500x, another gateway. What was missing was a way
 * for the HOST ITSELF to be on that wire. This is that: a TAP device on one
 * side, an ordinary RETH member on the other, frames copied between them.
 *
 * Neither half is new. nd500x already contains both - uplink_tap.c opens the
 * TAP, uplink_tcp.c speaks RETH - but they live INSIDE the emulator, wired to
 * nd500_xmsg. A browser machine has no such process to borrow them from, so
 * they are re-joined here as a program of their own. The gateway needs no
 * change at all; this joins port 3094 exactly the way a native nd500x does.
 *
 * WHY NOT IN THE GATEWAY: gateway.js is plain Node with no build step, and
 * Node has no binding for /dev/net/tun. Adding one would put a native addon
 * and a compiler into a tool that currently needs neither.
 *
 * WHAT THIS DOES NOT DO: create the TAP device, give it an address, or bring
 * it up. Those are root operations that change host networking, and a program
 * doing them silently is hard to undo and easy not to notice - the same
 * reasoning as nd500x's, whose tools/ndix-tap.sh does it once, on purpose.
 * Run that first; this only ever OPENS a device that already exists.
 *
 * ASCII only - no unicode anywhere in this toolchain.
 *
 * Build:  make
 * Use:    ./reth-tap --dev nd0 --host 127.0.0.1 --port 3094
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <net/if.h>          /* if_nametoindex - MUST precede linux/if.h */
#include <linux/if.h>
#include <linux/if_tun.h>

/* RETH, as the gateway and nd500x both speak it. See docs/GATEWAY-PROTOCOL.md
 * and nd500x's src/cpu/nd500_ethhub.h - these three constants must agree with
 * ETHHUB_HANDSHAKE_LEN / ETHHUB_VERSION_MEMBER / ETHHUB_MAX_FRAME. */
#define RETH_HANDSHAKE_LEN  5
#define RETH_VERSION_MEMBER 1        /* one emulated NIC - what NDIX is */
#define RETH_MAX_FRAME      2048

#define DEFAULT_DEV  "nd0"           /* not tap0: see tools/ndix-tap.sh */
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 3094            /* the ND Ethernet II PCB number */

#define RECONNECT_SECS 2

static volatile sig_atomic_t g_stop;

static unsigned long g_to_wire, g_to_host, g_dropped;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

/* ---- TAP ---------------------------------------------------------------- */

/* Attach to an EXISTING device - see the refusal below for why that is
 * checked rather than assumed. A persistent device made by ndix-tap.sh keeps
 * its address and route across restarts of this bridge, which is the whole
 * point of making it persistent.
 *
 * IFF_TAP, not IFF_TUN: ethernet frames with their MAC headers, which is what
 * a segment carries. IFF_NO_PI so a read returns exactly one frame with no
 * 4-byte packet-info prefix in front of it. */
static int tap_open(const char* dev) {
    struct ifreq ifr;
    int fd, flags;

    /* Refuse a name that is not already an interface.
     *
     * TUNSETIFF happily CREATES a device when the name is free, and on a box
     * where /dev/net/tun is world-writable it may well succeed. That is the
     * worst outcome available: the bridge would attach, join the segment and
     * report frames moving, while the device it made has no address, no route
     * and disappears on exit - so the host is not on the wire and nothing
     * says so. Failing here, with the command that fixes it, is the honest
     * answer. */
    if (if_nametoindex(dev) == 0) {
        fprintf(stderr, "reth-tap: there is no interface called \"%s\".\n"
                        "          Create it once, as root:\n"
                        "              sudo ip tuntap add dev %s mode tap user $USER\n"
                        "              sudo ip addr add 223.255.254.1/24 dev %s\n"
                        "              sudo ip link set %s up\n"
                        "          nd500x ships this as tools/ndix-tap.sh.\n",
                dev, dev, dev, dev);
        return -1;
    }

    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "reth-tap: /dev/net/tun: %s\n", strerror(errno));
        fprintf(stderr, "          (is the tun module loaded?)\n");
        return -1;
    }

    memset(&ifr, 0, sizeof ifr);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", dev);

    if (ioctl(fd, TUNSETIFF, &ifr) < 0) {
        fprintf(stderr, "reth-tap: cannot attach to TAP device \"%s\": %s\n",
                dev, strerror(errno));
        if (errno == EPERM)
            fprintf(stderr,
                    "          The device must exist and be owned by you. Create it once:\n"
                    "              sudo <nd500x>/tools/ndix-tap.sh up\n");
        close(fd);
        return -1;
    }

    /* Non-blocking: poll() is allowed a spurious wake-up, and a blocking read
     * on this fd would then stall the OTHER direction as well - both live in
     * one loop. pump_tap_to_wire treats EAGAIN as "nothing there". */
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        fprintf(stderr, "reth-tap: cannot set %s non-blocking: %s\n",
                dev, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* ---- TCP / RETH --------------------------------------------------------- */

/* Write all of it. A TCP send is allowed to be short, and a half-written
 * length prefix desynchronises the peer permanently - it has no way to tell a
 * truncated frame from the next one. */
static int write_all(int fd, const unsigned char* p, size_t n) {
    while (n > 0) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        p += (size_t)w;
        n -= (size_t)w;
    }
    return 0;
}

static int read_exact(int fd, unsigned char* p, size_t n) {
    while (n > 0) {
        ssize_t r = recv(fd, p, n, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        p += (size_t)r;
        n -= (size_t)r;
    }
    return 0;
}

/* Dial the segment and exchange the hello.
 *
 * The hello is WRITTEN BEFORE the peer's is read, matching the gateway and
 * nd500x. Both ends writing first is what makes two peers that connect at the
 * same instant unable to deadlock on each other. */
static int reth_connect(const char* host, int port) {
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    unsigned char hello[RETH_HANDSHAKE_LEN], peer[RETH_HANDSHAKE_LEN];
    int fd = -1, one = 1, rc;

    snprintf(portstr, sizeof portstr, "%d", port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "reth-tap: %s:%d: %s\n", host, port, gai_strerror(rc));
        return -1;
    }
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return -1;

    /* Nagle would batch small frames together. They are already framed, so it
     * costs latency and buys nothing - the gateway sets NoDelay for the same
     * reason on its side. */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    memcpy(hello, "RETH", 4);
    hello[4] = RETH_VERSION_MEMBER;
    if (write_all(fd, hello, sizeof hello) != 0) { close(fd); return -1; }

    if (read_exact(fd, peer, sizeof peer) != 0) {
        fprintf(stderr, "reth-tap: %s:%d closed before saying hello\n", host, port);
        close(fd);
        return -1;
    }
    if (memcmp(peer, "RETH", 4) != 0) {
        fprintf(stderr, "reth-tap: %s:%d answered, but not with a RETH handshake "
                        "- is that really an ethernet segment?\n", host, port);
        close(fd);
        return -1;
    }

    fprintf(stderr, "reth-tap: joined %s:%d (peer protocol version %u)\n",
            host, port, peer[4]);
    return fd;
}

/* ---- the two directions ------------------------------------------------- */

/* host -> segment. A TAP read returns exactly one frame, so there is no
 * framing to undo here, only the length prefix to add. */
static int pump_tap_to_wire(int tapfd, int sock) {
    unsigned char buf[2 + RETH_MAX_FRAME];
    ssize_t n;

    do {
        n = read(tapfd, buf + 2, RETH_MAX_FRAME);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        fprintf(stderr, "reth-tap: TAP read: %s\n", strerror(errno));
        return -1;
    }
    if (n == 0) return 0;
    if (n > RETH_MAX_FRAME) {          /* cannot happen with the read above */
        g_dropped++;
        return 0;
    }

    buf[0] = (unsigned char)(((unsigned)n >> 8) & 0xFF);
    buf[1] = (unsigned char)((unsigned)n & 0xFF);
    if (write_all(sock, buf, (size_t)n + 2) != 0) return -1;
    g_to_wire++;
    return 0;
}

/* segment -> host. The TCP side is a byte stream: one read can carry half a
 * frame, or three and a half. Everything not yet whole stays in `rx` for the
 * next go round. */
static int pump_wire_to_tap(int sock, int tapfd,
                            unsigned char* rx, size_t* rxlen, size_t rxcap) {
    ssize_t n;
    size_t used = 0;

    do {
        n = recv(sock, rx + *rxlen, rxcap - *rxlen, 0);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    if (n == 0) return -1;             /* peer went away */
    *rxlen += (size_t)n;

    for (;;) {
        size_t avail = *rxlen - used;
        unsigned len;
        if (avail < 2) break;
        len = ((unsigned)rx[used] << 8) | rx[used + 1];

        /* A length-prefixed stream cannot be resynchronised once it is out of
         * step: there is no marker to hunt for. The gateway drops the
         * connection on this and so does this - forwarding rubbish onto the
         * host's network is worse than reconnecting. */
        if (len == 0 || len > RETH_MAX_FRAME) {
            fprintf(stderr, "reth-tap: bad frame length %u from the segment "
                            "- dropping the connection\n", len);
            return -1;
        }
        if (avail < 2 + len) break;    /* rest of it has not arrived */

        {
            ssize_t w;
            do {
                w = write(tapfd, rx + used + 2, len);
            } while (w < 0 && errno == EINTR);
            if (w < 0) {
                /* A TAP write fails when nothing has the device up. That is a
                 * host-side condition, not a reason to leave the segment. */
                g_dropped++;
            } else {
                g_to_host++;
            }
        }
        used += 2 + len;
    }

    if (used > 0) {
        memmove(rx, rx + used, *rxlen - used);
        *rxlen -= used;
    }
    return 0;
}

/* ---- main --------------------------------------------------------------- */

static void usage(const char* argv0) {
    fprintf(stderr,
        "reth-tap - bridge a gateway ethernet segment onto a host TAP device\n"
        "\n"
        "usage: %s [--dev NAME] [--host HOST] [--port PORT] [--quiet]\n"
        "\n"
        "  --dev NAME    TAP device to attach to (default %s). It must already\n"
        "                exist; create it once with nd500x's tools/ndix-tap.sh\n"
        "  --host HOST   gateway address (default %s)\n"
        "  --port PORT   ethernet segment port (default %d)\n"
        "  --quiet       do not print the periodic frame counters\n"
        "\n"
        "The gateway needs no configuration for this: any RETH client may join\n"
        "a segment. Frames are repeated to every other member, so the host, a\n"
        "browser NDIX and a native nd500x all land on one wire.\n",
        argv0, DEFAULT_DEV, DEFAULT_HOST, DEFAULT_PORT);
}

/* Parse the command line into *dev, *host, *port, *quiet. Returns -1 to
 * carry on, or the exit code for main (0 after --help, 2 on a bad argument). */
static int parse_args(int argc, char **argv, const char **dev, const char **host, int *port,
                      int *quiet)
{
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dev") && i + 1 < argc)
        {
            *dev = argv[++i];
        }
        else if (!strcmp(argv[i], "--host") && i + 1 < argc)
        {
            *host = argv[++i];
        }
        else if (!strcmp(argv[i], "--port") && i + 1 < argc)
        {
            *port = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--quiet"))
        {
            *quiet = 1;
        }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "reth-tap: unknown argument \"%s\"\n\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    const char *dev = DEFAULT_DEV;
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    int quiet = 0;
    int tapfd, sock = -1, i;
    unsigned char rx[2 * (2 + RETH_MAX_FRAME)];
    size_t rxlen = 0;
    unsigned long last_reported = ~0UL;

    i = parse_args(argc, argv, &dev, &host, &port, &quiet);
    if (i >= 0)
    {
        return i;
    }

    /* SIGPIPE would kill the process on a write to a gateway that has just
     * gone away, which is the one thing this must survive. Every send already
     * uses MSG_NOSIGNAL; this covers the TAP writes too. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    tapfd = tap_open(dev);
    if (tapfd < 0) return 1;
    fprintf(stderr, "reth-tap: attached to TAP device \"%s\"\n", dev);

    while (!g_stop) {
        struct pollfd fds[2];
        int rc;

        if (sock < 0) {
            sock = reth_connect(host, port);
            if (sock < 0) {
                if (g_stop) break;
                fprintf(stderr, "reth-tap: no segment at %s:%d - retrying in %ds\n",
                        host, port, RECONNECT_SECS);
                sleep(RECONNECT_SECS);
                continue;
            }
            rxlen = 0;                 /* a new connection starts a new stream */
        }

        fds[0].fd = tapfd; fds[0].events = POLLIN; fds[0].revents = 0;
        fds[1].fd = sock;  fds[1].events = POLLIN; fds[1].revents = 0;

        rc = poll(fds, 2, 1000);
        if (rc < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "reth-tap: poll: %s\n", strerror(errno));
            break;
        }

        if (rc > 0) {
            if (fds[0].revents & POLLIN) {
                if (pump_tap_to_wire(tapfd, sock) != 0) {
                    close(sock);
                    sock = -1;
                    fprintf(stderr, "reth-tap: lost the segment - reconnecting\n");
                    continue;
                }
            }
            if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (pump_wire_to_tap(sock, tapfd, rx, &rxlen, sizeof rx) != 0) {
                    close(sock);
                    sock = -1;
                    fprintf(stderr, "reth-tap: lost the segment - reconnecting\n");
                    continue;
                }
            }
        }

        /* Counters only when they have changed. A bridge that prints nothing
         * looks identical to a bridge that is not running, and a bridge that
         * prints every second buries the line that matters. */
        if (!quiet && (g_to_wire + g_to_host) != last_reported && rc == 0) {
            last_reported = g_to_wire + g_to_host;
            fprintf(stderr, "reth-tap: host->segment %lu, segment->host %lu, dropped %lu\n",
                    g_to_wire, g_to_host, g_dropped);
        }
    }

    if (sock >= 0) close(sock);
    close(tapfd);
    fprintf(stderr, "\nreth-tap: stopped. host->segment %lu, segment->host %lu, dropped %lu\n",
            g_to_wire, g_to_host, g_dropped);
    return 0;
}
