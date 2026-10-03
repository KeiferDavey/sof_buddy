# URL radio (experimental Windows client feature)

This fork adds true HTTP(S) audio playback to SoF Buddy using dynamically loaded
**32-bit VLC 3.x**. A server sends a URL; each opted-in client opens that URL and
buffers audio while playing. The addon does not download a complete song into
SoF's sound directory. VLC may buffer/cache data internally. Python is not used.

This uses a separate audio output, not SoF's positional sound mixer. Adjust it
with the radio volume control. It supports media/streams understood by the VLC
runtime. A direct MP3 stream is the best first test. YouTube/Spotify/webpage links
are not direct audio URLs and are not handled by this feature. HTTP(S) playlists
may be handled by VLC, but they require separate live validation. Clients are
not sample-synchronized; buffering differs, and late arrivals start a file from
its beginning (or join the live point of a live radio stream).

## Client installation and first test

1. Use the Windows SoF Buddy client plus SoFplus `spcl.dll`. Install the fork's
   `sof_buddy.dll` in the game root. Existing Buddy files/funcmaps are needed.
   Run `sof_buddy/enable_sofplus_and_buddy.cmd` if Buddy isn't already enabled.
2. Obtain the **win32 ZIP of VLC 3.x** from VideoLAN:
   https://www.videolan.org/vlc/download-windows.html
   Extract the full runtime folder (including DLLs and `plugins/`) into
   `sof_buddy/vlc/`, such that `sof_buddy/vlc/libvlc.dll` exists. Keep the other
   runtime files: copying just libvlc.dll is insufficient. An alternative is to
   set `_sofbuddy_radio_vlc_dir` to an existing 32-bit VLC 3.x installation.
   VLC 64-bit cannot load into this 32-bit game. VLC 4.x is rejected because
   its C ABI differs. Windows 10/11 is the first target; XP/Wine is untested.
3. Start the client and run:

   ```text
   sofbuddy_radio_volume 40
   sofbuddy_radio_play "https://YOUR-HOST/live.mp3"
   sofbuddy_radio_status
   sofbuddy_radio_stop
   ```

   Replace the example URL with a real direct audio stream. Watch the console
   for `Connecting`, `Buffering`, `Playing`, or failure messages.
4. To accept this server's broadcasts, enable:

   ```text
   set _sofbuddy_radio_allow_server 1
   ```

   It defaults to 0. Setting it back to 0 stops server-owned playback.
   `set _sofbuddy_radio_enabled 0` disables radio and stops all radio playback.
   Manual client playback is allowed with the enabled setting and does not
   require server opt-in. Local `sofbuddy_radio_stop` stops the current request;
   it won't resume until the server issues a changed request (or opt-in is
   toggled). Turn off server opt-in to reject future play requests.

The feature uses client cvar **90**. Reserve it for this addon. If another addon
uses that cvar, change the `90` values in the server func and set
`_sofbuddy_radio_channel` to the same unused index (0..99) on every client.

## Server installation and commands

Copy `rsrc/server_radio/spf_sv_radio.func` and `.cfg` into the **server's active
user directory**, under `sofplus/addons/`. Use the function filename with **no
leading hyphen**. Restart the server so its addon init runs. No Buddy DLL, VLC,
or Python is needed on the dedicated server; it runs ordinary SoFplus.

In the server console or through authenticated RCON:

```text
sb_radio_play "https://YOUR-HOST/live.mp3"
sb_radio_stop
```

These are administrator controls, not player chat commands. Internally the
registered functions can also be called with `sp_sc_func_exec spf_sv_radio_play
"https://YOUR-HOST/live.mp3"` and `sp_sc_func_exec spf_sv_radio_stop`.

To play automatically at map end, edit the server cfg:

```text
set _spf_sv_radio_url "https://YOUR-HOST/song.mp3"
set _spf_sv_radio_play_at_end 1
```

Defaults are empty URL and manual control. Map begin sends stop. A new player
receives the current play request on client begin. Existing/ordinary clients
will receive the cvar value but will not play music without this fork.
The radio addon is separate from the highscore/capture addon.

## Protocol and threading

`sp_sv_client_cvar_set SLOT 90 VALUE` sets `_sp_cl_sv_90` on that client.
The server loops over occupied slots; the documented setter takes a slot, not
`*`. Values are `SB_RADIO_V1|COUNTER|play|URL` or `SB_RADIO_V1|COUNTER|stop`.
The counter allows replay of the same URL as a new request. The client polls
once per game frame and consumes each changed value once. It never evaluates
that value as commands. Only HTTP(S) URLs up to 384 printable ASCII characters
are accepted; encode spaces and non-ASCII characters in the URL. No shell or
external program is launched. Server-originated playback requires opt-in.

The libVLC loader, stream opening, stopping and cleanup run on one worker.
Only the game thread touches engine CVars/console. Messages are returned via a
bounded queue. CL_Shutdown joins the worker before engine shutdown; it is not
joined under DllMain's loader lock. Disconnect stops server-owned playback.
Media output is disabled for video (`--no-video`). libVLC uses 1.5s configured
network caching; this is buffering, not an exact latency guarantee.

Backend dependency: the official VLC 3.x API
https://videolan.videolan.me/vlc-3.0/group__libvlc__media.html
https://videolan.videolan.me/vlc-3.0/group__libvlc__media__player.html
Server bridge: SoFplus manual's `sp_sv_client_cvar_set`
https://sof1.megalag.org/sofplus/download/sofplus-manual.html

## Building your fork

Apply the provided patch to the recorded upstream base, or copy the changed
files into your fork. The feature is enabled in `features/FEATURES.txt` and is
recognized by `tools/generate_features_txt.py`. Existing release builds find
its `.cpp` files and generate its hooks automatically. No new link libraries
are required because VLC is loaded at runtime.

```bash
sudo apt-get install build-essential g++-mingw-w64-i686 python3 python3-yaml
make
```

The output is `bin/sof_buddy.dll`. This remains a 32-bit Windows DLL even when
compiled on Linux. Do not use upstream's auto-updater to replace the custom
DLL until this feature is included in the update you are installing.

Run the portable protocol checks:

```bash
g++ -std=c++14 -Wall -Wextra -Werror -Isrc tests/url_radio/protocol_test.cpp -o /tmp/radio-protocol-test
/tmp/radio-protocol-test
```

## Validation status / live checklist

A build and portable request-parser tests can verify compilation and protocol
validation, but cannot establish successful Windows in-game audio playback.
A Windows SoF installation is not available in the build workspace. Before
using this for other players, test:

- Local live MP3 URL: playback begins without waiting for the full stream.
- Volume, stop, play again, invalid URL, missing runtime and 64-bit runtime.
- Server play/stop reaches two opted-in clients; opted-out client stays silent.
- Same URL played twice restarts only on each new request, not every frame.
- Late join, map end playback, next map stop, disconnect and game exit.
- A dead/unreachable URL reports failure and leaves the game responsive.

Treat this as an experimental fork build until those checks pass. Audio stream
reachability, decoder support, Windows sound output and SoFplus cvar delivery
remain live-test dependencies.
