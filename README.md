# KaiVideo

An **open-source video player** for **Switch 2** running
[kaiHEN](https://github.com/ouncebrew/kaihen), in the spirit of
[KaiPlayer](https://github.com/mizuiroame/kaiplayer) and
[KaiRadio](https://github.com/mizuiroame/kairadio). Play the videos on your
microSD card with the console's own hardware video decoder.

![Home](docs/screenshots/home.png)

| A series | Next episode | Subtitles |
|----------|--------------|-----------|
| ![A series' page](docs/screenshots/series.png) | ![The next episode's card](docs/screenshots/up_next.png) | ![The subtitles list](docs/screenshots/subtitles.png) |

| Films | Settings |
|-------|----------|
| ![Films](docs/screenshots/films.png) | ![Settings](docs/screenshots/settings.png) |

The screenshots show made-up films and series.

> KaiVideo is an independent project. It is not affiliated with, endorsed by
> or sponsored by Nintendo. See the [Legal notice](#legal-notice).

## What it does

- A **library** of your **films and series**: choose the folder with your
  videos once, and KaiVideo finds them in it and its folders. **Home** shows
  what you were watching, what's newest, your series and your films; **Films**
  and **Series** show them all, a series opens on its seasons and episodes,
  and **Search** finds a title by name. Cards show a **picture from the
  video**, taken by the console's decoder in the background the first time.
- **Settings**: themes and accent colors, your own **background** image,
  and an About page.
- Plays **MP4**, **M4V**, **MKV** and **WebM** files from your card.
- **Hardware video decoding** with the console's decoder: **H.264**, **VP9**,
  **AV1** and **VP8**, tried up to 1080p at 60 frames per second.
- **HEVC (H.265)**, 8-bit and 10-bit, and **10-bit H.264**, decoded on the CPU
  with FFmpeg: the console's decoder interface doesn't offer them. Measured on
  the console, a 1080p 10-bit episode decodes at several times its speed. It
  uses more battery than hardware decoding. A jump shows the nearest picture
  at once, while the exact place is decoded.
- Sound with [FFmpeg](https://ffmpeg.org/): **AAC**, **MP3**, **Opus**,
  **Vorbis**, **FLAC**, **AC-3**, **E-AC-3** (Dolby Digital Plus), **DTS**
  and **Dolby TrueHD** (surround is mixed down to stereo).
- **Sound tracks**: a film with several (languages, commentary) lets you
  pick one from a list (**X**), without stopping the picture.
- **Subtitles**: an `.srt` file next to the video (`Film.srt` or
  `Film.es.srt`), or the text subtitles inside the file (SubRip and ASS in
  MKV, mov_text in MP4, WebVTT), picked from a list (**Y**), or none. Their
  size, color and style (outline or a dark box) are at the end of that list.
- **ASS subtitles keep their look**, drawn by [libass](https://github.com/libass/libass):
  colors, positions, rotation, movement, fades and the fonts inside the file.
  The list offers only their size.
- **Playback list** (**↑**): the video's **chapters** (also marked on the
  progress bar), the **speed** from 0.5x to 2x with the voices' pitch kept,
  and **audio** and **subtitle delays** for a file that's out of sync.
- **Resume where you left off**: open a video you stopped halfway and it goes
  on from there (**ZL** starts it over). Videos you've started show how far
  you got.
- **Next episode**: in an episode's last 30 seconds (the credits, usually) a
  card on the right offers the next one; it plays on its own at the end, or
  at once with **ZR**. The **×** on the card lets the credits run.
- **Watched episodes** get a check mark, set when you finish one or by hand
  (**X**, or tap the circle). **Continue watching** on Home suggests each
  series' next episode.
- **Your sound track and subtitles are remembered**, for each video and for
  the rest of its series.
- A **file browser** for the whole card (the **Files** tab): folders first,
  then videos, with their sizes.
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
4. Copy your videos to a folder on the card, for example `Videos`.
5. Open hbmenu and select **KaiVideo**. The first time, choose the folder with
   your videos: open it and press **X** (or tap **Use this folder**).

> **File names with accents or other non-ASCII characters** (á, ñ, ’,
> Japanese) are hidden by the console's file system, in every app. Rename
> those files, or upload them with [KaiDrop](https://github.com/mizuiroame/kaidrop),
> which gives the copies plain names.

### Settings

Press **−** (or tap **Settings**) for:

- **Library folder** and **Look for new videos**, after you copy more.
- **Play next episode**: on (the card in the last 30 seconds, and the next
  episode at the end), or off.
- **Theme** (Dark, Black, Midnight, Plum) and **Accent color**.
- **Background**: put JPEG or PNG images in a `Backgrounds` folder in your
  library folder and pick one; it's blurred (**Background blur**) and
  darkened so text stays readable.
- **Thumbnails**: pictures from the videos on their cards, or plain colors.
  They're made a few at a time while nothing plays, and kept in
  `config/thumbs`. Videos KaiVideo can't play keep their color.

### How films and series are found

KaiVideo goes by file and folder names, the way most media libraries are
kept:

- **Series**: files named with the season and episode, like
  `Rook.S01E03.Lamentia.1080p.mkv` or `Rook 1x03.mkv` (or `Rook - 03` for
  anime). A folder named after the series (`TV/Rook/`, `Rook/Season 1/`) gives
  it its name; otherwise the file's name does.
- **Films**: every other video. A `Title (Year)` folder gives the film its
  name (`Movies/Paper Lanterns (1998)/anything.mkv`); otherwise the file's name
  does, without tags like `1080p`, `BluRay` or `x264`.

> **Don't take the microSD card out while kaiHEN is running.** Removing it
> means restarting the console, and kaiHEN has to be started again after
> that.

### Where KaiVideo keeps its files

Your library folder, the list of videos found in it, where you left each
video, what you've watched, the sound tracks and subtitles you chose and a
log are kept in a `config` folder next to `KaiVideo.nro`, in its own folder:
`sdmc:/switch/kaivideo/config/` (or `sdmc:/brewstore/apps/kaivideo/config/`
when brewstore installs it). Your videos aren't touched.

## Controls

**In the library:**

| Button | What it does |
|--------|--------------|
| **ZL** / **ZR** (or **L** / **R**) | The tab before or after: Home, Films, Series, Search, Files |
| **↑** **↓** **◀** **▶** | Move |
| **A** | Play the film or episode, or open the series |
| **B** | Back from a series (in Files: the folder above) |
| **Y** | Search |
| **−** | Settings |
| **+** | The menu: Look for new videos, Settings, Exit (**+** again asks to exit, and **+** once more exits) |

On a series, **◀** / **▶** change the season and **X** marks the episode
watched or not (or tap its circle on the right). Tap a tab, a card or a row to
open it, and drag to scroll; the **☰** before the name opens the menu. In
**Files**, **◀** / **▶** move a page and the path at the top goes to the
folder above. The line at the bottom of the screen always says which buttons
do what, as in KaiPlayer and KaiRadio.

**While a video plays:**

| Button | What it does |
|--------|--------------|
| **A** | Pause or play |
| **◀** / **▶** | Back or forward 10 seconds |
| **L** / **R** | Back or forward 1 minute |
| **ZL** | Start over |
| **↑** | The Playback list: chapters, speed, audio and subtitle delays |
| **X** | The list of sound tracks |
| **Y** | The list of subtitles |
| **−** | Hide the bar (**−** again, or a tap, shows it) |
| **ZR** | In an episode's last 30 seconds: play the next one now |
| **B** | Back to the library (where you were is remembered) |
| **+** | The menu, over the video: Back to the library, Exit |

In a list of tracks, **↑** / **↓** move, **A** picks and **B** closes it.
The subtitles list stays open after you pick, for the options that apply to
them (**◀** / **▶**). In the Playback list, **A** goes to a chapter and **◀** / **▶**
change the speed and the delays (or tap the left or right half of their row).
The speed stays for the next videos until KaiVideo closes; the delays are for
the video playing.
Any button or tap shows the bar for a few seconds. With the bar showing, tap
the picture or the play button to pause or play (a paused video shows a big
play symbol), or tap or drag the progress bar to go there; **Playback**,
**Audio** or **CC** open their lists, and the **×** at the top left goes back.

## Limitations

- **Picture subtitles** (DVD and Blu-ray ones, in MKV) aren't shown; text ones
  are.
- **10-bit** VP9 and AV1, and **12-bit** HEVC, can't be played.
- **HEVC** is decoded on the CPU, because the decoder interface KaiVideo uses
  ([libounce](https://github.com/ouncebrew/libounce)) doesn't offer it. 10-bit
  video is shown in 8 bits.
- **4K HEVC** is too heavy for the CPU: measured on the console, 10-bit 4K
  decoded at 21 frames per second at 16 Mb/s and at 15 at 68 Mb/s, so frames
  are skipped to keep up with the sound.
- **HDR** isn't converted: HDR videos play, with washed-out colors.
- **Big libraries**: tried with a few dozen videos on the console (and with
  thousands of names on a PC). With hundreds of videos, the first scan and
  the first round of pictures take longer.
- After a jump (**◀** / **▶**, **L** / **R**, the progress bar) the picture
  can stutter for a moment.
- No streaming from the network: KaiVideo plays files on the card.

## Plans

- **HEVC in hardware**, if libounce offers it.
- **Jellyfin**: browsing a server and playing what the console can handle.
- **Picture subtitles** (PGS from Blu-ray, VobSub from DVD).
- Smoother jumps, and checks with big libraries on the console.

## Building

KaiVideo is built in Docker, on top of the
[ounce-devkit](https://github.com/ouncebrew/ounce-devkit) image:

```sh
docker build -t kaivideo-devkit ffmpeg   # once: the devkit plus FFmpeg (LGPL)
docker run --rm -v "$PWD:/w" -w /w kaivideo-devkit make          # KaiVideo.nro
docker run --rm -v "$PWD:/w" -w /w/tests kairadio-host make run  # host tests
docker run --rm -v "$PWD:/w" -w /w kaivideo-devkit make -C tests/preview  # screenshots
```

`ffmpeg/Dockerfile` builds FFmpeg 7.1 from its release tarball (checked by
SHA-256) with only the demuxers, parsers, bitstream filters, decoders and the
`atempo` audio filter KaiVideo needs, and fails if the build would include GPL
or non-free parts. Most video is decoded by the console, through
[libounce](https://github.com/ouncebrew/libounce)'s `VideoDecoder`; FFmpeg
decodes HEVC and 10-bit H.264.

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
- **FFmpeg and FriBidi.** KaiVideo uses FFmpeg, built under the LGPL 2.1 (no
  GPL or non-free parts), and FriBidi, under the same license. The script that
  builds FFmpeg is in this repository and FriBidi comes with the devkit, so
  KaiVideo can be rebuilt, changed and relinked.
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
fonts are read, and thumbnails written, with [stb](https://github.com/nothings/stb)
(public domain). Styled subtitles use [libass](https://github.com/libass/libass)
([ISC](third_party/libass-ISC.txt)), [FreeType](https://freetype.org/)
([FTL](third_party/FreeType-FTL.txt); portions of this software are copyright
The FreeType Project, all rights reserved),
[HarfBuzz](https://github.com/harfbuzz/harfbuzz) ([MIT](third_party/HarfBuzz-MIT.txt)),
[FriBidi](https://github.com/fribidi/fribidi) ([LGPL 2.1](third_party/FriBidi-LGPL-2.1.txt)),
[libpng](third_party/libpng-LICENSE.txt), [bzip2](third_party/bzip2-LICENSE.txt)
and [zlib](third_party/zlib-LICENSE.txt).
