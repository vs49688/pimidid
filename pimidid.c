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
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <alsa/asoundlib.h>
#include <libudev.h>
#include "pimidid.h"
#include "rpi.h"
#include "parg.h"

static struct parg_option argdefs[] = {
    {"nproc",     PARG_REQARG, NULL, 'n'},
    {"period",    PARG_REQARG, NULL, 'p'},
    {"device",    PARG_REQARG, NULL, 'd'},
    {"route",     PARG_REQARG, NULL, 'r'},
    {"help",      PARG_NOARG,  NULL, 'h'}
};

typedef struct PiMidiArgs
{
    const char *soundfont;
    int        nproc;
    int        period;
    const char *device;
    int        route;
} PiMidiArgs;

static const char *USAGE_OPTIONS =
    "Options:\n"
    "  -h, --help       Display this message.\n"
    "\n"
    "  -n, --nproc      FluidSynth's \"synth.cpu-cores\" property. Defaults to 1.\n"
    "\n"
    "  -p, --period     FluidSynth's \"audio.period-size\" property. Defaults to 444.\n"
    "\n"
    "  --device=DEVICE  Override the ALSA device string. Example: \"hw:0\".\n"
    "                   If set, don't attempt autodetection.\n"
    "\n"
    "  --route={-1,0,1,2,nochange,auto,35mm,hdmi}\n"
    "                   Set the audio route. Defaults to \"35mm\".\n"
    "                   Available values:\n"
    "                   - -1, nochange = Don't attempt to change the audio route.\n"
    "                   -  0, auto     = Route audio however the card decides.\n"
    "                   -  1, 35mm     = Route audio over the 3.5mm jack.\n"
    "                   -  2, hdmi     = Route audio over HDMI.\n"
    ;

static int parse_args(int argc, char **argv, PiMidiArgs *args)
{
    int has_nproc = 0, has_period = 0, has_route = 0;
    unsigned long nproc  = 1;
    unsigned long period = 444; /* This seems enforced on RPi3. */
    long          route  = RPI_AUDIO_ROUTE_35mm;
    struct parg_state ps;

    parg_init(&ps);
    args->soundfont  = NULL;
    args->period     = 0;
    args->nproc      = 0;
    args->device     = NULL;
    args->route      = 0;

    for(int c; (c = parg_getopt_long(&ps, argc, argv, "f:n:p:h", argdefs, NULL)) != -1; ) {
        switch(c) {
            case 1:
                if(args->soundfont != NULL)
                    goto usage;
                args->soundfont = ps.optarg;
                break;

            case 'n':
                if(has_nproc)
                    goto usage;
                errno = 0;
                nproc = strtoull(ps.optarg, NULL, 10);
                if(errno == ERANGE || nproc > INT_MAX)
                    goto usage;

                has_nproc = 1;
                break;

            case 'p':
                if(has_period)
                    goto usage;
                errno = 0;
                period = strtoul(ps.optarg, NULL, 10);
                if(errno == ERANGE || period > INT_MAX)
                    goto usage;

                has_period = 1;
                break;

            case 'd':
                if(args->device != NULL)
                    goto usage;
                args->device = ps.optarg;
                break;

            case 'r':
                if(has_route)
                    goto usage;

                if(strcmp("auto", ps.optarg) == 0) {
                    route = RPI_AUDIO_ROUTE_AUTO;
                } else if(strcmp("35mm", ps.optarg) == 0) {
                    route = RPI_AUDIO_ROUTE_35mm;
                } else if(strcmp("hdmi", ps.optarg) == 0) {
                    route = RPI_AUDIO_ROUTE_HDMI;
                } else if(strcmp("nochange", ps.optarg) == 0) {
                    route = -1;
                } else {
                    errno = 0;
                    route = strtol(ps.optarg, NULL, 10);
                    if(errno == ERANGE || route < (RPI_AUDIO_ROUTE_MIN - 1) || route > RPI_AUDIO_ROUTE_MAX)
                        goto usage;
                }

                has_route = 1;
                break;

            case 'h':
            default:
                goto usage;
        }
    }

    if(args->soundfont == NULL)
        goto usage;

    args->nproc = (int)nproc;
    args->period = (int)period;
    args->route  = (int)route;
    return 0;

usage:
    fprintf(stderr, "Usage: %s [OPTIONS] <soundfont>\n", argv[0]);
    fputs(USAGE_OPTIONS, stderr);
    return -1;
}

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

    if(err)
        fprintf(fp, ": %s", snd_strerror(err));

    fputc('\n', fp);
}

#define perm_ok(pinfo,bits) ((snd_seq_port_info_get_capability(pinfo) & (bits)) == (bits))

typedef struct pimidid_search
{
    PiMIDICtx *pi;

    int card;

    snd_seq_client_info_t *fluid_client;
    snd_seq_port_info_t *fluid_port;
} pimidid_search_t;

/* Try to find the fluidsynth port. */
static int locate_fluidstynth(snd_seq_t *seq, snd_seq_client_info_t *cinfo, snd_seq_port_info_t *pinfo, void *user)
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

static int locate_ports(snd_seq_t *seq, snd_seq_client_info_t *cinfo, snd_seq_port_info_t *pinfo, void *user)
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
static void do_connect(PiMIDICtx *pi, int card)
{
    pimidid_search_t s = {
        .pi           = pi,
        .card         = card,
        .fluid_client = NULL,
        .fluid_port   = NULL,
    };

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

static snd_ctl_t *open_card(const char *device, int route)
{
    int err, card;
    snd_ctl_t *handle;

    if(device == NULL) {
        if((err = rpi_get_alsa_index(&card)) < 0) {
            fprintf(stderr, "pimidid: error: unable to locate BCM2835 card: %s\n", snd_strerror(err));
            return NULL;
        }

        printf("pimidid: info: found BCM2835 card at hw:%d\n", card);

        if((err = rpi_snd_ctl_open_by_index(&handle, card, 0)) < 0) {
            fprintf(stderr, "pimidid: error: unable to open hw:%d: %s\n", card, snd_strerror(err));
            return NULL;
        }
    } else {
        if((err = snd_ctl_open(&handle, device, 0)) < 0) {
            fprintf(stderr, "pimidid: error: unable to open %s: %s\n", device, snd_strerror(err));
            return NULL;
        }
    }

    if(route < RPI_AUDIO_ROUTE_MIN)
        return handle;

    if((err = rpi_set_audio_route(handle, route)) < 0) {
        fprintf(stderr, "pimidid: error: error configuring audio route: %s\n", snd_strerror(err));
        return NULL;
    }

    return handle;
}

int main(int argc, char **argv)
{
    PiMidiArgs args;
    PiMIDICtx pi;
    struct sigaction act;
    snd_ctl_t *handle;

    if(parse_args(argc, argv, &args) < 0)
        return 2;
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

    if((handle = open_card(args.device, args.route)) == NULL)
        return 1;

    if(pimidid_init(&pi, handle, args.soundfont, args.nproc, args.period) < 0) {
        fprintf(stderr, "pimidid: error: initialisation failure\n");
        snd_ctl_close(handle);
        return 1;
    }

    snd_lib_error_set_handler(error_handler);

    do_connect(&pi, -1);
    for(int ret;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(pi.monitor_fd, &fds);

        ret = select(pi.monitor_fd + 1, &fds, NULL, NULL, NULL);
        if(ret < 0 && errno == EINTR) {
            int sig = caught_signal;
            caught_signal = 0;

            if(sig)
                printf("pimidid: trace: caught signal %d\n", sig);

            if(sig == SIGINT || sig == SIGTERM)
                break;

            do_connect(&pi, -1);
        } else if(ret > 0 && FD_ISSET(pi.monitor_fd, &fds)) {
            struct udev_device *dev;
            const char *node;
            unsigned int card;

            if(!(dev = udev_monitor_receive_device(pi.monitor)))
                continue;

            if(strcmp("add", udev_device_get_action(dev)) != 0)
                continue;

            if(!(node = udev_device_get_devnode(dev)))
                continue;

            /* FIXME: Is here a better way to do this? */
            if(sscanf(node, "/dev/snd/midiC%uD%*u", &card) != 2)
                continue;

            udev_device_unref(dev);

            do_connect(&pi, card);
        }
    }

    pimidid_deinit(&pi);
    snd_ctl_close(handle);
    snd_config_update_free_global();
    return 0;
}
