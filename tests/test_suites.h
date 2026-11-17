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

/* test_memory_banks */
int run_memory_bank_tests(void);

/* test_hdlc */
int run_tcp_receive_buffer_tests(void);
int run_hdlc_frame_tests(void);
int run_hdlc_crc_tests(void);
int run_hdlc_link_spec_tests(void);
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
int run_serial_modem_tests(void); /* pseudo-terminal pair, POSIX only */
int run_hdlc_displacement_tests(void); /* links ndlib, POSIX only */
#endif

#endif /* TEST_SUITES_H */
