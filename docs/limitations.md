# Limitations and plans

[Back to the README](../README.md)

## Limitations

- **Picture subtitles** from the SD card and a network folder: Blu-ray PGS
  and DVD VobSub inside an MKV, and a VobSub pair (`Film.idx` + `Film.sub`)
  next to the video, up to 8 languages, each read when first chosen. Not
  supported: PGS or VobSub in MP4, a `.sup` file beside the video, DVB
  subtitles, and a `.sub` over 96 MB. They're drawn as the disc has them
  (their own size, colors and place): the size, color and font options don't
  apply, and the subtitle delay does. From a server, the server still draws
  them on (it converts the video for that).
- **10-bit AV1** and **12-bit** HEVC can't be played.
- **HEVC** is decoded on the CPU: the console's decoder service, which
  [libounce](https://github.com/ouncebrew/libounce) uses, doesn't offer it,
  although the chip has it. 10-bit video is shown in 8 bits.
- **4K HEVC** is at the CPU's limit. Measured on the console, a 10-bit
  3840x1600 film at 16 Mb/s decodes at 28 frames per second (in real time for
  24 fps), but a 10-bit 4K remux at 68 Mb/s only at 18, so frames are skipped
  to keep up with the sound.
- **HDR** (HDR10, HLG) is brought down to SDR colors on the GPU (Settings >
  Playback > **HDR to SDR colors**). The picture reaches the GPU in 8 bits,
  so smooth skies can show some banding; HDR10+ and Dolby Vision's own data
  are not used (a Dolby Vision file with an HDR10 base plays as HDR10; one
  without, profile 5, keeps wrong colors). The pictures on the cards follow
  the setting as it was when they were made: turning it off or on later
  doesn't make them again. 4K HDR is decoded on the CPU like any
  4K HEVC (see below).
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
- **IPTV lists** have been tried with free channels from a public M3U list
  and with an Xtream test server on a PC, not with a paid provider. Channels
  that work only in their country don't play. Of the headers a channel may
  ask for, only Referer, User-Agent and Origin are sent. An Xtream account's film and series categories aren't used yet: its
  Films and Series list everything by title. IPTV films and series can't be
  downloaded.
- **Stremio addons** have been tried with a test addon on a PC. Only web
  streams play: torrents, magnet links, YouTube and outside links are always
  left out, and so are streams that need headers other than Referer,
  User-Agent and Origin. Each catalog
  shows its first page. Addons that need setting up do it on their own page
  first. Addon videos can't be downloaded.

## Plans

- Plex Home profiles.
- Smoother jumps, and checks with big libraries on the console.
- **Faster 4K** (Settings > Playback, "When needed" by default): frames
  nothing refers to skip their deblocking once a video over 1080p decoded on
  the CPU starts coming late. Measured on the console with `tools/hevctest`:
  about 5% faster (a 68 Mb/s 4K remux from 17.7 to 19.0 fps, a 16 Mb/s
  3840x1600 film from 27.9 to 29.2); on a PC, 8-9% on made-up 4K clips, with
  those frames at 50-54 dB PSNR (SSIM 0.997-0.999) against the normal
  picture. It helps a film that is just short of real time; it doesn't bring
  a 68 Mb/s remux to real time: letting the server convert it still does.
