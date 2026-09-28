# WarCraft 2 Remastered+ network protocol and local files

This document describes everything the mod sends and receives from other players, along with every file it writes. 

- The mod adds three message types to the game's existing lobby channel: a handshake (`H`),
  map sharing (`M`) and lobby settings (`L`). All three are **lobby-only**.
- Players without the mod never receive anything but the handshake announcement, which their
  game ignores. Everything else is sent to one player at a time, and only to players who have
  announced the mod.
- No changes are made that would prevent unmodded players from playing with modded players.


## 1. Transport

Every message uses the game's own peer-to-peer lobby channel as a message type the base game does
not use. Receivers recognise it by its first five bytes:

```
[0]      0xE7            message type (unused by the base game)
[1..3]   'W' '2' 'R'     magic -- this, not the type byte, marks a message as ours
[4]      subsystem       'H' handshake | 'M' map sharing | 'L' lobby settings
[5..]    the subsystem's own bytes; every multi-byte integer is little-endian
```

Implementation: `src/core/peer_messages.{h,cpp}`.

- Messages are removed from the stream before the game sees it, so the
  game only ever processes its own message types.
- Every receiver uses the network slot the
  game's transport reports, never a slot named inside a message. The host is always net slot 0.
- Sending and acting on anything received both require the waiting lobby to have been seen
  recently: within the last 500 ms for the handshake and lobby settings, and 2,000 ms for map
  sharing. Each subsystem keeps its own timestamp:
  - The handshake and map sharing refresh theirs only when the lobby window is drawn.
  - Lobby settings also refreshes its timestamp from the game's dropdown function and from
    the game's team-change and slot-swap request handlers, which another player can trigger by
    sending those requests. That only keeps the lobby-settings state alive; it never makes the mod
    send anything.
- In a match, the game's message pump stops at unknown message types, so the mod never sends there.
- Our sends go straight to the transport and never through the game's own
  send path, whose failure handling ends the session.
- Messages use the game's reliable, in-order peer channel: it
  resends anything unacknowledged and delivers each player's messages in the order they were sent and drops players who have been silent for 8 seconds. The mod never resends a lost message
  itself; the only repeats it makes are protocol-level. The one thing it must avoid is a message too large for the network path, which that channel
  would resend endlessly, but every message here is at most 989 bytes.
- Every base-game type `0x2a` is dropped since it belongs to a leftover file-transfer
  feature the base game never uses that can write files to disk. This mod refuses these messages altogether.

## 2. Handshake — subsystem `'H'`

Lets modded players find each other in a lobby. Implementation: `src/features/peer_handshake.cpp`.

| Offset | Size | Field |
|---|---|---|
| 5 | 1 | protocol version, 1 |
| 6 | 1 | kind: 1 HELLO, 2 REPLY |
| 7 | 4 | nonce: chosen on each lobby entry, identifies this instance |
| 11 | 4 | capabilities: bit 0 handshake, bit 2 map sharing enabled |
| 15 | 4 | simulation hash: always 0 (nothing that changes the game simulation exists) |
| 19 | 16 | mod version, ASCII, NUL-padded |

35 bytes. Longer messages are accepted and the extra bytes ignored, so a later version can append
fields.

- **HELLO** is **broadcast** when a player enters a lobby, when another player joins, or
  this player's map-sharing setting changes. It is the
  only message a player without the mod receives, and the base game ignores it.
- **REPLY** is sent to the HELLO's sender only, once per instance. A REPLY is never answered, so two
  modded players cannot loop.
- Messages that have another version or are shorter than 35 bytes are ignored.
- The nonce mixes the performance counter, the tick count and the process id. It only needs to be
  unique within a lobby. It tells a player's own broadcast from another's, and a rejoin from the same
  player, so it is not cryptographically random and is not a secret.
- A net slot that presents a new nonce within 5 s of its last one is ignored, so cycling nonces
  cannot flood the log or draw a REPLY each time.
- The version string is shown only after anything outside printable ASCII is replaced with `?`.
- What it's used for: the green dot beside modded players, the crown beside the host, and deciding
  who may be sent `M` and `L` messages at all.

---

## 3. Map sharing — subsystem `'M'`

A player who joins a lobby without the selected map can agree to download it from the host. Implementation: `src/features/map_download/` (`wire.cpp` is the decoder; `limits.h` holds every
limit below).

### Roles

- Only the host serves and other players request. There is no
  player-to-player transfer.
- Every message goes to exactly one player who has the mod.
- The request carries the map's name and not a path. The host answers only if that name exactly equals the map name its lobby is showing. It then sends the map file the game currently has selected, read from the game's Maps folders. The name is only compared and not used to open or search for a file, so no request can make the host send any other file.

### Messages

The header is 23 bytes: the envelope, version (1), kind, and a 16-byte request id chosen at random by
the requesting player (`BCryptGenRandom`; if that fails, nothing is requested).

| Kind | Name | Direction | Payload after the header | Exact length |
|---|---|---|---|---|
| 1 | REQUEST | player → host | `nameLen u8` (1–64), `name` | 24 + nameLen |
| 2 | OFFER | host → player | `size u32`, `sha256[32]`, `chunkLen u16` | 61 |
| 3 | DENY | host → player | `reason u8` | 24 |
| 4 | ACCEPT | player → host | — | 23 |
| 5 | CHUNK | host → player | `offset u32`, `len u16`, `data[len]` | 29 + len |
| 6 | ACK | player → host | `nextOffset u32` | 27 |
| 7 | CANCEL | either | `reason u8` | 24 |
| 8 | DONE | player → host | `result u8` | 24 |
| 9 | PROGRESS | host → each modded player | 8 × (`state u8`, `percent u8`), request id zero | 39 |
| 10 | HAVE | player → host | — (answers an OFFER: "I already have a map with that hash") | 23 |
| 11 | LEAVING | player → host | `why u8`, request id zero | 24 |

**Any message whose length is not exactly the length its kind defines is dropped**, as is any unknown
version, kind, reason or state. No kind has an optional field.

### Flow

```
player: the host selected a map (every modded player asks about every selected map, including
        one it has -- the OFFER's hash is how it checks its own copy is the same file)
  -> REQUEST(name)
host:   is it the selected map, and does the file pass the checks below?
  -> OFFER(size, sha256, chunkLen)               otherwise DENY(reason)
player: has a file with that hash?  -> HAVE, and the exchange ends
player: asked "download this map?" -- the default is to ask (auto-download is off by default)
  -> ACCEPT                                      otherwise CANCEL; the host then removes the player
                                                 (with the enhanced lobby off nothing can ask, so
                                                 the offer ends without removal and the game's own
                                                 missing-map message is shown)
host:   CHUNK(offset 0, 960, 1920, ...)          at most 8 unacknowledged, paced
player: ACK(nextOffset) for each in-order chunk
  ... last byte ...
player: SHA-256 equal to the OFFER's?  map passes validation?  -> write it, then DONE
```

### Limits (all enforced on the side they protect)

| | |
|---|---|
| map size | 1 byte to 262,144 bytes |
| chunk | at most 960 bytes; at most 8 unacknowledged; at most 64 chunks/second from the host |
| name | 1–64 bytes, printable ASCII |
| consent | the prompt gives the player 15 s; the host removes a player who has not answered 18 s after it sent the offer (the extra 3 s covers the delay before the offer arrives) |
| stalls | an upload that makes no progress (a full 8-chunk window) for 15 s ends with the player removed |
| deadline | an upload not finished 120 s after ACCEPT ends with the player removed (the largest map needs about 5 s); the player gives up on its own at 120 s too |
| refusals | at most one DENY per player per 5 s; further requests inside that time are dropped unanswered |
| retries | a player refused for now (busy, rate-limited, "not the current map" while the lobby still shows that map, or no answer within 10 s) asks again after 5.5 s, at most 12 times per map; any other refusal ends the attempt |
| requests | at most one offer per 5 s per player (a request inside that time is refused, and refusals are themselves limited, above). Per player per stay in the lobby: at most 32 uploads (accepted offers); at most 2 completed downloads of the same map (a completion counts whether it ends with the last ACK or with DONE); and at most 3 offers of the same map that end without an upload (answered HAVE, or cancelled). Checking many different maps costs nothing. Leaving and rejoining starts a new stay |
| concurrency | at most 2 uploads running at once per host -- an offer still waiting for its answer does not take one; an ACCEPT that finds both running is cancelled as busy, and the player asks again. One download at a time per player |

### What a player accepts

- An OFFER only from the host, only for its own pending request, only with a size and chunk length
  inside the limits. Memory is allocated only after the player accepts, at exactly the offered size.
- CHUNKs strictly in order: the offset must equal the bytes received so far, and the length may not
  run past the offered size (checked by subtraction, so it cannot wrap). A duplicate or a gap is
  dropped.
- The finished file must hash to the SHA-256 in the OFFER, and pass the map validator
  (`pud_validator.cpp`) which checks:
  - The file's section structure
  - Sizes
  - Dimensions (square, as every real map is)
  - Players
  - Tile codes
  - Terrain flags (SQM) and oil map (OILM)
  - Unit types
  - Unit sizes
  - Unit owners
  - Unit positions
  - Custom unit and upgrade data (every field the game reads)
- Any map not passing these checks will be discarded before being written to disk.
- A map that passes is still loaded by the game's own map code, which this project hasn't audited (This is the same risk as downloading and running maps from a third party source). If you'd rather not take that risk, disable map sharing.
- **Not checked:** 
  - The region map (REGM). Real maps use nearly every possible value, so a limit would restrict nothing.
  - Which units, spells and upgrades each player may use (ALOW).
  - Starting resources (SGLD, SLBR, SOIL).
  - The value field of each unit (a gold mine's or oil patch's resource amount).
  - The map description's text and category (DESC, SIGN) content
  - Custom unit and upgrade data when the map says to use the defaults, because the game ignores it then.
- The hash proves the bytes arrived as the host sent them. It does **not** prove where they came
  from: the host chooses the hash along with the file, so the validator, not the hash, is what stands
  between a hostile host and the game.
- A failed hash or validation is not retried for that map while it stays the lobby's map. If the
  host selects another map and later the same one again, it can be requested again.

### Where a downloaded map goes

Maps will be downloaded to `<game>\x86\Maps\Download\V<n>\<map name>`, inside the game's own Maps folder since the game only searches for maps there. The `V<n>` folders are used so that different maps with the same name can still be downloaded and used, and nothing is ever overwritten. The name must be printable ASCII and have no path
separators, reserved characters, or Windows device names, and must end in `.pud`. A name that fails is
not downloaded. The `Download` folder is capped at 64 MB and 512 files, and one name at `V1`–`V99`;
past that, nothing more is downloaded. The `Download` and `V<n>` folders are checked not to be
links when they are used (a check at that moment, not a lock held for the whole write), and the
file is written to a `.part` file first
(created fresh, replacing any `.part` left behind by an earlier attempt) and then moved into place
without replacing anything: if the final name exists by then, the download is discarded.

### Progress

The host sends PROGRESS so every modded player can show each download's percentage beside the player
downloading. The host's Start button stays disabled while a download is pending.

---

## 4. Lobby settings — subsystem `'L'`

The host's lobby settings, for modded players' screens. Implementation: `src/features/lobby_policy.cpp`.

| Offset | Size | Field |
|---|---|---|
| 5 | 1 | protocol version, 1 |
| 6 | 1 | flags: bit 0 teams locked, bit 1 slots locked |
| 7 | 1 | password length, 0–23 |
| 8 | n | the game's password, ASCII |

- Sent by the host only, to each modded player, when a setting changes and every 2 seconds until the game's start countdown. Accepted only from net slot 0.
- Teams and slots are controlled by the host (in the base game), so the corresponding locks are enforced by the host. If teams are locked, the host refuses team changes from other players in the game's own request handlers. Same thing for slots. This makes the feature still work when non-modded players are in the lobby.
- The password is included so modded players can see it in the lobby **after joining with the password**. This does not show the password before joining.
- A build without the password field reads the first 7 bytes and ignores the rest.

---

## 5. Files written on your computer

Everything but downloaded maps lives in one folder:

```
%USERPROFILE%\Saved Games\Warcraft2Remastered\wc2r-plus\
    config\      one <feature>.json per feature. The mod's settings
    logs\        wc2r-plus.log (moved to wc2r-plus.1.log at startup once it passes 5 MB; at most
                 20 MB is written per game launch, after which logging stops until the next)
                 chat-<date>_<time>.log, one per game launch, only if "Log Chat to Disk" is on (off by default)
    resources\   caches the mod rebuilds if deleted (a map-hash index, a map search index)
```

`wc2r-plus.log` is a diagnostic log, and it records what happened in the lobbies you joined: other
players' names and net slots, the maps selected, and the mod's side of each exchange. Nothing leaves
your computer from it; look it over before sharing it anywhere.

The mod's settings screen also shows some of the base game's options. Changing one of those saves it
the way the game's own options screen does, into the game's
`Warcraft2.ini` in `Saved Games\Warcraft2Remastered\`.

Downloaded maps: see section 3. The mod writes nothing else, and reads only its own files, the game's
own files and settings, and the maps in the game's Maps folder.

Deleting the `wc2r-plus` folder resets the mod to its defaults.

---

## 6. Release integrity

Each release lists the SHA-256 of its files in `SHA256SUMS.txt`, and the files are built from this
repository by GitHub Actions. Like the OFFER hash, the checksums prove a file is the one the release
published, not who made it: the files are not code-signed, so Windows may give a warning about the installer.
