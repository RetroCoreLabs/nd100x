/*
 * nd100x - ND100 Virtual Machine
 *
 * Copyright (c) 2006 Per-Olof Astrom
 * Copyright (c) 2006-2008 Roger Abrahamsson
 * Copyright (c) 2008 Zdravko
 * Copyright (c) 2025 Ronny Hansen
 *
 * This file is originated from the nd100em project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the nd100em
 * distribution in the file COPYING); if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "../devices_types.h"
#include "../devices_protos.h"


#include "../../cpu/cpu_types.h"
#include "../../cpu/cpu_protos.h"


#include "panel.h"

struct display_panel *gPAP;


void setup_pap(void)
{

	gPANS = 0x8000;			/* Tell system we are here (Bit 15 active will activate MOPC logic in MS20 in microcode every 20 ms)*/
	gPANS = gPANS | 0x4000; /* Set FULL which is active low, so not full */

	gPAP = calloc(1, sizeof(struct display_panel));

	UpdateMachineTime();
}

void ProcessMessageControl(PANC_Register panc)
{
	MessageControl mc = (panc.bits.wpan & 0b111);

	// if (debug) fprintf(debugfile, "Processing Message Control Command: %d WPAN=%d\n", mc, WPAN);

	switch (mc)
	{
	case StopRotatingMessage:
		gPAP->function_mode = 0; // Stop rotating
		break;
	case ReturnDisplayToNormal:
		gPAP->function_mode = 1; // Return to normal
		break;
	case ClearTextBuffer:
		gPAP->fdisp_cntr = 0;									   // Clear display counter
		memset(gPAP->func_display, 0, sizeof(gPAP->func_display)); // Clear display buffer
		break;
	case RotateMessage:
		gPAP->function_mode = 2; // Start rotating
		break;
	case ClearAndRotate:
		gPAP->fdisp_cntr = 0;									   // Clear display counter
		memset(gPAP->func_display, 0, sizeof(gPAP->func_display)); // Clear display buffer
		gPAP->function_mode = 2;								   // Start rotating
		break;
	default:
		break;
	}
}

/// <summary>
/// Called from TRR logic when "TRR PANC" has been executed
/// Process the command in gPANC
/// </summary>
void ProcessTerminalPanc(void)
{
	if (!gPAP)
		return;

	PANC_Register panc;
	panc.raw = gPANC;

	PANS_Register pans;
	pans.raw = 0;

	// Update time from the Host realtime clock when reading
	if (panc.bits.read_request)
		UpdateMachineTime();

	// Is this a system request?
	if (panc.bits.pfunc)
	{

		pans.bits.panel_present = 1; // yes, we are here
		pans.bits.input_pending = 1; // FIFO not full
		pans.bits.pan_interrupt = 1; // Yes, we have a PAN interrupt

		pans.bits.rpan = panc.bits.read_request; // Answering read or write request?
		pans.bits.pfunc = panc.bits.pfunc;		 // whhat function are we responding to?

		switch (panc.bits.pfunc)
		{
		case STATUS_ILLEGAL:
			// TODO: Implement!
			break;
		case STATUS_FUTURE_EXTENSION:
			// TODO: Implement!
			break;
		case STATUS_MESSAGE_APPEND:
			if (panc.bits.read_request)
			{
				// Read request not implemented for message append
			}
			else
			{
				if (panc.bits.wpan != 0)
				{
					if (panc.bits.wpan == 0x0E)
					{
						// "Shift Out" character - switch to alternate character set??
						// Not implemented yet
					}
					else
					{
						char c = (char)panc.bits.wpan;

						// Check if we have room in the buffer
						if (gPAP->fdisp_cntr < sizeof(gPAP->func_display) - 1)
						{
							gPAP->func_display[gPAP->fdisp_cntr++] = c;
						}
					}
				}
			}
			break;
		case STATUS_MESSAGE_CONTROL:
			if (panc.bits.read_request)
			{
				// read what ?
			}
			else
			{
				ProcessMessageControl(panc);
			}
			break;
		case STATUS_UPDATE_LOW_SECONDS:
			if (panc.bits.read_request)
			{
				pans.bits.rpan = (ushort)(gPAP->seconds & 0xFF);
				pans.bits.read_panel_valid = 1; // Yes, we have a valid response
			}
			else
			{
				gPAP->seconds = (ushort)((gPAP->seconds & 0xff00) | panc.bits.wpan);
			}
			break;
		case STATUS_UPDATE_HIGH_SECONDS:
			if (panc.bits.read_request)
			{
				pans.bits.rpan = (ushort)(gPAP->seconds >> 8 & 0xFF);
				pans.bits.read_panel_valid = 1; // Yes, we have a valid response
			}
			else
			{
				gPAP->seconds = (gPAP->seconds & 0x00ff) | (panc.bits.wpan << 8);
			}
			break;
		case STATUS_UPDATE_LOW_DAYS:
			if (panc.bits.read_request)
			{
				pans.bits.rpan = (ushort)(gPAP->days & 0x00ff);
				pans.bits.read_panel_valid = 1; // Yes, we have a valid response
			}
			else
			{
				gPAP->days = (gPAP->days & 0xFF00) | panc.bits.wpan;
			}
			break;
		case STATUS_UPDATE_HIGH_DAYS:
			if (panc.bits.read_request)
			{
				pans.bits.rpan = (ushort)(gPAP->days >> 8 & 0x00ff);
				pans.bits.read_panel_valid = 1; // Yes, we have a valid response
			}
			else
			{
				gPAP->days = (gPAP->days & 0x00FF) | (ushort)(panc.bits.wpan << 8);
			}
			break;
		case STATUS_MEMORY_EXAMINE:
			// TODO: Implement!
			break;
		case STATUS_DATA_TO_EXAMINE:
			// TODO: Implement!
			break;
		case STATUS_ACTIVE_LEVELS:
			// TODO: Implement!
			break;
		case STATUS_OUT_EXAMINE_MODE:
			// TODO: Implement!
			break;
		case STATUS_SEND_LABEL:
			// TODO: Implement!
			break;
		case STATUS_F_TYPED:
			// TODO: Implement!
			break;
		}
	}
	gPANS = pans.raw;
}

// respond to TRR LMP
void ProcessTerminalLamp(void)
{
	// read gLMP
	gPANS = 0x0000;
}
/// <summary>
/// Calculate HW clock info
///
/// HW Clock contains an offset since 00:00:00 1.January 1979 (TBASE)
/// The clock counts seconds and half-days (12 hours) from this time.
/// </summary>
void UpdateMachineTime(void)
{
	time_t tbase = 0;
	time_t now = time(NULL);

	// Set base time to 1979-01-01 00:00:00 CET
	struct tm *tm_base = localtime(&tbase);
	tm_base->tm_year = 79;  // Years since 1900, so 79 = 1979
	tm_base->tm_mon = 0;    // Months are 0-based, so 0 = January
	tm_base->tm_mday = 1;   // Day of month
	tm_base->tm_hour = 0;
	tm_base->tm_min = 0;
	tm_base->tm_sec = 0;
	tbase = mktime(tm_base);

	struct tm *tm_now = localtime(&now);


	// Sintran doesn't support Y2K (without patches) so stay in year before 2000...
	// Subtract 30 years from current time (2025-30 = 1995)

	tm_now->tm_year -= 30;
	now = mktime(tm_now);

	time_t midnight = now;
	struct tm *tm_midnight = localtime(&midnight);

	// Calculate days difference from TBASE
	int days_diff = (int)(difftime(now, tbase) / (24.0 * 3600.0));
	gPAP->days = (uint16_t)(days_diff * 2); // Convert to half-days

	// Check if we've passed noon
	if (tm_now->tm_hour >= 11)
	{
		gPAP->days++; // Add another half day

		// Get midnight of current day
		tm_midnight->tm_hour = 0;
		tm_midnight->tm_min = 0;
		tm_midnight->tm_sec = 0;
		midnight = mktime(tm_midnight);

		// Now counting since noon
		struct tm *tm_noon = localtime(&now);
		tm_noon->tm_hour -= 12;
		now = mktime(tm_noon);
	}

	// Calculate seconds since midnight
	gPAP->seconds = (uint16_t)difftime(now, midnight);
}

#ifdef _later_
void panel_thread(void)
{
	int s;
	int sock, connected, bytes_recieved;
	char recv_data[1024];
	struct sockaddr_in client_addr;
	socklen_t sin_size;

	if (debug)
		fprintf(debugfile, "(#)panel_thread running...\n");
	if (debug)
		fflush(debugfile);

	do_listen(5000, 1, &sock);
	if (debug)
		fprintf(debugfile, "\n(#)TCPServer Waiting for client on port 5000\n");
	if (debug)
		fflush(debugfile);

	while (CurrentCPURunMode != SHUTDOWN)
	{
		sin_size = (socklen_t)sizeof(struct sockaddr_in);
		connected = accept(sock, (struct sockaddr *)&client_addr, &sin_size);
		if (debug)
			fprintf(debugfile, "(#)I got a panel connection from (%s , %d)\n",
					inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
		if (debug)
			fflush(debugfile);
		while (CurrentCPURunMode != SHUTDOWN)
		{
			bytes_recieved = recv(connected, recv_data, 1024, 0);
			recv_data[bytes_recieved] = '\0';
			if (debug)
				fprintf(debugfile, "(#)PANEL DATA received\n");
			if (strncmp("OPCOM_PRESSED\n", recv_data, strlen("OPCOM_PRESSED")) == 0)
			{
				MODE_OPCOM = 1;
				if (debug)
					fprintf(debugfile, "(#)OPCOM_PRESSED\n");
			}
			else if (strncmp("MCL_PRESSED\n", recv_data, strlen("MCL_PRESSED")) == 0)
			{
				if (debug)
					fprintf(debugfile, "(#)MCL_PRESSED\n");
				/* TODO:: this should be in a separate routine DoMCL later */
				CurrentCPURunMode = STOP;
				/* NOTE:: buggy in that we cannot do STOP and MCL without a running cpu between.. FIXME */
				while ((s = sem_wait(&sem_stop)) == -1 && errno == EINTR) /* wait for stop lock to be free and take it */
					continue;											  /* Restart if interrupted by handler */
				bzero(gReg, sizeof(struct CpuRegs));					  /* clear cpu */
				setbit(_STS, _O, 1);
				setbit_STS_MSB(_N100, 1);
				gCSR = 1 << 2; /* this bit sets the cache as not available */
			}
			else if (strncmp("LOAD_PRESSED\n", recv_data, strlen("LOAD_PRESSED")) == 0)
			{
				if (debug)
					fprintf(debugfile, "(#)LOAD_PRESSED\n");
				gPC = STARTADDR;
				CurrentCPURunMode = RUN;
				if (sem_post(&sem_run) == -1)
				{ /* release run lock */
					if (debug)
						fprintf(debugfile, "ERROR!!! sem_post failure panel_thread\n");
					CurrentCPURunMode = SHUTDOWN;
				}
			}
			else if (strncmp("STOP_PRESSED\n", recv_data, strlen("STOP_PRESSED")) == 0)
			{
				if (debug)
					fprintf(debugfile, "(#)STOP_PRESSED\n");
				CurrentCPURunMode = STOP;
				/* NOTE:: buggy in that we cannot do STOP and MCL without a running cpu between.. FIXME */
				while ((s = sem_wait(&sem_stop)) == -1 && errno == EINTR) /* wait for stop lock to be free and take it */
					continue;											  /* Restart if interrupted by handler */
			}
			else
			{
				if (debug)
					fprintf(debugfile, "(#)Panel received:%s\n", recv_data);
			}
			if (debug)
				fflush(debugfile);
		}
	}
	close(sock);
	return;
}

void panel_event(void)
{
	char tmpbyte;

	if (gPAP->trr_panc)
	{ /* TRR has been issued, process command */
		if (debug)
			fprintf(debugfile, "panel_event: TRR\n");
		if (debug)
			fflush(debugfile);
		gPAP->trr_panc = false;
		switch ((gPANC & 0x0700) >> 8)
		{
		case 0: /* Illegal */
			break;
		case 1: /* Future extension */
			break;
		case 2: /* Message Append */ // TODO: Not Implemented yet except basic return info
			gPANS = 0xd200;
			break;
		case 3: /* Message Control */ // TODO: Not Implemented yet except basic return info
			gPANS = 0xd300;
			break;
		case 4: /* Update Low Seconds */
			if (gPANC & 0x2000)
			{ /* Read */
				tmpbyte = (gPAP->seconds) & 0x00ff;
				gPANS = 0xf400 | tmpbyte;
			}
			else
			{ /*Write */
				tmpbyte = gPANC & 0x00ff;
				gPAP->seconds = (gPAP->seconds & 0xff00) | tmpbyte;
				gPANS = 0xd400;
			}
			break;
		case 5: /* Update High Seconds */
			if (gPANC & 0x2000)
			{ /* Read */
				tmpbyte = (gPAP->seconds >> 8);
				gPANS = 0xf500 | tmpbyte;
			}
			else
			{ /*Write */
				tmpbyte = gPANC & 0x00ff;
				gPAP->seconds = (gPAP->seconds & 0x00ff) | ((ushort)tmpbyte) << 8;
				gPANS = 0xd500;
			}
			break;
		case 6: /* Update Low Days */
			if (gPANC & 0x2000)
			{ /* Read */
				tmpbyte = (gPAP->days) & 0x00ff;
				gPANS = 0xf600 | tmpbyte;
			}
			else
			{ /*Write */
				tmpbyte = gPANC & 0x00ff;
				gPAP->days = (gPAP->days & 0xff00) | tmpbyte;
				gPANS = 0xd600;
			}
			break;
		case 7: /* Update High Days */
			if (gPANC & 0x2000)
			{ /* Read */
				tmpbyte = (gPAP->days >> 8);
				gPANS = 0xf700 | tmpbyte;
			}
			else
			{ /*Write */
				tmpbyte = gPANC & 0x00ff;
				gPAP->days = (gPAP->days & 0x00ff) | ((ushort)tmpbyte) << 8;
				gPANS = 0xd700;
			}
			break;
		default: /* This should never happen */
			break;
		}
		if (debug)
			fprintf(debugfile, "panel_event: TRR - result: gPANS = %0x04\n", gPANS);
		if (debug)
			fflush(debugfile);
	}
	if (gPAP->sec_tick)
	{ /* Seconds tick from rtc, update counters */
		// if (debug) fprintf(debugfile,"panel_event: 1 second tick\n");
		// if (debug) fflush(debugfile);
		gPAP->sec_tick = false;
		gPAP->seconds++;
		if (gPAP->seconds >= 43200)
		{ /* 12h wraparound */
			gPAP->seconds = 0;
			gPAP->days++;
		}
	}
}

void panel_processor_thread(void)
{
	int s;
	while (CurrentCPURunMode != SHUTDOWN)
	{
		while ((s = sem_wait(&sem_pap)) == -1 && errno == EINTR) /* wait for pap 'kick' */
			continue;											 /* Restart if interrupted by handler */

		panel_event();
	}
	return;
}
#endif