# KaiVideo

An **open-source video player** for **Switch 2** running
[kaiHEN](https://github.com/ouncebrew/kaihen), in the spirit of
[KaiPlayer](https://github.com/mizuiroame/kaiplayer) and
[KaiRadio](https://github.com/mizuiroame/kairadio). Play the videos on your
microSD card with the console's own hardware video decoder.

> KaiVideo is an independent project. It is not affiliated with, endorsed by
> or sponsored by Nintendo. See the [Legal notice](#legal-notice).

## What it does

- Plays **MP4**, **M4V**, **MKV** and **WebM** files from your card.
- **Hardware video decoding** with the console's decoder: **H.264**, **VP9**,
  **AV1** and **VP8**, tried up to 1080p at 60 frames per second. A video the
  decoder can't play says so instead of stuttering.
- Sound with [FFmpeg](https://ffmpeg.org/): **AAC**, **MP3**, **Opus**,
  **Vorbis**, **FLAC**, **AC-3**, **E-AC-3** (Dolby Digital Plus), **DTS**
  and **Dolby TrueHD** (surround is mixed down to stereo).
- **Sound tracks**: a film with several (languages, commentary) lets you
  change between them with **X**, without stopping the picture.
- **Subtitles**: an `.srt` file next to the video (`Film.srt` or
  `Film.es.srt`), or the text subtitles inside the file (SubRip and ASS in
  MKV, mov_text in MP4, WebVTT). **Y** changes between them or turns them off.
- **Resume where you left off**: open a video you stopped halfway and it goes
  on from there (**ZL** starts it over). Videos you've started show how far
  you got in the list.
- A **file browser** for your card: folders first, then videos, with their
  sizes.
- Drawn on the GPU, sharp in handheld and docked. Every control works with the
  buttons and the touch screen.

## Install

You need a Switch 2 with **kaiHEN** running (launch a game while holding
**R** to open hbmenu), and a way to copy files to the card: OunceShell's FTP
server with an FTP client such as [FileZilla](https://filezilla-project.org/),
or [KaiDrop](https://github.com/mizuiroame/kaidrop).

1. Download `KaiVideo.nro` from the latest release on the
   **[Releases](../../releases)** page.
2. In hbmenu, open **OunceShell** and start its FTP server. It shows the
   console's address and port.
3. Connect to it from your PC, make a `kaivideo` folder inside the `switch`
   folder and copy `KaiVideo.nro` into it, so it is
   `sdmc:/switch/kaivideo/KaiVideo.nro`. (Straight in `switch` works too.)
4. Copy your videos anywhere on the card, for example a `Videos` folder.
5. Open hbmenu and select **KaiVideo**.

> **File names with accents or other non-ASCII characters** (á, ñ, ’,
> Japanese) are hidden by the console's file system, in every app. Rename
> those files, or upload them with [KaiDrop](https://github.com/mizuiroame/kaidrop),
> which gives the copies plain names.

> **Don't take the microSD card out while kaiHEN is running.** Removing it
> means restarting the console, and kaiHEN has to be started again after
> that.

### Where KaiVideo keeps its files

The folder you were in, where you left each video and a log are kept in a
`config` folder next to `KaiVideo.nro`, in its own folder:
`sdmc:/switch/kaivideo/config/` (or `sdmc:/brewstore/apps/kaivideo/config/`
when brewstore installs it). Your videos aren't touched.

## Controls

**In the list:**

| Button | What it does |
|--------|--------------|
| **↑** / **↓** | Move |
| **◀** / **▶** | A page up or down |
| **A** | Open the folder, or play the video |
| **B** | The folder above |
| **+** | Exit |

Tap a row to open it, drag the list to scroll it, and tap the path at the
top for the folder above.

**While a video plays:**

| Button | What it does |
|--------|--------------|
| **A** | Pause or play |
| **◀** / **▶** | Back or forward 10 seconds |
| **L** / **R** | Back or forward 1 minute |
| **ZL** | Start over |
| **X** | The next sound track |
| **Y** | The next subtitles, or none |
| **B** / **+** | Back to the list (where you were is remembered) |

Any button or tap shows the bar for a few seconds. With the bar showing, tap
the picture or the play button to pause or play, the progress bar to go
there, **CC** for the next subtitles, **Audio** for the next sound track, and
the **×** at the top left to go back to the list.

## Limitations

- **Picture subtitles** (DVD and Blu-ray ones, in MKV) aren't shown; text ones
  are.
- **10-bit** H.264 (Hi10P) and 10-bit AV1 videos can't be played.
- **HEVC (H.265)** videos can't be played: the decoder interface KaiVideo
  uses ([libounce](https://github.com/ouncebrew/libounce)) offers H.264, VP9,
  AV1 and VP8.
- Not tried with 4K videos or HDR yet.
- No streaming from the network: KaiVideo plays files on the card.

## Building

KaiVideo is built in Docker, on top of the
[ounce-devkit](https://github.com/ouncebrew/ounce-devkit) image:

```sh
docker build -t kaivideo-devkit ffmpeg   # once: the devkit plus FFmpeg (LGPL)
docker run --rm -v "$PWD:/w" -w /w kaivideo-devkit make          # KaiVideo.nro
docker run --rm -v "$PWD:/w" -w /w/tests kairadio-host make run  # host tests
```

`ffmpeg/Dockerfile` builds FFmpeg 7.1 from its release tarball (checked by
SHA-256) with only the demuxers, parsers, bitstream filters and audio and
subtitle decoders KaiVideo needs, and fails if the build would include GPL or
non-free parts. The video itself is decoded by the console, through
[libounce](https://github.com/ouncebrew/libounce)'s `VideoDecoder`.

`tools/make_icon.py` draws `icon.jpg`.

## Legal notice

- **Independent project.** KaiVideo is not affiliated with, endorsed by or
  sponsored by Nintendo Co., Ltd. or its subsidiaries.
- **Trademarks.** "Nintendo", "Nintendo Switch" and "Switch 2" are trademarks
  of their owners. They are used here only to say which hardware this program
  works with. Format names are used only to name the files KaiVideo plays.
- **No Nintendo material.** This repository and its releases contain no
  Nintendo code, SDK, firmware, cryptographic keys, games, or graphics or audio
  assets. KaiVideo is built only with open-source tools and libraries.
- **No circumvention.** KaiVideo does not circumvent any technical protection
  measure. It doesn't play DRM-protected video, and it doesn't download from
  streaming sites. It plays the files that users put on their own card.
- **FFmpeg.** KaiVideo uses FFmpeg, built under the LGPL 2.1 (no GPL or
  non-free parts). The script that builds it is in this repository, so it can
  be rebuilt, changed and relinked.
- **User responsibility.** Only play videos you have the right to watch.
  Running homebrew (kaiHEN) can put your console at risk, can affect its
  warranty and can go against the manufacturer's terms of service. You do it at
  your own risk.
- **No warranty.** The software is provided "as is", without warranty of any
  kind. See [LICENSE](LICENSE).

This notice is not legal advice.

## License

KaiVideo is released under the [MIT License](LICENSE). FFmpeg is released
under the LGPL 2.1 ([third_party/FFmpeg-LGPL-2.1.txt](third_party/FFmpeg-LGPL-2.1.txt)).
The fonts are Noto Sans, under the SIL Open Font License
([third_party/NotoSans-OFL.txt](third_party/NotoSans-OFL.txt)), and images and
fonts are read with [stb](https://github.com/nothings/stb) (public domain).
