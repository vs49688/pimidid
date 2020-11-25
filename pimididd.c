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
#include "pimidid.h"

#define SOUNDFONT "/nix/store/djlzrj8xhdmxf4sq99pfxn8k7k9p85hr-Fluid-3/share/soundfonts/FluidR3_GM2-2.sf2"

int pimidid_init(pimidid_t *pi)
{
	memset(pi, 0, sizeof(pimidid_t));
	pi->monitor_fd = -1;

	/* Alloc storage. */
	if((snd_seq_client_info_malloc(&pi->_fluid_client) < 0))
		goto failure;

	if((snd_seq_port_info_malloc(&pi->_fluid_port) < 0))
		goto failure;

	if(!(pi->fl_settings = new_fluid_settings()))
		goto failure;

	/* TODO: Make a command-line parameter */
	if(fluid_settings_setint(pi->fl_settings, "synth.cpu-cores", 4) == FLUID_FAILED)
		goto failure;

	if(fluid_settings_setstr(pi->fl_settings, "midi.driver", "alsa_seq") == FLUID_FAILED)
		goto failure;

	snprintf(pi->fl_portname, sizeof(pi->fl_portname) - 1, "pimidid-%d", (int)getpid());
	pi->fl_portname[sizeof(pi->fl_portname) - 1] = '\0';
	if(fluid_settings_setstr(pi->fl_settings, "midi.portname", pi->fl_portname) == FLUID_FAILED)
		goto failure;

	if(fluid_settings_setstr(pi->fl_settings, "audio.driver", "alsa") == FLUID_FAILED)
		goto failure;

	if(!(pi->fl_synth = new_fluid_synth(pi->fl_settings)))
		goto failure;

	/* TODO: Make a command-line parameter */
	if(fluid_synth_sfload(pi->fl_synth, SOUNDFONT, 1) == FLUID_FAILED)
		goto failure;

	if(snd_seq_open(&pi->seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
		goto failure;

	/* Create an external input port. Send all events to our synth. */
	pi->fl_mdriver = new_fluid_midi_driver(
		pi->fl_settings,
		fluid_synth_handle_midi_event,
		pi->fl_synth
	);

	if(pi->fl_mdriver == NULL)
		goto failure;

	if((pi->fl_adriver = new_fluid_audio_driver(pi->fl_settings, pi->fl_synth)) == NULL)
		goto failure;

	/* Setup udev. */
	if(!(pi->udev = udev_new()))
		goto failure;

	if(!(pi->monitor = udev_monitor_new_from_netlink(pi->udev, "kernel")))
		goto failure;

	if(udev_monitor_filter_add_match_subsystem_devtype(pi->monitor, "sound", NULL) < 0)
		goto failure;

	if(udev_monitor_enable_receiving(pi->monitor) < 0)
		goto failure;

	if((pi->monitor_fd = udev_monitor_get_fd(pi->monitor)) < 0)
		goto failure;

	return 0;
failure:
	pimidid_deinit(pi);
	return -1;
}

void pimidid_deinit(pimidid_t *pi)
{
	if(!pi)
		return;

	if(pi->monitor)
		udev_monitor_unref(pi->monitor);
	pi->monitor = NULL;
	pi->monitor_fd = -1;

	if(pi->udev)
		udev_unref(pi->udev);
	pi->udev = NULL;


	if(pi->fl_adriver)
		delete_fluid_audio_driver(pi->fl_adriver);
	pi->fl_adriver = NULL;

	if(pi->fl_mdriver)
		delete_fluid_midi_driver(pi->fl_mdriver);
	pi->fl_mdriver = NULL;

	if(pi->fl_synth)
		delete_fluid_synth(pi->fl_synth);
	pi->fl_synth = NULL;

	if(pi->fl_settings)
		delete_fluid_settings(pi->fl_settings);
	pi->fl_settings = NULL;


	if(pi->seq)
		snd_seq_close(pi->seq);
	pi->seq = NULL;

	if(pi->_fluid_client)
		snd_seq_client_info_free(pi->_fluid_client);
	pi->_fluid_client = NULL;

	if(pi->_fluid_port)
		snd_seq_port_info_free(pi->_fluid_port);
	pi->_fluid_port = NULL;
}

int pimidid_connect(snd_seq_t *seq, snd_seq_port_info_t *source, snd_seq_port_info_t *sink)
{
	const snd_seq_addr_t *sink_addr = snd_seq_port_info_get_addr(sink);
	const snd_seq_addr_t *source_addr = snd_seq_port_info_get_addr(source);

	snd_seq_port_subscribe_t *subs;
	snd_seq_port_subscribe_alloca(&subs);
	snd_seq_port_subscribe_set_sender(subs, source_addr);
	snd_seq_port_subscribe_set_dest(subs, sink_addr);
	snd_seq_port_subscribe_set_queue(subs, 0);
	snd_seq_port_subscribe_set_exclusive(subs, 0);
	snd_seq_port_subscribe_set_time_update(subs, 0);
	snd_seq_port_subscribe_set_time_real(subs, 0);

	if(snd_seq_get_port_subscription(seq, subs) == 0)
		return 0;

	return snd_seq_subscribe_port(seq, subs);
}

void do_search_port(snd_seq_t *seq, action_func_t do_action, void *user)
{
	snd_seq_client_info_t *cinfo;
	snd_seq_port_info_t *pinfo;
	int count;

	snd_seq_client_info_alloca(&cinfo);
	snd_seq_port_info_alloca(&pinfo);

	snd_seq_client_info_set_client(cinfo, -1);
	while(snd_seq_query_next_client(seq, cinfo) >= 0)
	{
		/* reset query info */
		snd_seq_port_info_set_client(pinfo, snd_seq_client_info_get_client(cinfo));
		snd_seq_port_info_set_port(pinfo, -1);
		count = 0;
		while (snd_seq_query_next_port(seq, pinfo) >= 0)
		{
			if(snd_seq_port_info_get_capability(pinfo) & SND_SEQ_PORT_CAP_NO_EXPORT)
				continue;

			if(do_action(seq, cinfo, pinfo, count, user))
				return;
			count++;
		}
	}
}
