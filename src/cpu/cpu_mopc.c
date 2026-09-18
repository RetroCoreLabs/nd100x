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

#include <string.h>

#include "cpu_types.h"
#include "cpu_protos.h"


extern int mopc_in(char *chptr);
extern void mopc_out(char ch);
/*
void mopc_out(char ch)
{
    IO_Write(0305, ch); // write to console device (0305)
}

int mopc_in(char *chptr)
{
    *chptr = IO_Read(0300); // read from console device
    return 1;
}
*/


/*
 * converts an ascii octal number to an integer.
 * only handles positive values, so if result is negative
 * we have an error. this also means we only handle numbers
 * up to max positive int on the platform.
 */
int aoct2int(char *str)
{
	double tmp = 0;
	int num, count;

	num = strlen(str);
	for (count = 0; num > 0; num--)
	{
		if ((str[num - 1] >= '0') && (str[num - 1] <= '7'))
		{
			tmp += (double)((str[num - 1] - '0') * pow((double)8, (double)count));
			count++;
		}
		else
		{
			return (-1);
		}
	}
	if (tmp > (double)INT_MAX)
		tmp = -1;
	return ((int)tmp);
}

/*
 *
 */
void mopc_cmd(char *cmdstr, char cmdc)
{
	int len;
	int val = -1;   /* -1 = no octal argument given */
	bool has_val = false;

	len = strlen((const char *)cmdstr);
	if (len > 255)
		return; /* This is BAAD, so we just silently fail the command at the moment */

	if (len)
	{ /* We probably have an octal argument here */
		val = aoct2int(cmdstr);
		has_val = true;
	}
	switch (cmdc)
	{
	case '.':
		/* Set breakpoint */
		if (!(has_val))
		{
			/*TODO: Check whats needed here */
		}
		if ((val >= 0) && (val < 65536))
		{ /* valid range for 16 bit addr */
			gReg->has_breakpoint = true;
			gReg->breakpoint = (ushort)(val & 0xffff);
			set_cpu_run_mode(CPU_BREAKPOINT);
		}
		break;
	default:
		break;
	}
}

/* We run mopc as a thread here, but ticks it either from panel or rtc to get more correct nd behaviour */
/* TODO:: NO ERROR CHECKING CURRENTLY DONE!!!! Need to see how real ND mopc behaves first */
void mopc_thread(void)
{
	char ch;
	char str[256];
	unsigned char ptr = 0; /* points to next free char position in str */
	int i;

	memset(str, '\0', sizeof(str));

	while (get_cpu_run_mode() != CPU_SHUTDOWN)
	{
		/* This should trigger once every rtc/panel interrupt hopefully */

		if (mopc_in(&ch))
		{
            /* char available */
			if ((ch >= '0' && ch <= '7') || (ch >= 'A' && ch <= 'Y'))
			{
				str[ptr] = ch;
				ptr++;
				mopc_out(ch);
			}
			else if ((ch == '@') || (ch == ' '))
			{
				ptr = 0;
				mopc_out(ch);
			}
			else if (ch == 10)
			{
				//				mopc_cmd(str,ch);
				mopc_out(ch);
				ptr = 0;
			}
			else if ((ch == '@') || (ch == ' ') || (ch == '<') || (ch == '/') || (ch == '*'))
			{
				mopc_out(ch);
			}
			else if ((ch == '&') || (ch == '$'))
			{
				mopc_out(ch);
			}
			else if (ch == '.')
			{
				mopc_out(ch);
				mopc_cmd(str, ch);
				ptr = 0;
				memset(str, '\0', sizeof(str));
			}
			else if (ch == 'Z')
			{
				mopc_out(ch);
			}
			else if (ch == '!')
			{
				if (get_cpu_run_mode() == CPU_STOPPED)
				{
					mopc_out(ch);
					if (ptr)
					{					   /* ok we have some chars available */
						i = aoct2int(str); /* FIXME :: THIS IS WRONG, we should use octal input, not decimal!!! (just added this quickly to test)*/
						gPC = i;
					}
					set_cpu_run_mode(CPU_RUNNING);
				}
				else
				{
					mopc_out('?');
				}
			}
			else if (ch == '#')
			{
				mopc_out(ch);
			}
			else if (ch == 27)
			{
                // TODO: Handle escape sequence to get out of OPCOM in case we where temporay in it
				//if (CurrentCPURunMode != STOP)
				//	MODE_OPCOM = 0;
			}
			else
				mopc_out('?');
		}
	}
}
