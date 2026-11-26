# Media servers

[Back to the README](../README.md)

KaiVideo works with **Jellyfin**, **Emby** and **Plex**, one or all three.

## Signing in

In **Settings** (**−**), each server has its own page:

- **Jellyfin**:
  1. Choose **Find servers on this network**, or type your server's address
     under **Server**, like `192.168.0.10:8096`.
  2. **Test the connection** says whether it answers, or which step failed:
     the name, the port, the time it took, the certificate.
  3. Choose **Sign in with a code**. In Jellyfin on your phone or computer,
     open your profile, choose **Quick Connect** and enter the code. Or
     choose **Sign in with a password** (the password isn't kept).
- **Emby**: the same, but Emby has no Quick Connect. Sign in with your user
  name and password.
- **Plex**: choose **Sign in at plex.tv/link**, and a code appears. On your
  phone or computer, go to **plex.tv/link**, signed in to your Plex account,
  and enter it. KaiVideo finds your account's server and the address of it
  that answers (your home network first).

  If your account has a **Plex Home** with several profiles, KaiVideo then
  asks who's watching, as Plex does: choose a profile (a lock means it has a
  PIN, typed on the PIN pad with the buttons or your fingers). Its library,
  what it watched and where it left off are its own. Switch later with
  **Profile** on Plex's page in Settings, or **Switch Plex profile** in the
  **+** menu. A profile can only be used if the server is shared with it.

Once you're signed in, your library appears. **X** on the library's screens,
or the button next to KaiVideo's name, goes through the SD card, the
[network folder](network-folder.md) and each server you're signed in to. The
server shown wears its color: its own, or another chosen under **Color** on
its page in Settings.

## The library

A server's libraries are the tabs, by their own names (like Movies and
Shows), with Home, Search and Files. Films and episodes open a panel with
their summary:

- **A** plays or resumes.
- **Y** starts from the beginning.
- **X** marks them watched.
- **R** adds the film, or the episode's series, to your **Favorites** (on a
  series' page, **R** too). Favorites have their own row on Home.
- **L** downloads them. See [Downloads](downloads.md).

Where you left a video, what you've watched and your favorites go to the
server and come back from it, so they match your phone and TV.

A Jellyfin or Emby server outside your home works over **https**. Its
certificate is checked. For a server with a certificate of its own making,
turn **Certificate check** off on that server's page: each server has its
own, and only that server's address (its name and port) then takes any
certificate. Plex finds its own way in: its own https addresses, or Plex's
relay, and its certificates are always checked.

## What plays as it is, and what's converted

What the console can play is played from the original file: its picture,
every sound track (changed almost at once) and its subtitles, styled ones
included. Everything else, like 4K, HDR or very high bitrates, is converted
by your server to H.264 up to 1080p, with HDR turned into normal colors.
Your server needs video transcoding allowed for your account.

**Server video quality**, in Settings' Playback page or in the Playback list
(**↑**) while a video plays, sets the most a video may use, from **Auto**
down to 420 kb/s. A video over it is converted to fit. On a converted video:

- changing the quality opens it again at the same place, which takes a few
  seconds;
- changing the sound track doesn't stop the picture: the server converts that
  track alone, and it comes in a second or two.

Separate subtitle files on the server show in the subtitles list too.
**Picture subtitles** (PGS from Blu-ray, VobSub from DVD) can't be drawn by
the console, so they show as "... (burned in)". Choose one and the video
opens again at the same place, converted by the server with them drawn on
it. Another track, or Off, goes back to the original file. KaiVideo never
asks for them unless you choose them, so a film with a picture track marked
default still plays as it is.

Each sign-in is kept on the card (`config/jellyfin.txt`, `config/emby.txt`,
`config/plex.txt`), with each server's library (`config/*_library.txt`), so
it shows at once the next time. **Sign out** ends it.

## What differs between the servers

- **Live TV**, and pictures of a moment while dragging the bar, come from
  Jellyfin.
- **Plex** keeps no favorites of its own: KaiVideo keeps them on the console
  (`config/plex_library_favorites.txt`, and one for each other Plex Home
  profile). Its intro and credits markers come
  with Plex Pass. On a video Plex converts, text subtitles inside the file
  are drawn onto the picture by the server, as picture subtitles are.
- **Emby** keeps no place to resume in a video's first seconds.

## What your server adds, when it's set up for it

- **Skip intro, recap and credits**: where the server marked them (with a
  plugin such as [Intro Skipper](https://github.com/intro-skipper/intro-skipper),
  or chapters named so), a button comes up at the bottom right. **ZR** or a
  tap skips that part. In an episode's last seconds, **ZR** plays the next
  one instead.
- **Pictures on the bar**: with **trickplay** turned on in the server's
  libraries, dragging the progress bar shows a picture of where your finger
  is, and a far jump shows one while it loads.

## Live TV

With tuners or channels on a Jellyfin server, a **Live TV** tab shows its
guide: a row for each channel and its programs over two hours, with a line at
the time now.

- **↑** / **↓** change channel, and **◀** / **▶** move through its programs,
  up to a day ahead. Drag the guide sideways to move through the hours. The
  strip at the bottom says what the cursor is on. The guide is read a few
  hours at a time as you move through it.
- **A** watches the channel. Tap a program to put the cursor on it, and tap
  it again to watch.
- **L** / **R** change channel while it plays or while one opens, skipping as
  many as you press. **B** goes back at once.

### Recording (Jellyfin's DVR)

When the server records Live TV (a recording folder set in its Live TV
settings, and a user allowed to manage Live TV), KaiVideo can use it:

- **R** (or the **Record** pill in the strip) on a program: **Record this
  program**, **Record the series** (for programs the guide marks as a series),
  or, when it's already set, **Don't record this program** and **Stop
  recording the series**. Programs the server will record have a red dot.
- **ZR** (or its button) while a channel plays records what's on now.
- **L** (or the **Recordings** pill) lists what the server will record, the
  series it records and what it recorded. **A** plays a recording, as any of
  the server's videos; **X** twice cancels a recording to come, stops a
  series, or deletes a recording (if your user may delete). **B** goes back
  to the guide.

The recordings are made by the server, so the console can be off. Plex's DVR
needs Plex Pass and isn't supported; neither is Emby's.

A channel whose source is an HLS stream of its own plays straight from it, at
the best quality up to 1080p, with its own sound tracks (**X**) and WebVTT
subtitles (**Y**). Live captions run a little behind the voice; a negative
subtitle delay in the Playback list makes up for it. Other channels are
converted by the server. Playback starts a few seconds behind live, so it
doesn't stop to wait. The channel you watch shows as playing on the server,
as videos do.
