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
#ifndef _PIMIDID_H
#define _PIMIDID_H

#include <libudev.h>
#include <alsa/asoundlib.h>

typedef struct pimidid
{
	snd_seq_t *seq;

	struct udev *udev;
	struct udev_monitor *monitor;
	int monitor_fd;

	/* Storage */
	snd_seq_client_info_t *_fluid_client;
	snd_seq_port_info_t *_fluid_port;
} pimidid_t;

int pimidid_init(pimidid_t *pi);
void pimidid_deinit(pimidid_t *pi);
int pimidid_connect(snd_seq_t *seq, snd_seq_port_info_t *source, snd_seq_port_info_t *sink);

typedef int (*action_func_t)(snd_seq_t *seq, snd_seq_client_info_t *cinfo, snd_seq_port_info_t *pinfo, int count, void *user);
void do_search_port(snd_seq_t *seq, action_func_t do_action, void *user);

#endif /* PIMIDID_H */
