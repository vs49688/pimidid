/*
 * pimidid - Copyright (C) 2020 Zane van Iperen.
 *    Contact: zane@zanevaniperen.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, and only
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <alsa/asoundlib.h>
#include <libudev.h>
#include "pimidid.h"
#include "rpi.h"


/* Set to 1 to find our embedded FluidSynth's port. */
#define SEARCH_EMBEDDED_SYNTH 1

static void error_handler(const char *file, int line, const char *function, int err, const char *fmt, ...)
{
	const char *errs;
	FILE *fp;
	va_list arg;

	if(err) {
		errs = "error";
		fp = stderr;
	} else {
		errs = "info";
		fp = stdout;
	}

	fprintf(fp, "alsa: %s: %s:%i:(%s):", errs, file, line, function);
	va_start(arg, fmt);
	vfprintf(fp, fmt, arg);
	va_end(arg);

	if(err) {
		fprintf(fp, ": %s", snd_strerror(err));
	}

	fputc('\n', fp);
}

#define perm_ok(pinfo,bits) ((snd_seq_port_info_get_capability(pinfo) & (bits)) == (bits))

typedef struct pimidid_search
{
	pimidid_t *pi;

	int card;

	snd_seq_client_info_t *fluid_client;
	snd_seq_port_info_t *fluid_port;
} pimidid_search_t;

/* Try to find the fluidsynth port. */
static int locate_fluidstynth(snd_seq_t *seq, snd_seq_client_info_t *cinfo, snd_seq_port_info_t *pinfo, int count, void *user)
{
	pimidid_search_t *s = (pimidid_search_t*)user;

	if(s->fluid_port)
		return 0;

#if SEARCH_EMBEDDED_SYNTH
	/* FluidSynth will set both the client and portname to "midi.portname" */
	const char *name = snd_seq_client_info_get_name(cinfo);
	if(!fluid_settings_str_equal(s->pi->fl_settings, "midi.portname", name))
		return 0;
#else
	const char *name = snd_seq_client_info_get_name(cinfo);
	if(strstr(name, "FLUID") != name)
		return 0;
#endif

	if(!perm_ok(pinfo, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
		return 0;

	s->fluid_client = s->pi->_fluid_client;
	snd_seq_client_info_copy(s->fluid_client, cinfo);

	s->fluid_port = s->pi->_fluid_port;
	snd_seq_port_info_copy(s->fluid_port, pinfo);

#if SEARCH_EMBEDDED_SYNTH
	const char *pname = snd_seq_port_info_get_name(s->fluid_port);
	if(!fluid_settings_str_equal(s->pi->fl_settings, "midi.portname", pname))
		return 0;
#endif
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

	printf("pimidid: info: Found DEVICE port at %3d:%d, connecting to %d:%d...\n",
		snd_seq_client_info_get_client(cinfo),
		snd_seq_port_info_get_port(pinfo),
		snd_seq_client_info_get_client(s->fluid_client),
		snd_seq_port_info_get_port(s->fluid_port)
	);

	if(pimidid_connect(seq, pinfo, s->fluid_port) < 0)
		printf("pimidid: warning: Connection failed (%s)\n", snd_strerror(errno));

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

	printf("pimidid: info: Found  FLUID port at %3d:%d...\n",
		snd_seq_client_info_get_client(s.fluid_client),
		snd_seq_port_info_get_port(s.fluid_port)
	);
	do_search_port(pi->seq, locate_ports, &s);
}

volatile sig_atomic_t caught_signal = 0;

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

	printf("pimidid: info: starting up, pid = %d\n", (int)getpid());

	pimidid_t pi;
	if(pimidid_init(&pi) < 0)
	{
		fprintf(stderr, "pimidid: error: initialisation failure\n");
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
			int sig = caught_signal;
			caught_signal = 0;

			if(sig)
				printf("pimidid: trace: caught signal %d\n", sig);

			if(sig == SIGINT || sig == SIGTERM)
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
