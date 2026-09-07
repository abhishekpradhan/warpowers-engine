# Experimental: GeneralsXWeb WebRTC LAN prototype

**Status: unsupported.** Nothing in this directory is compiled, staged or shipped by War Powers. The files are kept for reference because the engine's browser UDP shim was written against them.

Both files were donated by [GeneralsXWeb](https://github.com/meerzulee/GeneralsXWeb) (commits by Meerzulee, July 2026) and arrived with the browser port:

| File | What it is |
|---|---|
| `boot.html` | GeneralsXWeb's development boot page. It stages retail `.big` archives and loose script files into the Emscripten file system, writes an `Options.ini`, loads `GeneralsXZH.js` with cache busting, mirrors the engine log into the page and shows a room/connection HUD. War Powers boots through the parent workspace's web shell instead; this page still expects retail file names under `/gamedata/`, so it does not run the War Powers dataset as-is. |
| `webrtc_udp.js` | `window.CafeUdp`: UDP over WebRTC DataChannels. It joins a room on a signalling server ("cafe"), opens one unordered DataChannel per peer (bounded retransmits), assigns synthetic `10.0.0.<slot>` addresses from the server roster and exposes `bind`, `send`, `recv`, `close`, `localIP`, `status`, `joinRoom` and `hostIP`. |

## Licensing

Neither file carries a license header. They were published in the GeneralsXWeb repository, whose README states "GPL v3, same as the source it derives from" and whose `LICENSE.md` is the same GPL-3.0 text with Electronic Arts' additional terms as this repository's [LICENSE.md](../../LICENSE.md). They are kept under that license. No MIT grant applies to these two files; the MIT-licensed part of GeneralsXWeb's work is the d8web renderer, which is vendored separately in the parent workspace (`dvijoke/d8web`, with its own `LICENSE`).

## Relation to `udp.cpp`

`Core/GameEngine/Source/GameNetwork/udp.cpp` bridges the engine's `UDP` class under `__EMSCRIPTEN__` to `window.CafeUdp` through `EM_ASM` calls (bind, send, recv, localIP, close). When no `window.CafeUdp` object exists those calls return 0, the engine sees no network, and the game stays single-player, which is the shipped state. Loading `webrtc_udp.js` before the engine script is what would make the in-game LAN lobby discover peers. The comments in `udp.cpp` still name the old location `wasm/webrtc_udp.js`; the file now lives here.

## Signalling server

There is no default signalling server. The donated files pointed at a third party's server and a fixed room; that default has been removed. To experiment you must run, or be given access to, a compatible server and name it explicitly:

- `boot.html?cafe=https://your-signalling-host&room=duel&player=alice` (`?host=1` auto-creates the game, `?autojoin=1` auto-joins the host, `?token=` for gated rooms), or
- from your own page, set `window.CAFE_URL`, `window.CAFE_ROOM`, `window.CAFE_NAME`, optionally `window.CAFE_TOKEN`, and `window.CAFE_ENABLE = true` before loading `webrtc_udp.js`. With `CAFE_ENABLE` set and no `CAFE_URL`, the bridge logs a message and does not connect.

The protocol the bridge expects: a WebSocket at `<CAFE_URL with ws(s) scheme>/room/<room>/ws?name=<name>[&token=...]` exchanging JSON messages `welcome` (`youAre`, `host`, `iceServers`), `roster` (`players` with `id`, `slot`, `host`, `name`), `signal` (`from`/`to`/`data` carrying SDP descriptions and ICE candidates) and `start`. No implementation of that server is part of this repository.

Browser multiplayer and deterministic cross-platform replay compatibility have not been established for War Powers; nothing here changes that.
