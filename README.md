# pimidid

Small daemon to automatically handle MIDI devices on a RPi3.

pimidid does the following:
* At startup:
  - Fix the audio routing (equivalent to `amixer cset numid=3 1`).
  - Spawn a FluidSynth instance and connects all readable ports.
* When a udev kernel add event from the sound subsystem is received,
  get the card id and connect all its readable ports.
* When a SIGHUP is received, connect all readable ports.

Specifically, I use this so I can attach headphones (via a Raspberry Pi) to an
[Oxygen 88](https://m-audio.com/products/view/oxygen-88).

## Usage
```
Usage: pimidid [OPTIONS] <soundfont>
Options:
  -h, --help       Display this message.

  -n, --nproc      FluidSynth's "synth.cpu-cores" property. Defaults to 1.

  -p, --period     FluidSynth's "audio.period-size" property. Defaults to 444.

  --device=DEVICE  Override the ALSA device string. Example: "hw:0".
                   If set, don't attempt autodetection.

  --route={-1,0,1,2,nochange,auto,35mm,hdmi}
                   Set the audio route. Defaults to "35mm".
                   Available values:
                   - -1, nochange = Don't attempt to change the audio route.
                   -  0, auto     = Route audio however the card decides.
                   -  1, 35mm     = Route audio over the 3.5mm jack.
                   -  2, hdmi     = Route audio over HDMI.
```

## Installation
This has only been used with OpenRC.
* Make sure you have a FluidSynth daemon running, with the name `fluidsynth`. Edit `pimidid.openrc` if you don't.
* Compile the application and stick it at `/usr/sbin/pimidid`
* Copy `pimidid.openrc` to `/etc/init.d/pimidid`
* Add the service: `rc-update add pimidid default`
* Start the service: `rc-service pimidid start`

## License

This project is licensed under the [GNU General Public License v2.0 only](https://spdx.org/licenses/GPL-2.0-only.html):

Copyright &copy; 2020 [Zane van Iperen](mailto:zane@zanevaniperen.com)

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; version 2.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA. 

* * *

This project uses portions of [`alsa-utils`](http://www.alsa-project.org/) which is licensed under the [GNU General Public License v2.0 or later](https://spdx.org/licenses/GPL-2.0-or-later.html).
