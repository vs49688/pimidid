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

#include <errno.h>
#include <alsa/asoundlib.h>

#include "rpi.h"

int rpi_get_alsa_index(int *card)
{
    int err, card_ = -1;
    char *name = NULL;

    if(card == NULL)
        return -EINVAL;

    while((err = snd_card_next(&card_)) >= 0) {
        if(name) {
            free(name);
            name = NULL;
        }

        if(err < 0) {
            errno = err;
            SYSERR("snd_card_next(%d)", card_);
            return err;
        }

        if(card_ == -1)
            return -ENOENT;

        if((err = snd_card_get_name(card_, &name)) < 0) {
            errno = err;
            SYSERR("snd_card_get_name(%d)", card_);
            return err;
        }

        /* Explicitly skip this. */
        if(strcmp("vc4-hdmi", name) == 0)
            continue;

        if(strcmp("bcm2835 ALSA", name) == 0)
            break;
    }

    if(name)
        free(name);

    *card = card_;
    return 0;
}

int rpi_snd_ctl_open_by_index(snd_ctl_t **ctl, int index, int mode)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "hw:%d", index);
    buf[sizeof(buf) - 1] = '\0';
    return snd_ctl_open(ctl, buf, mode);
}

int rpi_set_audio_route(snd_ctl_t *handle, int route)
{
    int err;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_value_t *control;

    if(handle == NULL || route < RPI_AUDIO_ROUTE_MIN || route > RPI_AUDIO_ROUTE_MAX)
        return -EINVAL;

    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_value_alloca(&control);

    /*
     * NB: Not setting this, iirc it was different on the older Pis
     * Searching by name is fine.
     */
    //snd_ctl_elem_id_set_numid(id, 3);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, "PCM Playback Route");

    /* NB: Could also use */
    //err = snd_ctl_ascii_elem_id_parse(id, "numid=3,iface=MIXER,name='PCM Playback Route'");

    snd_ctl_elem_info_set_id(info, id);

    if((err = snd_ctl_elem_info(handle, info)) < 0) {
        errno = err;
        SYSERR("snd_ctl_elem_info()");
        return err;
    }

    snd_ctl_elem_info_get_id(info, id);
    snd_ctl_elem_value_set_id(control, id);
    snd_ctl_elem_value_set_integer(control, 0, route);

    if((err = snd_ctl_elem_write(handle, control)) < 0) {
        errno = err;
        SYSERR("snd_ctl_elem_write()");
        return err;
    }

    return 0;
}
