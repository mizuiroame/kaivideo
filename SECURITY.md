# Security policy

## Supported versions

Only the [latest release](https://github.com/mizuiroame/kaivideo/releases/latest)
gets security fixes. Please check that a problem still happens with it before
reporting.

## Reporting a vulnerability

Please don't open a public issue for a security problem. Report it privately
instead: on this repository's **Security** tab, choose **Report a
vulnerability**. Say which version you used, what you did and what happened,
and, if you can, attach the file or the server answer that shows it (a video,
a subtitle file, an HLS list, a WebDAV listing, a server's JSON).

KaiVideo is a hobby project kept by one person. Reports are read and answered
as time allows; once a fix is released, the report can be made public, and
you're credited in the release notes unless you'd rather not be.

## Scope

In scope: KaiVideo's own code and how it uses its libraries. For instance, a
file on the SD card, a network folder or a media server that can crash
KaiVideo or make it run code, read or write files outside what it should, or
send a password or token where it shouldn't.

Out of scope: kaiHEN, the console's system software, the servers themselves
(Jellyfin, Emby, Plex, Samba, Windows sharing, WebDAV servers) and bugs that
belong to a bundled library alone (please report those upstream too; KaiVideo
updates them when there's a fix).

## How KaiVideo handles your data

Under kaiHEN, homebrew runs with broad system access, so a bug in an app that
reads untrusted files could affect the whole console. KaiVideo keeps what it
touches small:

- **What it connects to.** Nothing, unless you set something up. With a
  server, it talks to that server (Jellyfin, Emby or Plex, at the address you
  gave), and for Plex to plex.tv for the sign-in and to find your server; it
  tells the server what you watched and where you stopped, as the server's own
  apps do. **Find servers on this network** sends Jellyfin's discovery
  broadcast on your network (UDP 7359). With a
  [network folder](docs/network-folder.md), it talks to that folder's server.
  Live TV channels may be read from the address the server gives for them.
  Requests say they're from KaiVideo (its version and this repository), and
  nothing else about you.
- **Only http and https.** Every web request is limited to `http://` and
  `https://` (no `file:`, `ftp:` or other schemes, even through a redirect),
  and an `https://` address can't be redirected to plain `http://`. A
  server's token is sent only to that server: never to another host an HLS
  list names, and a sign-in isn't re-sent to wherever a redirect points.
  Certificates are checked unless you turn **Certificate check** off, and never
  stop being checked for plex.tv and `*.plex.direct`.
- **What's kept on the card**, in `config/`: your settings, what you watched,
  the libraries found, and the sessions:
  - A server's session (`jellyfin.txt`, `emby.txt`, `plex.txt`) is its access
    token, not your password. Whoever has the card can use that token until
    you sign out (which ends it on the server) or remove the device in the
    server's settings.
  - The network folder's password is never kept as typed. For a shared
    folder (SMB), `network.txt` holds its NT hash, what Windows signs in with:
    the password can't be read from it, but the hash still opens that share.
    For WebDAV it's only encoded, since WebDAV sends the password itself. With
    **Remember the password** off, nothing of it is written. Give KaiVideo
    [a user of its own](docs/network-folder.md#a-user-of-its-own) that can
    only read the folder.
  - `kaivideo.log` lists what was played and how (paths and server item ids),
    to find problems. It never holds tokens or passwords.
- **Shared folders are signed.** SMB connections require signing, so nobody
  on the network can change what's read. **Allow without signing (old NAS)**
  turns that off for a folder whose server can't sign.
- **Downloads** are written only as video files under the card's downloads
  folder; a list entry that points elsewhere is dropped.

## Untrusted input

- **Videos** are read by FFmpeg (LGPL build, no GPL parts), only as MP4/MOV,
  Matroska or MPEG-TS: a file can't make FFmpeg follow a playlist to other
  files. Servers' conversions are HLS lists, read with limits on their size
  and on each piece.
- **Subtitles** (SRT, WebVTT, the server's) are read with a limit on cues and
  on every time field; **servers' answers** (JSON) by Jansson, with size
  limits; **network folder listings** with length checks, leaving out names
  with control characters or slashes, `.` and `..`.
- **Hardening.** The `.nro` is built with `-fstack-protector-strong` and
  `-fwrapv`. The host tests run under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- **Libraries.** FFmpeg, libsmb2, FreeType, HarfBuzz, FriBidi, Jansson and
  the others are listed with their versions and licenses in
  [docs/legal.md](docs/legal.md). libcurl comes from devkitPro's packages
  (7.69.1, older than upstream's).

The main remaining risk is in FFmpeg's demuxers and decoders, which parse
every video. Only play files from sources you trust.
