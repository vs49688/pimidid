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
#include "pimidid.h"
#include "rpi.h"

#define perm_ok(pinfo,bits) ((snd_seq_port_info_get_capability(pinfo) & (bits)) == (bits))

static int locate_internal_fluid(snd_seq_t *seq, snd_seq_client_info_t *cinfo, snd_seq_port_info_t *pinfo, void *user)
{
    PiMidiCtx *pi = user;
    const char *name;

    assert(pi->fluid_port == NULL && pi->fluid_client == NULL);

    /* FluidSynth will set both the client and portname to "midi.portname" */
    name = snd_seq_client_info_get_name(cinfo);
    if(!fluid_settings_str_equal(pi->fl_settings, "midi.portname", name))
        return 0;

    if(!perm_ok(pinfo, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
        return 0;

    name = snd_seq_port_info_get_name(pinfo);
    if(!fluid_settings_str_equal(pi->fl_settings, "midi.portname", name))
        return 0;


    if((snd_seq_client_info_malloc(&pi->fluid_client) < 0))
        return 0;

    if((snd_seq_port_info_malloc(&pi->fluid_port) < 0))
        return 0;

    snd_seq_client_info_copy(pi->fluid_client, cinfo);
    snd_seq_port_info_copy(pi->fluid_port, pinfo);
    return 1;
}

int pimidid_init(
    PiMidiCtx *pi,
    snd_ctl_t *ctl,
    const char *sf2,
    int cpu_cores,
    int period_size
)
{
    memset(pi, 0, sizeof(PiMidiCtx));

    pi->ctl = ctl;
    pi->monitor_fd = -1;

    if(!(pi->fl_settings = new_fluid_settings()))
        goto failure;

    if(fluid_settings_setint(pi->fl_settings, "synth.cpu-cores", cpu_cores) == FLUID_FAILED)
        goto failure;

    if(fluid_settings_setstr(pi->fl_settings, "midi.driver", "alsa_seq") == FLUID_FAILED)
        goto failure;

    snprintf(pi->fl_portname, sizeof(pi->fl_portname) - 1, "pimidid-%d", (int)getpid());
    pi->fl_portname[sizeof(pi->fl_portname) - 1] = '\0';
    if(fluid_settings_setstr(pi->fl_settings, "midi.portname", pi->fl_portname) == FLUID_FAILED)
        goto failure;

    if(fluid_settings_setint(pi->fl_settings, "audio.period-size", period_size) == FLUID_FAILED)
        goto failure;

    if(fluid_settings_setstr(pi->fl_settings, "audio.driver", "alsa") == FLUID_FAILED)
        goto failure;

    if(fluid_settings_setstr(pi->fl_settings, "audio.alsa.device", snd_ctl_name(pi->ctl)) == FLUID_FAILED)
        goto failure;

    if(!(pi->fl_synth = new_fluid_synth(pi->fl_settings)))
        goto failure;

    if(fluid_synth_sfload(pi->fl_synth, sf2, 1) == FLUID_FAILED)
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

    do_search_port(pi->seq, locate_internal_fluid, pi);

    if(pi->fluid_client == NULL || pi->fluid_port == NULL)
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

void pimidid_deinit(PiMidiCtx *pi)
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

    if(pi->fluid_client)
        snd_seq_client_info_free(pi->fluid_client);
    pi->fluid_client = NULL;

    if(pi->fluid_port)
        snd_seq_port_info_free(pi->fluid_port);
    pi->fluid_port = NULL;
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

void do_search_port(snd_seq_t *seq, PiMIDISearchProc proc, void *user)
{
    snd_seq_client_info_t *cinfo;
    snd_seq_port_info_t *pinfo;

    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while(snd_seq_query_next_client(seq, cinfo) >= 0)
    {
        /* reset query info */
        snd_seq_port_info_set_client(pinfo, snd_seq_client_info_get_client(cinfo));
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0)
        {
            if(snd_seq_port_info_get_capability(pinfo) & SND_SEQ_PORT_CAP_NO_EXPORT)
                continue;

            if(proc(seq, cinfo, pinfo, user))
                return;
        }
    }
}
