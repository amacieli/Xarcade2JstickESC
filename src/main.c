/* ======================================================================== */
/*  This program is free software; you can redistribute it and/or modify    */
/*  it under the terms of the GNU General Public License as published by    */
/*  the Free Software Foundation; either version 2 of the License, or       */
/*  (at your option) any later version.                                     */
/*                                                                          */
/*  This program is distributed in the hope that it will be useful,         */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       */
/*  General Public License for more details.                                */
/*                                                                          */
/*  You should have received a copy of the GNU General Public License       */
/*  along with this program; if not, write to the Free Software             */
/*  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.               */
/* ======================================================================== */
/*                 Copyright (c) 2014, Florian Mueller                      */
/* ======================================================================== */

// Modifications made on 2018-01-28 by Adam Macielinski
// Pressing 1 and 3 (or, P1 START and SELECT) together will output ESC
// Pressing 2 and 4 (or, P2 START and SELECT) together will output ESC
// If started with the option "-m", assumes MAME, and performs no remapping
// other than the above.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <syslog.h>

#include "uinput_gamepad.h"
#include "uinput_kbd.h"
#include "input_xarcade.h"

// TODO Extract all magic numbers and collect them as defines in at a central location

#define GPADSNUM 2
#define GPAD1 0
#define GPAD2 1

UINP_KBD_DEV uinp_kbd;
UINP_GPAD_DEV uinp_gpads[GPADSNUM];
INP_XARC_DEV xarcdev;
int use_syslog = 0;
int mame = 0;

#define SYSLOG(...) if (use_syslog == 1) { syslog(__VA_ARGS__); }

static void teardown();
static void signal_handler(int signum);
static void x2j_write(int keypressed, int mappedkey, int option, int eventtype, int gpad);
static void x2j_write_keyboard(int keytopress);
static void x2j_write_gpad(int keytopress, int gpad);
static void x2j_handle_signal_SIGUSR1(int signum);
static void x2j_handle_signal_SIGUSR2(int signum);

int main(int argc, char* argv[]) {
	int result = 0;
	int rd, ctr, combo = 0;
	char keyStates[256];

	int detach = 0;
	int opt;

	while ((opt = getopt(argc, argv, "+dsm")) != -1) {
		switch (opt) {
			case 'd':
				detach = 1;
				break;
			case 's':
				use_syslog = 1;
				break;
			case 'm':
				mame = 1;	// User wanted a MAME configuration
				break;
			default:
				fprintf(stderr, "Usage: %s [-d] [-s] [-m]\n", argv[0]);
				exit(EXIT_FAILURE);
				break;
		}
	}

	SYSLOG(LOG_NOTICE, "Starting.");

	if (mame)
		printf("[Xarcade2Joystick] Setting up for MAME.\n");
	else
		printf("[Xarcade2Joystick] NOT setting up for MAME.\n");

	printf("[Xarcade2Joystick] Getting exclusive access: ");
	result = input_xarcade_open(&xarcdev, INPUT_XARC_TYPE_TANKSTICK);
	if (result != 0) {
		if (errno == 0) {
			printf("Xarcade not found.\n");
			SYSLOG(LOG_ERR, "Xarcade not found, exiting.");
		} else {
			printf("Failed to get exclusive access to Xarcade: %d (%s)\n", errno, strerror(errno));
			SYSLOG(LOG_ERR, "Failed to get exclusive access to Xarcade, exiting: %d (%s)", errno, strerror(errno));
		}
		exit(EXIT_FAILURE);
	}

	SYSLOG(LOG_NOTICE, "Got exclusive access to Xarcade.");

	uinput_gpad_open(&uinp_gpads[0], UINPUT_GPAD_TYPE_XARCADE, 1);
	uinput_gpad_open(&uinp_gpads[1], UINPUT_GPAD_TYPE_XARCADE, 2);
	uinput_kbd_open(&uinp_kbd);

	if (detach) {
		if (daemon(0, 1)) {
			perror("daemon");
			return 1;
		}
	}
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGUSR1, x2j_handle_signal_SIGUSR1);
	signal(SIGUSR2, x2j_handle_signal_SIGUSR2);

	SYSLOG(LOG_NOTICE, "Running.");

	while (1) {
		rd = input_xarcade_read(&xarcdev);
		if (rd < 0) {
			break;
		}

		for (ctr = 0; ctr < rd; ctr++) {
			if (xarcdev.ev[ctr].type == 0)
				continue;
			if (xarcdev.ev[ctr].type == EV_MSC)
				continue;
			if (EV_KEY == xarcdev.ev[ctr].type) {

				keyStates[xarcdev.ev[ctr].code] = xarcdev.ev[ctr].value;

				switch (xarcdev.ev[ctr].code) {

				/* ----------------  Player 1 controls ------------------- */
				/* buttons */
				case KEY_LEFTCTRL:
					x2j_write(xarcdev.ev[ctr].code, BTN_A, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD1);
					break;
				case KEY_LEFTALT:
					x2j_write(xarcdev.ev[ctr].code, BTN_B, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD1);
					break;
				case KEY_SPACE:
					x2j_write(xarcdev.ev[ctr].code, BTN_C, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD1);
					break;
				case KEY_LEFTSHIFT:
					x2j_write(xarcdev.ev[ctr].code, BTN_X, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD1);
					break;
				case KEY_Z:
					x2j_write(xarcdev.ev[ctr].code, BTN_Y, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD1);
					break;
				case KEY_X:
					x2j_write(xarcdev.ev[ctr].code, BTN_Z, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD1);
					break;
				case KEY_C:
					x2j_write(xarcdev.ev[ctr].code, BTN_TL, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD1);
					break;
				case KEY_5:
					x2j_write(xarcdev.ev[ctr].code, BTN_TR, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD1);
					break;
				case KEY_1:
                                        if (keyStates[KEY_3] && xarcdev.ev[ctr].value) {
                                                x2j_write_keyboard(KEY_ESC);
                                                combo = 2;
                                                continue;
                                        }
                                        /* it's a key down, ignore */
                                        if (xarcdev.ev[ctr].value)
                                                continue;
                                        if (!combo)
	                                        x2j_write_gpad(BTN_START, GPAD1);
                                        else
                                                combo--;
                                        break;
				case KEY_3:
                                        /* it's a key down, ignore */
                                        if (xarcdev.ev[ctr].value)
                                                continue;
                                        if (!combo)
	                                        x2j_write_gpad(BTN_SELECT, GPAD1);
                                        else
                                                combo--;
                                        break;
				case KEY_KP4:
				case KEY_LEFT:
					x2j_write(xarcdev.ev[ctr].code, ABS_X, xarcdev.ev[ctr].value == 0 ? 2 : 0, EV_ABS, GPAD1); // center or left
					break;
				case KEY_KP6:
				case KEY_RIGHT:
					x2j_write(xarcdev.ev[ctr].code, ABS_X, xarcdev.ev[ctr].value == 0 ? 2 : 4, EV_ABS, GPAD1); // center or right
					break;
				case KEY_KP8:
				case KEY_UP:
					x2j_write(xarcdev.ev[ctr].code, ABS_Y, xarcdev.ev[ctr].value == 0 ? 2 : 0, EV_ABS, GPAD1); // center or up
					break;
				case KEY_KP2:
				case KEY_DOWN:
					x2j_write(xarcdev.ev[ctr].code, ABS_Y, xarcdev.ev[ctr].value == 0 ? 2 : 4, EV_ABS, GPAD1); // center or down
					break;

					/* ----------------  Player 2 controls ------------------- */
					/* buttons */
				case KEY_A:
					x2j_write(xarcdev.ev[ctr].code, BTN_A, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD2);
					break;
				case KEY_S:
					x2j_write(xarcdev.ev[ctr].code, BTN_B, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD2);
					break;
				case KEY_Q:
					x2j_write(xarcdev.ev[ctr].code, BTN_C, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD2);
					break;
				case KEY_W:
					x2j_write(xarcdev.ev[ctr].code, BTN_X, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD2);
					break;
				case KEY_E:
					x2j_write(xarcdev.ev[ctr].code, BTN_Y, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD2);
					break;
				case KEY_LEFTBRACE:
 					x2j_write(xarcdev.ev[ctr].code, BTN_Z, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD2);
					break;
				case KEY_RIGHTBRACE:
					x2j_write(xarcdev.ev[ctr].code, BTN_TL, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD2);
					break;
				case KEY_6:
					x2j_write(xarcdev.ev[ctr].code, BTN_TR, xarcdev.ev[ctr].value > 0, EV_KEY, GPAD2);
					break;
/* 2018-01-28 Adam Macielinski - if KEY_2 and KEY_4 are pressed together, then write KEY_ESC */
				case KEY_2:
					/* handle combination */
					if (keyStates[KEY_4] && xarcdev.ev[ctr].value) {
						x2j_write_keyboard(KEY_ESC);
						combo = 2;
						continue;
					}
					/* it's a key down, ignore */
					if (xarcdev.ev[ctr].value)
						continue;
					if (!combo)
						x2j_write_gpad(BTN_START, GPAD2);
					else
						combo--;
					break;
				case KEY_4:
					/* it's a key down, ignore */
					if (xarcdev.ev[ctr].value)
						continue;
					if (!combo)
						x2j_write_gpad(BTN_SELECT, GPAD2);
					else
						combo--;
					break;

					/* joystick */
				case KEY_D:
					x2j_write(xarcdev.ev[ctr].code, ABS_X, xarcdev.ev[ctr].value == 0 ? 2 : 0, EV_ABS, GPAD2); // center or left
					break;
				case KEY_G:
					x2j_write(xarcdev.ev[ctr].code, ABS_X, xarcdev.ev[ctr].value == 0 ? 2 : 4, EV_ABS, GPAD2); // center or right
					break;
				case KEY_R:
					x2j_write(xarcdev.ev[ctr].code, ABS_Y, xarcdev.ev[ctr].value == 0 ? 2 : 0, EV_ABS, GPAD2); // center or up
					break;
				case KEY_F:
					x2j_write(xarcdev.ev[ctr].code, ABS_Y, xarcdev.ev[ctr].value == 0 ? 2 : 4, EV_ABS, GPAD2); // center or down
					break;

				default:
					break;
				}
			}
		}
	}

	teardown();
	return EXIT_SUCCESS;
}

static void teardown() {
	printf("Exiting.\n");
	SYSLOG(LOG_NOTICE, "Exiting.");

	input_xarcade_close(&xarcdev);
	uinput_gpad_close(&uinp_gpads[0]);
	uinput_gpad_close(&uinp_gpads[1]);
	uinput_kbd_close(&uinp_kbd);
}

static void signal_handler(int signum) {
	signal(signum, SIG_DFL);

	printf("Received signal %d (%s), exiting.\n", signum, strsignal(signum));
	SYSLOG(LOG_NOTICE, "Received signal %d (%s), exiting.", signum, strsignal(signum));
	teardown();
	exit(EXIT_SUCCESS);
}

/* For most X-Arade keys:
   If we're using MAME, then just simulate whatever the X-Arcade would normally have provided.
   Otherwise, map to a gamepad button.
*/
static void x2j_write(int keypressed, int mappedkey, int option, int eventtype, int gpad) {
	if (mame) {
		// If we are using MAME, then pass through whatever X-Arcade is sending, without mapping.
		uinput_kbd_write(&uinp_kbd, keypressed, option, eventtype);
	} else {
		// Otherwise, map X-Arcade to a gamepad key.
		uinput_gpad_write(&uinp_gpads[gpad], mappedkey, option, eventtype); }
}

static void x2j_write_keyboard(int keytopress) {
	uinput_kbd_write(&uinp_kbd, keytopress, 1, EV_KEY);
	uinput_kbd_sleep();
	uinput_kbd_write(&uinp_kbd, keytopress, 0, EV_KEY);
}

static void x2j_write_gpad(int keytopress, int gpad) {
	uinput_gpad_write(&uinp_gpads[gpad], keytopress, 1, EV_KEY);
	uinput_gpad_sleep();
	uinput_gpad_write(&uinp_gpads[gpad], keytopress, 0, EV_KEY);
}

static void x2j_handle_signal_SIGUSR1(int signum) {
	mame = 0;
}

static void x2j_handle_signal_SIGUSR2(int signum) {
	mame = 1;
}

