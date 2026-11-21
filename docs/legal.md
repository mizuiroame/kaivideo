# Legal notice and licenses

[Back to the README](../README.md)

## Legal notice

- **Independent project.** KaiVideo is not affiliated with, endorsed by or
  sponsored by Nintendo Co., Ltd. or its subsidiaries.
- **Trademarks.** "Nintendo", "Nintendo Switch" and "Switch 2" are
  trademarks of their owners, as are "Jellyfin", "Emby", "Plex" and
  "Windows". They are used here only to say which hardware, servers and
  systems this program works with. KaiVideo is not affiliated with or
  endorsed by any of them. Format names are used only to name the files
  KaiVideo plays.
- **No Nintendo material.** This repository and its releases contain no
  Nintendo code, SDK, firmware, cryptographic keys, games, or graphics or
  audio assets. KaiVideo is built only with open-source tools and libraries.
- **No circumvention.** KaiVideo does not circumvent any technical
  protection measure. It doesn't play DRM-protected video, and it doesn't
  download from streaming sites. It plays the files users put on their own
  card, in their own network folders or on their own Jellyfin, Emby or Plex
  server, and the Live TV channels their own server lists. It comes with no
  channels or channel lists of its own.
- **FFmpeg, libsmb2 and FriBidi.** KaiVideo uses FFmpeg, built under the LGPL
  2.1 (no GPL or non-free parts), libsmb2's client library, under the LGPL
  2.1, and FriBidi, under the same license. The scripts that build FFmpeg and
  libsmb2 are in this repository and FriBidi comes with the devkit, so
  KaiVideo can be rebuilt, changed and relinked.
- **User responsibility.** Only play videos you have the right to watch.
  Running homebrew (kaiHEN) can put your console at risk, can affect its
  warranty and can go against the manufacturer's terms of service. You do it
  at your own risk.
- **No warranty.** The software is provided "as is", without warranty of any
  kind. See [LICENSE](../LICENSE).

This notice is not legal advice.

## Licenses

KaiVideo is released under the [MIT License](../LICENSE). It includes:

| Part | What it does | License |
|------|--------------|---------|
| [FFmpeg](https://ffmpeg.org/) | Demuxing, decoding, sound | [LGPL 2.1](../third_party/FFmpeg-LGPL-2.1.txt) |
| [libsmb2](https://github.com/sahlberg/libsmb2) | Network folders over SMB | [LGPL 2.1](../third_party/libsmb2-LGPL-2.1.txt) ([its notice](../third_party/libsmb2-COPYING.txt)) |
| [curl](https://curl.se/) | The network, servers and WebDAV | [curl](../third_party/curl-LICENSE.txt) |
| [Jansson](https://github.com/akheron/jansson) | Servers' answers | [MIT](../third_party/Jansson-MIT.txt) |
| [libass](https://github.com/libass/libass) | Styled subtitles | [ISC](../third_party/libass-ISC.txt) |
| [FreeType](https://freetype.org/) | Fonts | [FTL](../third_party/FreeType-FTL.txt) |
| [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | Text shaping | [MIT](../third_party/HarfBuzz-MIT.txt) |
| [FriBidi](https://github.com/fribidi/fribidi) | Right-to-left text | [LGPL 2.1](../third_party/FriBidi-LGPL-2.1.txt) |
| [stb](https://github.com/nothings/stb) | Images and thumbnails | Public domain |
| libpng, bzip2, zlib | Compression and images | [libpng](../third_party/libpng-LICENSE.txt), [bzip2](../third_party/bzip2-LICENSE.txt), [zlib](../third_party/zlib-LICENSE.txt) |
| Noto Sans | KaiVideo's font | [OFL](../third_party/NotoSans-OFL.txt) |
| Atkinson Hyperlegible, Fira Sans, Lato, PT Serif, Comic Neue | Fonts for subtitles | OFL: [Atkinson Hyperlegible](../third_party/AtkinsonHyperlegible-OFL.txt), [Fira Sans](../third_party/FiraSans-OFL.txt), [Lato](../third_party/Lato-OFL.txt), [PT Serif](../third_party/PTSerif-OFL.txt), [Comic Neue](../third_party/ComicNeue-OFL.txt) |

Portions of this software are copyright The FreeType Project, all rights
reserved.
