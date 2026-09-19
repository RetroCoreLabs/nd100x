/*
 * test-framing.c - exercise reth-tap's wire handling without a TAP device.
 *
 * The TAP half of this bridge needs root to set up, but the part most likely
 * to be WRONG is the framing: a length-prefixed stream where one read can
 * carry half a frame or three and a half. That part is pure fd work, so it is
 * driven here over socketpairs instead - no privilege, no gateway, no network.
 *
 * reth-tap.c is #included rather than linked because its pumps are static, and
 * making them extern purely to be tested would change the program to suit the
 * test. `main` is renamed instead, which changes nothing about the code under
 * test.
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

/* A pair where [0] is what the code under test uses and [1] is the far end.
 *
 * The TAP side is a DATAGRAM pair, not a stream one. A real TAP preserves
 * frame boundaries - one read is one frame - and that is the property the code
 * relies on. Over a stream pair two consecutive 10 and 20 byte writes arrive
 * as a single 30-byte read, which is not what a TAP ever does and would make
 * the test assert things about the harness rather than about the bridge.
 * The segment side stays a STREAM pair, because that is exactly what TCP is. */
static void pair_tap(int fd[2])
{
    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, fd) == 0);
}
static void pair_sock(int fd[2])
{
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
}

int main(void)
{
    int tap[2];
    int sock[2];
    unsigned char buf[4096];
    unsigned char rx[2 * (2 + RETH_MAX_FRAME)];
    size_t rxlen;
    ssize_t n;
    int i;

    /* ---- host -> segment: the length prefix ---- */
    pair_tap(tap);
    pair_sock(sock);
    {
        unsigned char frame[60];
        for (i = 0; i < 60; i++)
        {
            frame[i] = (unsigned char)i;
        }
        assert(write(tap[1], frame, sizeof frame) == 60);

        check(pump_tap_to_wire(tap[0], sock[0]) == 0, "tap->wire: a 60-byte frame is accepted");

        n = recv(sock[1], buf, sizeof buf, 0);
        check(n == 62, "tap->wire: 60 bytes go out as 2 + 60");
        check(buf[0] == 0 && buf[1] == 60, "tap->wire: length prefix is big-endian");
        check(memcmp(buf + 2, frame, 60) == 0, "tap->wire: the frame is unchanged");
    }
    close(tap[0]);
    close(tap[1]);
    close(sock[0]);
    close(sock[1]);

    /* ---- segment -> host: two whole frames arriving in ONE read ---- */
    pair_tap(tap);
    pair_sock(sock);
    rxlen = 0;
    {
        unsigned char msg[2 + 10 + 2 + 20];
        memset(msg, 0xAA, sizeof msg);
        msg[0] = 0;
        msg[1] = 10;
        msg[2 + 10] = 0;
        msg[2 + 10 + 1] = 20;
        assert(write(sock[1], msg, sizeof msg) == (ssize_t)sizeof msg);

        check(pump_wire_to_tap(sock[0], tap[0], rx, &rxlen, sizeof rx) == 0,
              "wire->tap: two frames in one read are accepted");
        check(rxlen == 0, "wire->tap: nothing is left over");

        n = read(tap[1], buf, sizeof buf);
        check(n == 10, "wire->tap: the first frame is delivered whole");
        n = read(tap[1], buf, sizeof buf);
        check(n == 20, "wire->tap: the second frame is delivered too");
    }
    close(tap[0]);
    close(tap[1]);
    close(sock[0]);
    close(sock[1]);

    /* ---- segment -> host: ONE frame split across two reads ----
     * This is the case that a naive implementation gets wrong, and it is not
     * rare: TCP splits wherever it likes. */
    pair_tap(tap);
    pair_sock(sock);
    rxlen = 0;
    {
        uint8_t head[2 + 4];
        uint8_t tail[6];
        head[0] = 0;
        head[1] = 10;
        memset(head + 2, 0x11, 4);
        memset(tail, 0x22, 6);

        assert(write(sock[1], head, sizeof head) == (ssize_t)sizeof head);
        check(pump_wire_to_tap(sock[0], tap[0], rx, &rxlen, sizeof rx) == 0,
              "wire->tap: half a frame is not an error");
        check(rxlen == 6, "wire->tap: the half frame is held, not dropped");

        assert(write(sock[1], tail, sizeof tail) == (ssize_t)sizeof tail);
        check(pump_wire_to_tap(sock[0], tap[0], rx, &rxlen, sizeof rx) == 0,
              "wire->tap: the rest completes it");
        check(rxlen == 0, "wire->tap: the buffer is drained again");

        n = read(tap[1], buf, sizeof buf);
        check(n == 10, "wire->tap: the reassembled frame is 10 bytes");
        check(buf[0] == 0x11 && buf[9] == 0x22, "wire->tap: both halves are in it");
    }
    close(tap[0]);
    close(tap[1]);
    close(sock[0]);
    close(sock[1]);

    /* ---- segment -> host: a length that cannot be right ----
     * There is no way to resynchronise a pure length-prefixed stream, so the
     * only honest move is to drop the connection. */
    pair_tap(tap);
    pair_sock(sock);
    rxlen = 0;
    {
        unsigned char bad[2] = {0xFF, 0xFF}; /* 65535, far past 2048 */
        assert(write(sock[1], bad, 2) == 2);
        check(pump_wire_to_tap(sock[0], tap[0], rx, &rxlen, sizeof rx) != 0,
              "wire->tap: an impossible length drops the connection");
    }
    close(tap[0]);
    close(tap[1]);
    close(sock[0]);
    close(sock[1]);

    pair_tap(tap);
    pair_sock(sock);
    rxlen = 0;
    {
        unsigned char zero[2] = {0x00, 0x00}; /* a zero-length frame */
        assert(write(sock[1], zero, 2) == 2);
        check(pump_wire_to_tap(sock[0], tap[0], rx, &rxlen, sizeof rx) != 0,
              "wire->tap: a zero length drops the connection");
    }
    close(tap[0]);
    close(tap[1]);
    close(sock[0]);
    close(sock[1]);

    /* ---- the peer going away is a disconnect, not a frame ---- */
    pair_tap(tap);
    pair_sock(sock);
    rxlen = 0;
    {
        close(sock[1]);
        check(pump_wire_to_tap(sock[0], tap[0], rx, &rxlen, sizeof rx) != 0,
              "wire->tap: a closed segment is reported as lost");
    }
    close(tap[0]);
    close(tap[1]);
    close(sock[0]);

    /* ---- a maximum-size frame still fits ---- */
    pair_tap(tap);
    pair_sock(sock);
    {
        static unsigned char big[RETH_MAX_FRAME];
        memset(big, 0x5A, sizeof big);
        assert(write(tap[1], big, sizeof big) == (ssize_t)sizeof big);
        check(pump_tap_to_wire(tap[0], sock[0]) == 0, "tap->wire: a 2048-byte frame is accepted");
        n = recv(sock[1], buf, sizeof buf, 0);
        check(n >= 2 && ((buf[0] << 8) | buf[1]) == RETH_MAX_FRAME,
              "tap->wire: its length prefix reads 2048");
    }
    close(tap[0]);
    close(tap[1]);
    close(sock[0]);
    close(sock[1]);

    printf("\n%s\n", g_fail ? "FAILURES" : "all checks passed");
    return g_fail;
}
