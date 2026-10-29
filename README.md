# mm2astrisk

Universal ModemManager to Asterisk VoIP bridge written in C.

The program bridges cellular voice calls on Linux devices running ModemManager to an Asterisk PBX or external media server using the Asterisk AudioSocket protocol. It enables incoming calls on a cellular SIM card to be answered, authenticated, and streamed bidirectionally as raw linear PCM audio over a standard TCP connection.

## Overview

The daemon runs on Linux systems with cellular modems such as the PinePhone, Librem 5, Raspberry Pi with cellular expansion boards, or laptops with integrated WWAN modules. It communicates directly with ModemManager through libmm-glib on the system D-Bus to detect incoming calls, inspect caller identification, and control call states.

When an incoming call arrives, mm2astrisk checks the caller phone number against an authorized whitelist. If authorized, the daemon accepts the cellular call, establishes an AudioSocket connection to Asterisk, and starts capture and playback audio workers using ALSA. Terminating the call from either the cellular side or Asterisk tears down the session cleanly.

## Requirements

Building mm2astrisk requires GCC, GNU Make, pkg-config, ModemManager development libraries, GLib and GIO libraries, and ALSA libraries.

On Debian, Mobian, or Ubuntu systems, install the dependencies with:

```bash
sudo apt-get install -y build-essential pkg-config libmm-glib-dev libglib2.0-dev libasound2-dev
```

On Arch Linux ARM or Manjaro, install the dependencies with:

```bash
sudo pacman -S base-devel modemmanager glib2 alsa-lib
```

On Alpine or postmarketOS, install the dependencies with:

```bash
sudo apk add build-base pkgconf modemmanager-dev glib-dev alsa-lib-dev
```

## Compilation and Testing

Compile the program by running make in the root directory:

```bash
make
```

Run the unit test suite to verify configuration parsing and AudioSocket protocol framing:

```bash
make test
```

## Configuration

The program reads settings from an INI file named config.ini or a custom path passed via the command line.

The modem section defines the modem path or auto detection and the list of permitted telephone numbers separated by commas. The audio section configures ALSA capture and playback devices along with the sampling rate in Hertz. The server section defines the remote host address, port, and UUID string for the AudioSocket connection.

Example config.ini:

```ini
[modem]
modem_path = auto
whitelist = +989120000000
allow_all = false

[audio]
capture_device = default
playback_device = default
sample_rate = 8000

[server]
host = 127.0.0.1
port = 9092
uuid = 12345678-1234-5678-1234-567812345678

[general]
debug = false
```

## Usage

Scan the system for voice-capable modems to verify hardware detection:

```bash
./bin/mm2astrisk --scan
```

Run the daemon using the default configuration file:

```bash
./bin/mm2astrisk -c config.ini
```

Run the daemon in debug mode to inspect real-time call states:

```bash
./bin/mm2astrisk -c config.ini --debug
```

## System Service Installation

Install the compiled binary, configuration file, and systemd service to standard system locations:

```bash
sudo make install
sudo systemctl enable --now mm2astrisk
```

## License

This project is licensed under the GNU Affero General Public License version 3 (AGPL-3.0). See the LICENSE file for details.

