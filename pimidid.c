/*
pimidid

https://github.com/vs49688/pimidid

Licensed under the GNU General Public License v2.0 only <https://spdx.org/licenses/GPL-2.0-only.html>
SPDX-License-Identifier: GPL-2.0-only
Copyright (C) 2018 Zane van Iperen

This program is free software; you can redistribute it and/or modify
it undertheterms of the GNU General Public License as published by
the Free Software Foundation; version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA. 
*/

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <alsa/asoundlib.h>
#include <libudev.h>
#include <syslog.h>
#include "pimidid.h"

static void error_handler(const char *file, int line, const char *function, int err, const char *fmt, ...)
{
	va_list arg;

	if (err == ENOENT)	/* Ignore those misleading "warnings" */
		return;

	syslog(LOG_ERR, "ALSA lib %s:%i:(%s):", file, line, function);
	va_start(arg, fmt);
	vsyslog(LOG_ERR, fmt, arg);
	if(err)
		syslog(LOG_ERR, "  %s", snd_strerror(err));

	va_end(arg);
}

#define perm_ok(pinfo,bits) ((snd_seq_port_info_get_capability(pinfo) & (bits)) == (bits))

typedef struct pimidid_search
{
	pimidid_t *pi;

	int card;

	snd_seq_client_info_t *fluid_client;
	snd_seq_port_info_t *fluid_port;

	snd_seq_client_info_t *dev_client;
	snd_seq_port_info_t *dev_port;
} pimidid_search_t;

/* Try to find the fluidsynth port. */
static int locate_fluidstynth(snd_seq_t *seq, snd_seq_client_info_t *cinfo, snd_seq_port_info_t *pinfo, int count, void *user)
{
	pimidid_search_t *s = (pimidid_search_t*)user;

	if(s->fluid_port)
		return 0;

	const char *name = snd_seq_client_info_get_name(cinfo);
	if(strstr(name, "FLUID") != name)
		return 0;

	unsigned int caps = snd_seq_port_info_get_capability(pinfo);

	if(!perm_ok(pinfo, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
		return 0;

	s->fluid_client = s->pi->_fluid_client;
	snd_seq_client_info_copy(s->fluid_client, cinfo);

	s->fluid_port = s->pi->_fluid_port;
	snd_seq_port_info_copy(s->fluid_port, pinfo);

	return 1;
}

static int locate_ports(snd_seq_t *seq, snd_seq_client_info_t *cinfo, snd_seq_port_info_t *pinfo, int count, void *user)
{
	pimidid_search_t *s = (pimidid_search_t*)user;

	assert(s->fluid_port);

	/* If we have a udev card number, see if it matches the alsa one. */
	if(s->card >= 0 && s->card != snd_seq_client_info_get_card(cinfo))
		return 0;

	/* Skip the "System" client. */
	if(snd_seq_client_info_get_client(cinfo) == 0)
		return 0;

	if(!perm_ok(pinfo, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
		return 0;

	syslog(LOG_INFO,
		"Found DEVICE port at %3d:%d, connecting to %d:%d...\n",
		snd_seq_client_info_get_client(cinfo),
		snd_seq_port_info_get_port(pinfo),
		snd_seq_client_info_get_client(s->fluid_client),
		snd_seq_port_info_get_port(s->fluid_port)
	);

	if(pimidid_connect(seq, pinfo, s->fluid_port) < 0)
		syslog(LOG_WARNING, "Connection failed (%s)\n", snd_strerror(errno));

	return 0;
}

/*
** - Scan and find the first FluidSynth writeable port
** - Scan and connect each readable port on the given card to the FluidSynth port.
**   - If card is negative, all devices match.
*/
static void do_connect(pimidid_t *pi, int card)
{
	pimidid_search_t s;
	memset(&s, 0, sizeof(s));
	s.pi = pi;
	s.card = (int)card;

	/* Search for FluidSynth. Re-do this every time incase it crashed and was restarted. */
	do_search_port(pi->seq, locate_fluidstynth, &s);
	if(!s.fluid_port)
		return;

	syslog(LOG_INFO,
		"Found  FLUID port at %3d:%d...\n",
		snd_seq_client_info_get_client(s.fluid_client),
		snd_seq_port_info_get_port(s.fluid_port)
	);
	do_search_port(pi->seq, locate_ports, &s);
}

static int caught_signal = 0;

static void sighandler(int signum)
{
	caught_signal = signum;
}

int main(int argc, char **argv)
{
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = sighandler;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, SIGTERM);
	sigaddset(&act.sa_mask, SIGINT);
	sigaddset(&act.sa_mask, SIGHUP);
	act.sa_restorer = NULL;

	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);

	openlog("pimidid", LOG_PERROR | LOG_PID, LOG_DAEMON);

	pimidid_t pi;
	if(pimidid_init(&pi) < 0)
	{
		syslog(LOG_ERR, "Initialisation failure");
		return 1;
	}

	snd_lib_error_set_handler(error_handler);

	do_connect(&pi, -1);
	for(;;)
	{
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(pi.monitor_fd, &fds);

		int ret = select(pi.monitor_fd + 1, &fds, NULL, NULL, NULL);
		if(ret < 0 && errno == EINTR)
		{
			if(caught_signal == SIGINT || caught_signal == SIGTERM)
				break;

			do_connect(&pi, -1);
		}
		else if(ret > 0 && FD_ISSET(pi.monitor_fd, &fds))
		{
			struct udev_device *dev = udev_monitor_receive_device(pi.monitor);
			if(!dev)
				continue;

			const char *action = udev_device_get_action(dev);
			if(strcmp("add", action) != 0)
				continue;

			const char *node = udev_device_get_devnode(dev);
			if(!node)
				continue;

			/* FIXME: Is here a better way to do this? */
			unsigned int card, dard;
			if(sscanf(node, "/dev/snd/midiC%uD%u", &card, &dard) != 2)
				continue;

			udev_device_unref(dev);

			do_connect(&pi, card);
		}
	}

	pimidid_deinit(&pi);
	snd_config_update_free_global();
	return 0;
}
