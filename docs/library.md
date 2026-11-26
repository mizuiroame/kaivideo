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

## Summaries, genres and pictures (.nfo files)

KaiVideo reads the `.nfo` files that Kodi, Jellyfin, Emby, tinyMediaManager
and similar tools write next to videos, and the pictures beside them. With
them, Home's **Showcase** shows the summary, year, length and genres of a film
or series on the card or the network folder, a wide picture behind it, and a
row for each common genre, as it does for a media server.

```
Movies/
  Paper Lanterns (1998)/
    Paper.Lanterns.1998.1080p.mkv
    movie.nfo            (or Paper.Lanterns.1998.1080p.nfo)
    poster.jpg           (or folder.jpg, cover.jpg, Paper.Lanterns.1998.1080p-poster.jpg)
    fanart.jpg           (or backdrop.jpg, background.jpg, Paper.Lanterns.1998.1080p-fanart.jpg)
  Kite Hill.mkv          (several films in one folder: each by its own name)
  Kite Hill.nfo
  Kite Hill-poster.jpg
  Kite Hill-fanart.jpg
TV/
  Rook/
    tvshow.nfo           the series' name, year, summary and genres
    poster.jpg           the series' poster
    fanart.jpg           the series' wide picture
    Season 1/
      Rook.S01E01.mkv
      Rook.S01E01.nfo    the episode's title and summary (optional)
      Rook.S01E01-thumb.jpg   a still of the episode (optional)
```

- **Names**: a video's own files are named like it, without its extension,
  plus `.nfo`, `-poster`, `-fanart` or `-thumb` (`.jpg`, `.jpeg` or `.png`; any
  case). `movie.nfo`, `fanart.jpg` and `poster.jpg` count only in a folder
  with **one** film; in a folder of several, each film needs its own.
- **A series' files** (`tvshow.nfo`, `poster.jpg`, `fanart.jpg`) go in the
  series' folder; they cover its episodes there and in its season folders
  (`Season 1`, `S2`, `Specials`...). Episodes still need the season and
  episode in their file names (`S01E03`).
- **What's read**: from `<movie>`, `<tvshow>` and `<episodedetails>`, the
  title, year (or the premiere date), plot (or outline), up to three genres,
  the length in minutes, and an episode's season and number. A film's `.nfo`
  title replaces the one from its file name; a `tvshow.nfo` title names the
  series. Ratings, cast and the web addresses of artwork are left out: only
  pictures next to the videos are shown.
- **Episodes**: on a series' page, an episode is listed by its `.nfo` title;
  **A** (or a tap) on one whose `.nfo` has a plot opens its details first, with
  the plot, as a server's episode does, and **A** there plays it. Episodes
  without a plot play at once.
- **When**: `.nfo` files are read only while the library is scanned (at start,
  or **Update the library**), and what they said is kept in `library.txt`, so
  after editing one, update the library. A broken `.nfo`, or one that's only a
  web address, is skipped: the file's name is used.
- **Text**: UTF-8, as these tools write it. A network folder's `.nfo` files
  are read one request each (up to 4000 per update), so a big folder takes
  longer to update the first time.

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
