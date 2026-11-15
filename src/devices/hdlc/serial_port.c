/*
 * serial_port.c - Raw host serial port (POSIX termios / Windows COM) for the HDLC modem.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2026 Ronny Hansen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The only platform code of the serial transport. Two halves, chosen by
 * _WIN32: Windows COM ports (CreateFile / SetCommState / ReadFile /
 * WriteFile) and POSIX ttys (termios, poll, read, write). Not built for
 * the browser.
 */

#include "serial_port.h"

#if HDLC_SERIAL_AVAILABLE

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "../../ndlib/log.h"

void serial_port_init(SerialPort *port)
{
    if (!port)
    {
        return;
    }
#ifdef _WIN32
    port->handle = NULL;
#else
    port->fd = -1;
#endif
}

bool serial_port_is_open(const SerialPort *port)
{
    if (!port)
    {
        return false;
    }
#ifdef _WIN32
    return port->handle != NULL;
#else
    return port->fd >= 0;
#endif
}

#ifdef _WIN32
/* ======================================================================== */
/* Windows                                                                  */
/* ======================================================================== */

/* COM10 and up can only be opened as \\.\COMn; the prefix works for COM1-9
 * too, so it is always added to a bare COMn name. */
static void win_device_path(const char *device, char *out, size_t outlen)
{
    if (strncmp(device, "\\\\", 2) == 0)
    {
        (void)snprintf(out, outlen, "%s", device);
    }
    else
    {
        (void)snprintf(out, outlen, "\\\\.\\%s", device);
    }
}

int serial_port_open(SerialPort *port, const char *device, int baud)
{
    char path[300];
    HANDLE h;
    DCB dcb;
    COMMTIMEOUTS to;

    if (!port || !device || !hdlc_link_spec_is_valid_baud(baud))
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "Serial: bad open request\n");
        return -1;
    }
    win_device_path(device, path, sizeof(path));

    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: cannot open %s (Windows error %lu)\n", path,
            (unsigned long)GetLastError());
        return -1;
    }

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb))
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "Serial: GetCommState %s failed (Windows error %lu)\n", path,
            (unsigned long)GetLastError());
        (void)CloseHandle(h);
        return -1;
    }
    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    /* The gateways read DTR as "host present". */
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb))
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "Serial: SetCommState %s failed (Windows error %lu)\n", path,
            (unsigned long)GetLastError());
        (void)CloseHandle(h);
        return -1;
    }

    /* ReadFile returns as soon as any byte is there, or after the constant
     * when nothing comes (MAXDWORD/MAXDWORD/constant is the documented form
     * for that). */
    memset(&to, 0, sizeof(to));
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = MAXDWORD;
    to.ReadTotalTimeoutConstant = SERIAL_PORT_READ_TIMEOUT_MS;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = SERIAL_PORT_WRITE_TIMEOUT_MS;
    if (!SetCommTimeouts(h, &to))
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "Serial: SetCommTimeouts %s failed (Windows error %lu)\n",
            path, (unsigned long)GetLastError());
        (void)CloseHandle(h);
        return -1;
    }
    if (!PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR))
    {
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: PurgeComm %s failed (Windows error %lu)\n", path,
            (unsigned long)GetLastError());
    }

    port->handle = h;
    LOG(LOG_CAT_HDLC, LOG_INFO, "Serial: opened %s at %d baud, 8N1, DTR and RTS on\n", path,
        baud);
    return 0;
}

void serial_port_close(SerialPort *port)
{
    if (!serial_port_is_open(port))
    {
        return;
    }
    if (!CloseHandle((HANDLE)port->handle))
    {
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: CloseHandle failed (Windows error %lu)\n",
            (unsigned long)GetLastError());
    }
    port->handle = NULL;
}

int serial_port_read(SerialPort *port, uint8_t *buf, int max)
{
    DWORD got = 0;

    if (!serial_port_is_open(port) || !buf || max <= 0)
    {
        return -1;
    }
    if (!ReadFile((HANDLE)port->handle, buf, (DWORD)max, &got, NULL))
    {
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: read failed (Windows error %lu), port lost\n",
            (unsigned long)GetLastError());
        return -1;
    }
    return (int)got;
}

int serial_port_write(SerialPort *port, const uint8_t *buf, int len)
{
    int sent = 0;

    if (!serial_port_is_open(port) || !buf || len < 0)
    {
        return -1;
    }
    while (sent < len)
    {
        DWORD put = 0;
        if (!WriteFile((HANDLE)port->handle, buf + sent, (DWORD)(len - sent), &put, NULL))
        {
            LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: write failed (Windows error %lu), port lost\n",
                (unsigned long)GetLastError());
            return -1;
        }
        if (put == 0)
        {
            LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: write timed out after %d ms\n",
                SERIAL_PORT_WRITE_TIMEOUT_MS);
            return -1;
        }
        sent += (int)put;
    }
    return 0;
}

#else
/* ======================================================================== */
/* POSIX                                                                    */
/* ======================================================================== */

static bool posix_speed(int baud, speed_t *out)
{
    switch (baud)
    {
    case 1200:
        *out = B1200;
        return true;
    case 2400:
        *out = B2400;
        return true;
    case 4800:
        *out = B4800;
        return true;
    case 9600:
        *out = B9600;
        return true;
    case 19200:
        *out = B19200;
        return true;
    case 38400:
        *out = B38400;
        return true;
    case 57600:
        *out = B57600;
        return true;
    case 115200:
        *out = B115200;
        return true;
    case 230400:
        *out = B230400;
        return true;
#ifdef B460800
    case 460800:
        *out = B460800;
        return true;
#endif
#ifdef B921600
    case 921600:
        *out = B921600;
        return true;
#endif
    default:
        return false;
    }
}

/* Put the tty in raw 8N1 with no flow control; reads never block in the
 * kernel (VMIN = VTIME = 0), waiting is done with poll(). */
static int posix_configure(int fd, const char *device, speed_t speed)
{
    struct termios tio;

    if (tcgetattr(fd, &tio) != 0)
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "Serial: %s is not a tty (%s)\n", device, strerror(errno));
        return -1;
    }
    cfmakeraw(&tio);
    tio.c_cflag &= ~(tcflag_t)(PARENB | CSTOPB | CSIZE);
    tio.c_cflag |= (tcflag_t)(CS8 | CLOCAL | CREAD);
#ifdef CRTSCTS
    tio.c_cflag &= ~(tcflag_t)CRTSCTS;
#endif
    tio.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0)
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "Serial: cannot set speed on %s (%s)\n", device,
            strerror(errno));
        return -1;
    }
    if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "Serial: tcsetattr %s failed (%s)\n", device,
            strerror(errno));
        return -1;
    }
    if (tcflush(fd, TCIOFLUSH) != 0)
    {
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: tcflush %s failed (%s)\n", device, strerror(errno));
    }
    return 0;
}

int serial_port_open(SerialPort *port, const char *device, int baud)
{
    speed_t speed;
    int fd;
    int lines = TIOCM_DTR | TIOCM_RTS;

    if (!port || !device || !posix_speed(baud, &speed))
    {
        LOG(LOG_CAT_HDLC, LOG_ERROR, "Serial: bad open request (baud %d)\n", baud);
        return -1;
    }

    fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: cannot open %s (%s)\n", device, strerror(errno));
        return -1;
    }
    if (posix_configure(fd, device, speed) != 0)
    {
        (void)close(fd);
        return -1;
    }
    /* The gateways read DTR as "host present". A device without modem
     * lines (a pseudo-terminal, some adapters) refuses this; the bytes
     * still flow, so that is a warning and not a failure. */
    if (ioctl(fd, TIOCMBIS, &lines) != 0)
    {
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: %s has no DTR/RTS lines (%s)\n", device,
            strerror(errno));
    }

    port->fd = fd;
    LOG(LOG_CAT_HDLC, LOG_INFO, "Serial: opened %s at %d baud, 8N1, DTR and RTS on\n", device,
        baud);
    return 0;
}

void serial_port_close(SerialPort *port)
{
    if (!serial_port_is_open(port))
    {
        return;
    }
    if (close(port->fd) != 0)
    {
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: close failed (%s)\n", strerror(errno));
    }
    port->fd = -1;
}

int serial_port_read(SerialPort *port, uint8_t *buf, int max)
{
    struct pollfd pfd;
    int pr;
    ssize_t n;

    if (!serial_port_is_open(port) || !buf || max <= 0)
    {
        return -1;
    }
    pfd.fd = port->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    pr = poll(&pfd, 1, SERIAL_PORT_READ_TIMEOUT_MS);
    if (pr < 0)
    {
        if (errno == EINTR)
        {
            return 0;
        }
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: poll failed (%s), port lost\n", strerror(errno));
        return -1;
    }
    if (pr == 0)
    {
        return 0;
    }
    if ((pfd.revents & POLLIN) == 0)
    {
        /* POLLHUP / POLLERR / POLLNVAL with nothing to read: unplugged. */
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: port hung up (poll events 0x%x)\n",
            (unsigned)pfd.revents);
        return -1;
    }
    n = read(port->fd, buf, (size_t)max);
    if (n < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            return 0;
        }
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: read failed (%s), port lost\n", strerror(errno));
        return -1;
    }
    if (n == 0)
    {
        /* poll said readable, read gives nothing: end of file, the device
         * is gone. */
        LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: end of file on port, port lost\n");
        return -1;
    }
    return (int)n;
}

int serial_port_write(SerialPort *port, const uint8_t *buf, int len)
{
    int sent = 0;

    if (!serial_port_is_open(port) || !buf || len < 0)
    {
        return -1;
    }
    while (sent < len)
    {
        ssize_t w = write(port->fd, buf + sent, (size_t)(len - sent));
        if (w > 0)
        {
            sent += (int)w;
            continue;
        }
        if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: write failed (%s), port lost\n",
                strerror(errno));
            return -1;
        }
        /* The output buffer is full: wait for room, but not for ever. */
        struct pollfd pfd;
        pfd.fd = port->fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, SERIAL_PORT_WRITE_TIMEOUT_MS);
        if (pr == 0)
        {
            LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: write timed out after %d ms\n",
                SERIAL_PORT_WRITE_TIMEOUT_MS);
            return -1;
        }
        if (pr < 0 && errno != EINTR)
        {
            LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: poll failed (%s), port lost\n", strerror(errno));
            return -1;
        }
        if (pr > 0 && (pfd.revents & POLLOUT) == 0)
        {
            LOG(LOG_CAT_HDLC, LOG_WARN, "Serial: port hung up during write\n");
            return -1;
        }
    }
    return 0;
}

#endif /* _WIN32 */

#endif /* HDLC_SERIAL_AVAILABLE */
