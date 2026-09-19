/*
 * test_suites.h - Entry points of the test suites linked into a shared test main.
 *
 * nd100x - ND100 Virtual Machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2026 Ronny Hansen
 *
 * Entry points of the test suites that are linked into a shared test main
 * (test_printer_main.c and test_hdlc_main.c). Each returns the number of
 * failed checks.
 */

#ifndef TEST_SUITES_H
#define TEST_SUITES_H

/* test_printer: tmpdir is a scratch directory the suite may write into */
int run_pdfwriter_tests(const char *tmpdir);
int run_escp_tests(void);
int run_printjob_tests(const char *tmpdir);

/* test_hdlc */
int run_tcp_receive_buffer_tests(void);
int run_hdlc_frame_tests(void);
int run_hdlc_crc_tests(void);

#endif /* TEST_SUITES_H */
