# The library

[Back to the README](../README.md)

KaiVideo shows your videos as a library of **films and series**. It builds
the library from the folder you choose on the card, or from a
[network folder](network-folder.md), or it shows a
[media server's](media-servers.md) own library.

The first time, KaiVideo asks where your videos are: **a folder on the SD card**
(the folder picker; **−** or **B** at the card's top goes back), **a network
folder**, **a media server**, **an IPTV list** or **Stremio addons**, which open
their page in Settings (**B** there comes back), or **Later**. Leaving without
setting anything up brings the question back.

- **Home** shows what you were watching, what's newest, your series and your
  films.
- **Films** and **Series** show them all. A series opens on its seasons and
  episodes.
- **Search** finds a title by name.
- **Files** browses the whole card (folders first, then videos, with their
  sizes) and the network folder, which is listed first.

Cards show a **picture from the video**. The console's decoder takes it in
the background the first time; what's on screen is made first. The pictures
are kept in `config/thumbs`, so they show at once afterwards.

## How films and series are found

KaiVideo goes by file and folder names, the way most media libraries are
kept:

- **Series**: files named with the season and episode, like
  `Rook.S01E03.Lamentia.1080p.mkv` or `Rook 1x03.mkv` (or `Rook - 03` for
  anime). A folder named after the series (`TV/Rook/`, `Rook/Season 1/`) gives
  it its name. Otherwise the file's name does.
- **Films**: every other video. A `Title (Year)` folder gives the film its
  name (`Movies/Paper Lanterns (1998)/anything.mkv`). Otherwise the file's
  name does, without tags like `1080p`, `BluRay` or `x264`.

After you copy more videos, **Update the library** (in the **+** menu, or
in Settings' Library page) looks again.

## Settings

Press **−** (or tap **Settings**). Settings has pages:

- **Library**: the card's folder, **Update the library**, and
  **Thumbnails** (pictures from the videos on their cards, or plain colors).
- **Network folder**: its address, user and password. See
  [Network folders](network-folder.md).
- **Jellyfin**, **Emby** and **Plex**: each server's page. See
  [Media servers](media-servers.md).
- **Playback**: **Play next episode**, and the quality to ask a server for.
- **Display**:
  - **Language**: see [Languages](languages.md).
  - **Theme**: Dark, Black, Midnight or Plum.
  - **Accent color**.
  - **Background**: put JPEG or PNG images in a `Backgrounds` folder in your
    library folder and pick one. It's blurred (**Background blur**) and
    darkened, so text stays readable.
  - **Clock**: 12 or 24 hours.
- **About**:
  - **Credits and licenses**.
  - **Check once a day**: on by default, KaiVideo asks GitHub once a day for
    a new version and says when there is one.
  - **Look for an update**: looks now. With a new version found, if KaiVideo
    runs from `sdmc:/switch/`, **A** downloads it, checks its SHA-256 and puts
    it in place for the next start. From brewstore, update it there.

## Where KaiVideo keeps its files

KaiVideo keeps everything in a `config` folder next to `KaiVideo.nro`:
`sdmc:/switch/kaivideo/config/`, or `sdmc:/brewstore/apps/kaivideo/config/`
when brewstore installs it. That covers:

- your settings;
- the libraries found (`library.txt`, `library-network.txt` and each
  server's);
- where you left each video, and what you've watched;
- the sound tracks and subtitles you chose;
- the downloads (`downloads.txt`), and what waits to be told to a server
  (`offline_sync.txt`);
- the network folder's account (`network.txt`);
- a log of the last run (`kaivideo.log`).

Your videos aren't touched.

> **Don't take the microSD card out while kaiHEN is running.** Removing it
> means restarting the console, and kaiHEN has to be started again after
> that.
