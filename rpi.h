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
#ifndef _RPI_H
#define _RPI_H

/* <alsa/asoundlib.h> */
struct _snd_ctl;
typedef struct _snd_ctl snd_ctl_t;

enum {
    RPI_AUDIO_ROUTE_AUTO = 0,
    RPI_AUDIO_ROUTE_35mm = 1,
    RPI_AUDIO_ROUTE_HDMI = 2,
};

/* Get the index of the BCM2835 analog output. */
int rpi_get_alsa_index(int *card);

/* snd_ctl_open() variant that takes an index. */
int rpi_snd_ctl_open_by_index(snd_ctl_t **ctl, int index, int mode);

/* Set the audio route of the BCM2835 analog output. */
int rpi_set_audio_route(snd_ctl_t *handle, int route);

#endif /* _RPI_H_ */
