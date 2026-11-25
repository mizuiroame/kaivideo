# IPTV lists and Stremio addons

[Back to the README](../README.md)

KaiVideo comes with **no channels, no lists and no addons**. These sources show
only what you set up yourself: your own IPTV list or account, and addons you
add by their address. Only play what you have the right to watch.

## IPTV

Settings > **IPTV** sets one list:

- **An M3U list**: its address. Its channels show with their groups and logos.
- **An Xtream account**: the server's address (`host:port`), your user and
  your password. Its live channels show, and also its **films and series**.

Press **Load the channels**. Then **X** goes to the IPTV view, after your
servers. It has an orange pill and these tabs:

- **Home**: as a Showcase (Settings > Display > Home style), the channel under
  the cursor shows large, its logo on a blur of its colors, with what's on now
  (how far into it, and its description when the guide has one) and next.
  Rows: Recently watched, Favorite channels, one row per group, then with an
  Xtream account Films, Series and one row per film category. **A** watches a
  channel, or opens a film's details or a series' page; a tap shows it large
  first, a second tap opens it. **R**, or its button, adds a channel to the
  Favorites or takes it out. A film's or series' details are read only once
  the cursor rests on it. With Home style **Classic**: Recently watched,
  Favorites, then one row per group, and the button above the row.
- **Live TV**: the guide, if the list has one. **Y** picks a group.
- **Films**, **Series** and **Search**, with an Xtream account that has them.
  A series' episodes are read when you open its page. When the provider puts
  them in categories, **Y** (or the pill above the cards) picks one.

**Guide (XMLTV)** uses another guide than the list's own. **Certificate check
Off** accepts any certificate from the list, its guide and its channels. Many
IPTV servers have broken ones, so turn it off only for a list you trust.

KaiVideo plays channels in HLS (including fMP4) and plain MPEG-TS. A channel
that won't open says why: it moved, it refuses to play outside its country,
its server doesn't answer, or its certificate can't be trusted.

Some channels play only when the app sends a **Referer**, **User-Agent** or
**Origin** of their choice. KaiVideo sends them when the list says so, in
either form lists use:

```
#EXTINF:-1,A channel
#EXTVLCOPT:http-referrer=https://example.com/
#EXTVLCOPT:http-user-agent=Mozilla/5.0
https://example.com/live.m3u8
```

or after the address, as Kodi takes them:
`https://example.com/live.m3u8|Referer=https%3A%2F%2Fexample.com%2F&User-Agent=Mozilla%2F5.0`.
Only these three are sent; others (a cookie, say) are left out. KaiVideo
doesn't get around channels that play only in their country.

Recently watched and Favorites are kept by the channel's name in
`config/iptv_marks.txt`. The account is in `config/iptv.txt`, with the
password encoded, not as typed.

## Stremio and Nuvio addons

Settings > **Stremio addons** takes addons by their address: the
`manifest.json` link an addon's page gives, as you would paste it into Stremio
or Nuvio. A `stremio://` link works too. You can add up to six. **A** twice on
one removes it. Nuvio's own "plugins" aren't addons and don't work here.

**X** goes to the Addons view, after IPTV. It has a blue pill, one tab per
catalog, Home with what you were watching, and Search, which asks every addon
that can be searched. A catalog brings its next page as you near its end, when
its addon gives more than one.

When you play a film or an episode, KaiVideo asks every addon for its streams
and lists them with each addon's name and details, so you can choose one. It
plays only **web streams** (`http` and `https`, MP4, MKV, WebM or HLS).
**Torrents, magnet links, YouTube ids and links to outside apps are always
left out.** A stream that asks for a **Referer**, **User-Agent** or **Origin**
(`proxyHeaders` in its addon's answer) gets them; one that asks for any other
header is left out, since it wouldn't play.

Addons that need to be set up on their own page first say so. Set them up
there, then add the address the page gives you.

The addresses are kept in `config/addons.txt`. What you watched is kept on the
card, as for any video. Addons aren't told.
