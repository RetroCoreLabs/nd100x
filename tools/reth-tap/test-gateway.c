/*
 * test-gateway.c - drive reth-tap's real code against the REAL gateway.
 *
 * test-framing.c proves the byte handling in isolation. This proves the part
 * that isolation cannot: that reth_connect() actually joins a live gateway
 * ethernet segment, and that a frame pushed in at the TAP end comes out at
 * another member of that segment - through the gateway's own repeat logic,
 * not a stand-in for it.
 *
 * Everything except the literal TUNSETIFF is exercised. The TAP fd is a
 * datagram socketpair, which has the property the code depends on: one read is
 * one frame. Creating a real TAP needs root and changes host networking, so it
 * is deliberately NOT done here - see the note in reth-tap.c.
 *
 * The gateway is started by test-gateway.sh on ports nothing else uses, with a
 * temp config, so the repository's own gateway.conf.json is left alone.
 *
 * ASCII only.
 */

#define main reth_tap_main
#include "reth-tap.c"
#undef main

#include <assert.h>

static int g_fail;

static void check(int cond, const char *what)
{
    printf("%-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond)
    {
        g_fail = 1;
    }
}

/* A plain RETH member, written the way any other client would be - so that
 * what it proves is about the protocol and not about shared code. */
static int join_plain(const char *host, int port)
{
    struct sockaddr_in a;
    unsigned char hello[RETH_HANDSHAKE_LEN], peer[RETH_HANDSHAKE_LEN];
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return -1;
    }

    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = inet_addr(host);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0)
    {
        close(fd);
        return -1;
    }

    memcpy(hello, "RETH", 4);
    hello[4] = RETH_VERSION_MEMBER;
    if (write_all(fd, hello, sizeof hello) != 0)
    {
        close(fd);
        return -1;
    }
    if (read_exact(fd, peer, sizeof peer) != 0)
    {
        close(fd);
        return -1;
    }
    if (memcmp(peer, "RETH", 4) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

/* Read one length-prefixed frame, giving up after `ms`. */
static int recv_frame(int fd, unsigned char *out, int cap, int ms)
{
    unsigned char hdr[2];
    struct pollfd p;
    unsigned len;

    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    if (poll(&p, 1, ms) <= 0)
    {
        return -1;
    }
    if (read_exact(fd, hdr, 2) != 0)
    {
        return -1;
    }
    len = ((unsigned)hdr[0] << 8) | hdr[1];
    if (len == 0 || (int)len > cap)
    {
        return -1;
    }
    if (read_exact(fd, out, len) != 0)
    {
        return -1;
    }
    return (int)len;
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int port = (argc > 1) ? atoi(argv[1]) : 39400;
    int bridge, other, tap[2];
    unsigned char frame[64], got[RETH_MAX_FRAME];
    unsigned char rx[2 * (2 + RETH_MAX_FRAME)];
    size_t rxlen = 0;
    int n, i;

    signal(SIGPIPE, SIG_IGN);
    for (i = 0; i < (int)sizeof frame; i++)
    {
        frame[i] = (unsigned char)(0x40 + i);
    }

    /* The bridge's OWN connect path, not a copy of it. */
    bridge = reth_connect(host, port);
    check(bridge >= 0, "reth_connect: the bridge joins a live gateway segment");
    if (bridge < 0)
    {
        printf("\nFAILURES\n");
        return 1;
    }

    other = join_plain(host, port);
    check(other >= 0, "a second plain RETH member joins the same segment");
    if (other < 0)
    {
        printf("\nFAILURES\n");
        return 1;
    }

    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, tap) == 0);

    /* host -> segment: in at the TAP end, out at the other member.
     *
     * Retried until it arrives rather than sent once and waited on. The
     * gateway writes its hello the moment a socket connects, but only ADDS the
     * member once it has read the member's hello - so join_plain() can return
     * before the segment knows about it, and a frame sent in that window is
     * repeated to nobody. That is correct gateway behaviour, not a fault, and
     * a test that ignores it passes on a quiet machine and fails on a busy
     * one. Duplicates do not matter here; arrival does. */
    {
        int attempt;
        n = -1;
        for (attempt = 0; attempt < 20 && n < 0; attempt++)
        {
            assert(write(tap[1], frame, sizeof frame) == (ssize_t)sizeof frame);
            if (pump_tap_to_wire(tap[0], bridge) != 0)
            {
                break;
            }
            n = recv_frame(other, got, sizeof got, 100);
        }
        check(attempt < 20, "host->segment: the bridge sends the frame on");
    }
    check(n == (int)sizeof frame, "host->segment: it reaches the other member");
    check(n > 0 && memcmp(got, frame, sizeof frame) == 0,
          "host->segment: byte for byte, through the real gateway");

    /* A frame is never echoed to its sender - the gateway's rule, and the one
     * that stops a bridge from looping the host's own traffic back at it. */
    check(recv_frame(bridge, got, sizeof got, 300) < 0,
          "host->segment: the bridge does NOT get its own frame back");

    /* segment -> host: in at the other member, out at the TAP end. */
    {
        unsigned char msg[2 + 40];
        msg[0] = 0;
        msg[1] = 40;
        for (i = 0; i < 40; i++)
        {
            msg[2 + i] = (unsigned char)(0x90 + i);
        }
        assert(write_all(other, msg, sizeof msg) == 0);

        /* Wait for the gateway to repeat it before pumping. */
        {
            struct pollfd p;
            p.fd = bridge;
            p.events = POLLIN;
            p.revents = 0;
            check(poll(&p, 1, 2000) > 0, "segment->host: the gateway repeats it to the bridge");
        }
        check(pump_wire_to_tap(bridge, tap[0], rx, &rxlen, sizeof rx) == 0,
              "segment->host: the bridge accepts it");
        n = (int)read(tap[1], got, sizeof got);
        check(n == 40, "segment->host: 40 bytes reach the TAP end");
        check(n == 40 && got[0] == 0x90 && got[39] == (unsigned char)(0x90 + 39),
              "segment->host: byte for byte");
    }

    close(tap[0]);
    close(tap[1]);
    close(bridge);
    close(other);
    printf("\n%s\n", g_fail ? "FAILURES" : "all checks passed");
    return g_fail;
}
