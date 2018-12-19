# pimidid

Small daemon to automatically connect MIDI devices to FluidSynth.

pimidid does the following:
* At startup, connect all readable ports.
* When a udev kernel add event from the sound subsystem is received,
  get the card id and connect all its readable ports.
* When a SIGHUP is received, connect all readable ports.

Before each of the above conditions, rescan for the FluidSynth port in case its id has changed.

## Usage
```
pimidid
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

Copyright &copy; 2018 [Zane van Iperen](mailto:zane@zanevaniperen.com)

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; version 2.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA. 

* * *

This project uses portions of [`alsa-utils`](http://www.alsa-project.org/) which is licensed under the [GNU General Public License v2.0 or later](https://spdx.org/licenses/GPL-2.0-or-later.html).
