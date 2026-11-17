# KaiVideo

Open-source video player for Switch 2 homebrew (kaiHEN), in the spirit of
[KaiPlayer](https://github.com/mizuiroame/kaiplayer).

> **Work in progress.** Nothing to download yet.

## Plans

- Play the videos on your card: MP4, MKV and WebM files.
- Hardware video decoding with the console's own decoder: H.264, VP9, AV1
  and VP8, up to 1080p at 60 frames per second (and 4K at 30, scaled to the
  screen). Files the hardware can't play are named as such instead of
  stuttering.
- Audio with [FFmpeg](https://ffmpeg.org/): AAC, Opus, Vorbis, MP3 and FLAC.
- Drawn on the GPU, sharp in handheld and docked, with KaiPlayer's look,
  touch and buttons: resume where you left off, subtitles, audio tracks.

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
under the LGPL 2.1.
