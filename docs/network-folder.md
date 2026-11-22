# Network folders

[Back to the README](../README.md)

KaiVideo can play from a folder on your network, with nothing to install on
it:

- a **Windows shared folder**;
- a **NAS** (Synology, QNAP and the like);
- **Samba** on Linux or macOS;
- a **WebDAV** server (Nextcloud, rclone, a NAS's WebDAV).

| As a library | Its Settings page |
|--------------|-------------------|
| ![A network folder's library](screenshots/network_library.png) | ![The network folder's page](screenshots/network_settings.png) |

## Setting it up

In **Settings > Network folder**:

1. **Address**: one of these.
   - `\\PC\Videos` or `smb://PC/Videos` for a shared folder (SMB). A folder
     inside it works too: `\\PC\Videos\Films`. Use the computer's IP address
     (`\\192.168.0.10\Videos`) if its name isn't found. For another port:
     `smb://PC:4450/Videos`.
   - `192.168.0.10:8098/videos`, or a full `http://` or `https://` address,
     for WebDAV.
2. **User and password**: the account that can read the folder. For a domain,
   type `DOMAIN\user`. With no user, the folder's guest access is used.
3. **Test the connection**: it says how many folders and videos are at the
   top, or what went wrong.

Then:

- **X** (or the button next to KaiVideo's name) goes through the SD card, the
  network folder and each server. The network folder's library has its own
  turquoise color. It shows at once what was found the last time, and looks
  for changes in the background.
- **Files** shows the network folder first, above the card's folders, to
  browse it folder by folder.

A new address forgets the old user and password, so they're never sent to
another computer, and the old folder's library. **Forget this folder** removes the address, user and
password.

## Sharing a folder on Windows

1. **Make your network private**: Settings > Network & internet > Ethernet
   (or Wi-Fi) > Network profile type > **Private**. On a public network,
   Windows doesn't share folders.
2. **Share the folder**: right-click it > Properties > Sharing > **Advanced
   Sharing** > check **Share this folder** and give it a name, for example
   `Media`. Under **Permissions**, give your user **Read** only: KaiVideo
   only reads.
3. On the console, type `\\<your PC's IP address>\Media` and your Windows
   user name and password. With a Microsoft account, use its e-mail address
   and password, not the PIN. If that fails, try
   `MicrosoftAccount\you@example.com`.

If **Test the connection** says nothing answered, check that **File and
Printer Sharing** is allowed in Windows Firewall for private networks.

## Good to know

- **The password** is kept on the card, in `config/network.txt`, as it was
  typed: KaiVideo needs it to sign in each time. Use an account that can only
  read the folder.
- **WebDAV over plain `http://`** sends the password without encryption.
  Use it only on your home network, or use `https://`.
- **Pictures** for the cards are made by reading each video over the network
  the first time, so they come more slowly than from the card. Once made,
  they're kept.
- Opening a folder in Files waits for the server's answer. That's usually a
  moment, but it can take several seconds when the server doesn't answer.
- `.srt` files next to a video in a network folder aren't loaded yet.
  Subtitles inside the video are.
- Tried with a Windows 11 shared folder, a Samba server and an rclone WebDAV
  server, all on a home network.
