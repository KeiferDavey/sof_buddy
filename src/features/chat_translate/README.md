# Automatic English chat translation (slot-filter v6 test build)

This client feature uses DeepL API Free over HTTPS. It runs translation on a
background worker, retains the original chat, and prints a local English line
through the normal game console/notification output. Other players do not see
these translations. No Python, VLC or server addon is needed for translation.

## Install and test

1. Close SoF. Back up your current `sof_buddy.dll` and replace it with the DLL
   in this package's `client/` directory. Keep your existing working Buddy and
   SoFplus setup. On a fresh installation, copy the contents of `client/` to
   the SoF game root and use `sof_buddy/enable_sofplus_and_buddy.cmd`.
2. Create a DeepL **API Free** account and get its API key:
   https://www.deepl.com/en/developers
   The ordinary free website translator account is not an API credential.
3. Create `sof_buddy/deepl.key` beneath the game root. Put ONLY your API key
   on one line. Make sure Windows has not named it `deepl.key.txt`.
   This is a private plaintext file: keep it out of shared ZIPs and source control.
   The addon does not echo it or store it as a console variable/userinfo.
4. Start the game and enter these commands in the CLIENT console:

   ```text
   set _sofbuddy_translate_enabled 1
   sofbuddy_translate_reload
   sofbuddy_translate_test "Bonjour, bonne partie!"
   ```

   Expected asynchronous output:

   ```text
   [translate] Test [FR -> EN]: ...English translation...
   ```

5. Join a server and have a player send a French/Italian/German/etc. message.
   Standard `Name: message` and SoFplus `Name : [slot] message` chat are recognized. Example display:

   ```text
   Julien: Bonjour, bonne partie!
   [translate] Julien [FR -> EN]: Hello, good game!
   ```

   Exact wording and detected language are determined by DeepL.
6. If it does not work, run `sofbuddy_translate_status` and note the output.
   `chat_hook=installed` confirms installation, not successful chat recognition.
   If the manual test works but `matched=0` after foreign chat, the server's
   capture path or format still needs attention. The status now also includes
   `capture=console` (lower console writer) or `capture=printf` (fallback), `seen`
   (captured print chunks), and `cls` (8 means in-game). Capture an exact example
   and these counters if the issue persists. Names containing colon, multiline
   messages, and nonstandard server chat formats may still be missed.

## Settings and controls

- `_sofbuddy_translate_enabled`: 0 default (off), 1 enables translation.
- `_sofbuddy_translate_codepage`: 1252 default for legacy Western European
  input bytes. Valid UTF-8 is accepted automatically first. For legacy Cyrillic
  use 1251; for Central European input try 1250. Supported fallback code pages
  are 1250..1258, 437, 850, 866 and 65001. This cannot recover characters the
  original game's font/input has already lost.
- `sofbuddy_translate_reload`: reload the private key and reset a quota/key pause.
- `sofbuddy_translate_status`: show enabled/key/hook status and request counters.
- `sofbuddy_translate_test "text"`: test the API independently of chat capture.
  Explicit tests always display a response, including English detection.
- `set _sofbuddy_translate_enabled 0`: disable and discard pending results.

Settings persist through Buddy's existing configuration mechanism; the key
stays in its separate local file. Translated output targets English, converted
to legacy Windows-1252 for display. Characters unsupported by the game font may
show as `?`; this build does not add Unicode fonts.

## Behavior and limits

Chat capture remains a conservative **format heuristic**, not an engine
PRINT_CHAT discriminator. Capture v2 tries the shared console writer below
SoFplus's print wrapper, verifying its callsite in the executable first; it falls
back to Com_Printf if that verification fails. It assembles split print chunks,
removes palette controls, and strips a SoFplus [0..31] slot marker. A connected-client console/server line shaped exactly
like a player chat line may be recognized too. The API receives only message
text; the speaker name stays local. Recognized team/private messages are also
sent to DeepL. Enable only if you want that behavior.

DeepL detects the language. English messages are sent too and can use allowance,
but their returned translations are suppressed. No unreliable local language
classifier is used. The service can misidentify very short messages/slang.

A single worker sends at most four uncached requests per second, with up to eight
pending messages; excess messages are dropped. It caches up to 256 distinct
messages, including English results, in memory only. Cache entries are shared
between speakers; returned translations keep the current speaker label.

HTTP 456 (quota) and 401/403 (credentials) pause further requests until an
explicit reload/key change. Disabling/re-enabling or reconnecting does not
bypass that pause. HTTP 429 and connection/server errors back off 30 seconds;
failed messages are not automatically retried. Status counters are per process.

Map/connection changes, disabling and key reload discard stale queued/results.
The active network request may still finish and consume quota. Timeouts bound
network operations; game exit may wait for an in-progress request to finish.
HTTPS certificate validation is enabled; authenticated redirects are disabled.
No external API key, endpoint, or command can be supplied by a game server.

## Validation and implementation

- Windows 32-bit release DLL compiles and links.
- New Windows source files compile with -Wall -Wextra -Werror.
- Native tests cover filtering, escaped JSON, Unicode decoding, output control
  sanitization, key validation and variadic print forwarding.
- Mock-transport worker tests cover queue overflow, rate limiting, stale-work
  cancellation, cache behavior, quota pause and explicit reset.
- No live DeepL request has been tested: no real API key was provided.
- No Windows/SoF runtime test has been performed. This is an experimental build.

Com_Printf is pointer-only in the existing generator. The feature retains a
manual variadic detour at its known upstream entry after Cvar initialization,
formats the complete va_list and forwards it using a fixed `%s` format. This
avoids losing variadic arguments and treating chat text as a format string.

API reference:
https://developers.deepl.com/api-reference/translate/request-translation

Picojson v1.3.0 (BSD 2-clause license, vendored unchanged) parses/serializes JSON:
https://github.com/kazuho/picojson/tree/v1.3.0
Its license is retained in vendor/picojson.h and the package THIRD_PARTY.txt.

This DLL also retains the earlier URL-radio feature. That feature alone still
needs its separately installed 32-bit VLC runtime; translation does not.

## Capture v2 changes

The supplied in-game log showed key=loaded and enabled, but matched=0 and
API requests=0. The actual chat was `Name : [0] bonjour, bonne`. Its printed
text passes the original recognizer, so a bypassed print hook or hidden controls
were possible causes. Capture v2 adds a guarded lower console-writing hook,
split-call assembly, colour normalization and slot stripping. Tests now cover
the actual legacy player-name bytes and format from that log. Runtime operation
still needs testing; successful DLL installation alone does not verify capture.

The correct API-only test includes its text argument:
`sofbuddy_translate_test "Bonjour, bonne partie!"`
Entering only `sofbuddy_translate_test` prints usage and makes no API request.

## Output v3 changes

The follow-up log showed API requests=1 and cached=2, confirming that a parsable
response had been cached, but no response was displayed. V3 also pumps results
from Buddy's existing console/notification drawing overrides and from the status
command, rather than depending solely on Qcommon_Frame. Explicit API tests always
show their response, even when the detected source is English. Status now includes
frames, pumps, displayed, last_source and last_result. Automatic chat capture
remains unresolved for the user's executable: v2 selected the printf fallback.
The actual SoF.exe is needed to identify and validate its lower console routine.

## Automatic capture v4

The supplied executable confirmed Con_Print at RVA 0x20AE0. Its Com_Printf call
at RVA 0x1C795 is followed by a six-byte register load before `add esp,4`.
The earlier guard required immediate stack cleanup, incorrectly rejecting this
correct callsite. V4 accepts this exact verified sequence too; regression tests
use its actual instruction bytes. The uploaded executable is inspected only,
not modified or included in this package.

After replacing the DLL, retain sof_buddy/deepl.key and enable translation:
`set _sofbuddy_translate_enabled 1`
Send ordinary non-English chat, e.g. `bonjour, bonne partie!`, without running
the test command. Expected startup: Capture v4: shared console writer verified
and installed. Expected status: capture=console; matched/queued increase on chat.
Translations appear automatically in local console/notification output.
This captures message text and lets DeepL detect its language. English chat may
use quota, but English translations are suppressed. Runtime automatic capture
still requires the user's in-game validation.

## Latency v5

Uncached requests now have a 250ms minimum pause rather than one second.
Cached results bypass that pause. Rate-limit/error backoff remains unchanged.
Network latency and queued work can still delay responses; no instant-translation
promise is made. SoFplus Client [slot] and Incoming [slot] diagnostics are ignored,
as are a small list of common English greetings/chat slang (hi, hello, hey, lol,
lmao, rofl, ok, okay, gg, gl, hf, thx, ty, np, brb, afk), including simple punctuation.
This avoids the API's false language guesses for these short English tokens.
Profanity is not filtered. English text remains suppressed after API detection,
whether profane or not. The supplied status log confirmed English_suppressed for
the English chat; caching is independent of that display decision.

## Startup regression rollback and slot filtering (v6)

The lower console-writing detour introduced in v4 is removed. The user's working
logs already showed automatic translation through printf capture. V6 restores
that path and keeps the render/status result pumps and v5 latency changes.
The additional detour is suspected, not confirmed, as the startup failure's cause;
no Windows crash trace was supplied. Startup must still be tested on Windows.

Only parsed chat with a valid [0..31] player-slot marker is eligible for automatic
translation. The local connection slot is read from SoFplus's _sp_cl_info_slot.
Own-slot messages are skipped before API requests by default. If the local slot
is unknown, automatic requests pause to avoid sending your own chat accidentally.
Unslotted notices are always skipped, including in debug mode.

For testing your own chat:
set _sofbuddy_translate_debug_self 1

Restore normal other-player-only operation:
set _sofbuddy_translate_debug_self 0

The API-only sofbuddy_translate_test command remains available as an explicit
manual debugging command. Status includes local_slot, debug_self, skipped_self,
skipped_unslotted and skipped_unknown_local. If local_slot=-1 while connected,
report this value; the local-slot cvar was not populated by that SoFplus setup.

This filters textual slot markers; it does not authenticate the sender against
an engine player roster. Names and slot markers stay local; only text goes to DeepL.
