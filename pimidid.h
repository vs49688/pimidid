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
#ifndef _PIMIDID_H
#define _PIMIDID_H

#include <libudev.h>
#include <alsa/asoundlib.h>
#include <fluidsynth.h>

typedef struct PiMidiCtx
{
    snd_ctl_t *ctl;
    snd_seq_t *seq;

    struct udev *udev;
    struct udev_monitor *monitor;
    int monitor_fd;

    char 				 fl_portname[64];
    fluid_settings_t     *fl_settings;
    fluid_synth_t        *fl_synth;
    fluid_midi_driver_t  *fl_mdriver;
    fluid_audio_driver_t *fl_adriver;

    snd_seq_client_info_t *fluid_client;
    snd_seq_port_info_t   *fluid_port;
} PiMidiCtx;

int pimidid_init(
    PiMidiCtx *pi,
    snd_ctl_t *ctl,
    const char *sf2,
    int cpu_cores,
    int period_size
);
void pimidid_deinit(PiMidiCtx *pi);
int pimidid_connect(snd_seq_t *seq, snd_seq_port_info_t *source, snd_seq_port_info_t *sink);

typedef int (*PiMIDISearchProc)(snd_seq_t *seq, snd_seq_client_info_t *cinfo, snd_seq_port_info_t *pinfo, void *user);
void do_search_port(snd_seq_t *seq, PiMIDISearchProc proc, void *user);

#endif /* PIMIDID_H */
