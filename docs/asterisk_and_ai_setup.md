# Asterisk PBX and mm2astrisk Setup Guide

This guide details configuring Asterisk PBX to receive cellular audio from mm2astrisk via AudioSocket.

## 1. Asterisk Dialplan Configuration

Asterisk AudioSocket enables bidirectional raw 16-bit linear PCM audio streaming over a standard TCP connection.

Add the following context to your Asterisk `extensions.conf`:

```ini
[from-cellular-bridge]
exten => _.,1,NoOp(Incoming cellular call bridged from mm2astrisk)
 same => n,Answer()
 same => n,AudioSocket(12345678-1234-5678-1234-567812345678,127.0.0.1:9092)
 same => n,Hangup()
```

When an incoming cellular call is accepted by mm2astrisk, it connects to Asterisk on the configured host and port, establishing real-time audio transmission.

## 2. PinePhone Audio Configuration

On Linux phones such as PinePhone or Librem 5, audio during a voice call is routed through ALSA Use Case Manager (UCM) or PipeWire.

Verify available audio devices using standard ALSA utilities:

```bash
arecord -l
aplay -l
```

Setting `capture_device = default` and `playback_device = default` in `config.ini` routes audio through the active PipeWire or ALSA call session. Specific hardware devices can be defined when using dedicated USB modems.
