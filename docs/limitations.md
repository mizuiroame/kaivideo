# Limitations and plans

[Back to the README](../README.md)

## Limitations

- **Picture subtitles** (DVD and Blu-ray ones, in MKV) aren't shown from the
  SD card or a network folder; text ones are. From a server, the server draws
  them on.
- **10-bit AV1** and **12-bit** HEVC can't be played.
- **HEVC** is decoded on the CPU: the console's decoder service, which
  [libounce](https://github.com/ouncebrew/libounce) uses, doesn't offer it,
  although the chip has it. 10-bit video is shown in 8 bits.
- **4K HEVC** is too heavy for the CPU: measured on the console, 10-bit 4K
  decoded at 21 frames per second at 16 Mb/s and at 15 at 68 Mb/s, so frames
  are skipped to keep up with the sound.
- **HDR** on the SD card or a network folder isn't converted: HDR videos
  play, with washed-out colors. From a server, the server converts them.
- **Big libraries**: tried with a few dozen videos on the console (and with
  thousands of names on a PC). With hundreds of videos, the first scan and
  the first round of pictures take longer.
- After a jump (**◀** / **▶**, **L** / **R**, the progress bar), the picture
  can stutter for a moment.
- **Media servers**: converted videos take a few seconds, rather than
  starting at once, to start, to change quality or picture subtitles, and to
  jump far back, while the server converts from there. Their text subtitles
  come without styles. Jellyfin has been tried on a home network, and over
  https through a test proxy with a certificate of its own. Emby and Plex
  have been tried with test servers on a home network. None has been tried
  from outside a home network yet.
- **Network folders**:
  - pictures for the cards come slowly the first time;
  - opening a folder waits for the server;
  - `.srt` files next to a video aren't loaded yet;
  - tried on a home network only. See [Network folders](network-folder.md).
- **Downloads**:
  - keep one subtitle file a video (the one its server shows first, else the
    first);
  - picture subtitles (PGS, VobSub) don't come with a converted one;
  - wait while a video streams from a server;
  - after the network comes back, where you left a downloaded episode offline
    may only show in the library once you open it.

  Downloads have been tried on the console with a home Jellyfin server, and
  with Emby and Plex test servers from a PC.
- **Live TV** has been tried with test channels a server made from video
  files and with free channels' own streams, not with a TV tuner. A channel
  the server converts keeps one sound track. Programs can't be recorded.

## Plans

- `.srt` files from network folders, and pictures from `poster.jpg` or
  `folder.jpg` when a folder has one.
- **Picture subtitles** (PGS, VobSub) from the SD card.
- Smoother jumps, and checks with big libraries on the console.
