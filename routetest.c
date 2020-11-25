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

/*
 * [nix-shell:~/routetest]# aplay -l
 * **** List of PLAYBACK Hardware Devices ****
 * card 0: ALSA [bcm2835 ALSA], device 0: bcm2835 ALSA [bcm2835 ALSA]
 *   Subdevices: 7/7
 *   Subdevice #0: subdevice #0
 *   Subdevice #1: subdevice #1
 *   Subdevice #2: subdevice #2
 *   Subdevice #3: subdevice #3
 *   Subdevice #4: subdevice #4
 *   Subdevice #5: subdevice #5
 *   Subdevice #6: subdevice #6
 * card 0: ALSA [bcm2835 ALSA], device 1: bcm2835 IEC958/HDMI [bcm2835 IEC958/HDMI]
 *   Subdevices: 1/1
 *   Subdevice #0: subdevice #0
 *
 * [nix-shell:~/routetest]# aplay -L
 * null
 *     Discard all samples (playback) or generate zero samples (capture)
 * default:CARD=ALSA
 *     bcm2835 ALSA, bcm2835 ALSA
 *     Default Audio Device
 * sysdefault:CARD=ALSA
 *     bcm2835 ALSA, bcm2835 ALSA
 *     Default Audio Device
 *
 * [root@pianopi:~]# amixer controls
 * numid=3,iface=MIXER,name='PCM Playback Route'
 * numid=2,iface=MIXER,name='PCM Playback Switch'
 * numid=1,iface=MIXER,name='PCM Playback Volume'
 * numid=5,iface=PCM,name='IEC958 Playback Con Mask'
 * numid=4,iface=PCM,name='IEC958 Playback Default'
 *
 * [nix-shell:~/routetest]# aplay -l
 * **** List of PLAYBACK Hardware Devices ****
 * card 0: vc4hdmi [vc4-hdmi], device 0: MAI PCM vc4-hdmi-hifi-0 [MAI PCM vc4-hdmi-hifi-0]
 *   Subdevices: 1/1
 *   Subdevice #0: subdevice #0
 *
 * [nix-shell:~/routetest]# aplay -L
 * null
 *     Discard all samples (playback) or generate zero samples (capture)
 * default
 *     Default Audio Device
 * sysdefault
 *     Default Audio Device
 * iec958
 *     IEC958 (S/PDIF) Digital Audio Output
 * default:CARD=vc4hdmi
 *     vc4-hdmi, MAI PCM vc4-hdmi-hifi-0
 *     Default Audio Device
 * sysdefault:CARD=vc4hdmi
 *     vc4-hdmi, MAI PCM vc4-hdmi-hifi-0
 *     Default Audio Device
 * front:CARD=vc4hdmi,DEV=0
 *     vc4-hdmi, MAI PCM vc4-hdmi-hifi-0
 *     Front output / input
 * iec958:CARD=vc4hdmi,DEV=0
 *     vc4-hdmi, MAI PCM vc4-hdmi-hifi-0
 *     IEC958 (S/PDIF) Digital Audio Output
 */

int main(int argc, char **argv)
{
    int err, card;
    snd_ctl_t *handle;

    if((err = rpi_get_alsa_index(&card)) < 0) {
        fprintf(stderr, "Unable to find card: %d: %s\n", err, snd_strerror(err));
        return -1;
    }

    err = rpi_snd_ctl_open_by_index(&handle, card, 0);
    if(err < 0) {
        fprintf(stderr, "Control %s open error: %s\n", "default", snd_strerror(err));
        return err;
    }

    rpi_set_audio_route(handle, RPI_AUDIO_ROUTE_35mm);

    snd_config_update_free_global();
    return 0;
}
