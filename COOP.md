# Arx Fatalis Co-op (fan-made)

> ### ⚠️ WORK IN PROGRESS - EXPECT ANYTHING
>
> This is an unfinished experiment shared early. It will have bugs, some of
> them strange ones. Things that worked yesterday may break tomorrow. Enemies
> may behave oddly, items may misbehave, a session may desynchronise or crash.
> **Do not play this on a save you care about** - it writes its own files and
> a future version may not read them.
>
> It is not endorsed by Arkane Studios, ZeniMax, or the Arx Libertatis team.
> It is one person's mod, made for fun.

Two players in one Arx Fatalis. Not a split of the game into client and
server - both machines run the whole engine, and one of them is the authority
on the shared world.

---

## What you need

- **Your own copy of Arx Fatalis** (Steam or GOG). No game data is distributed
  here and none ever will be - it belongs to Arkane / ZeniMax. This mod is only
  an engine build.
- Windows. (Linux and macOS are possible - the engine is cross-platform - but
  nothing has been built or tested there yet.)
- A way to reach each other: the same local network, a virtual LAN such as
  Radmin VPN, Hamachi or ZeroTier, or a forwarded UDP port on the host's router.

## Install

1. Download the release archive.
2. Unzip it **into your Arx Fatalis folder** - the one holding `data.pak`.
   On Steam that is usually
   `...\steamapps\common\Arx Fatalis`.
3. Run `arx.exe`.

Nothing is overwritten. Your original game keeps working exactly as before;
this is an extra executable that reads the same data files.

If you keep the mod somewhere else instead, start it with
`arx.exe --data-dir "C:\path\to\Arx Fatalis"`.

## Playing together

1. Both players start `arx.exe`.
2. **Host**: Options → CO-OPERATIVE PLAY → HOST GAME, then start or load a game.
3. **Guest**: type the host's IP address, then JOIN GAME.

The port is automatic (27100). Tick **USE PORT** only if that one is taken or
your router was set up by hand.

Over a virtual LAN, use the address the VPN gives the host - nothing else to
configure. Over the open internet the host must forward **UDP 27100**.

The second player does not need a save of their own. They receive the host's
world on joining, and their own character - stats, inventory, equipment - is
kept between sessions on their own machine, keyed to that playthrough. Rejoin
the same game and your character is exactly as you left it.

## What works today

- Both players in one world, seeing each other move, fight and cast in real time
- Shared enemies, doors, levers, loot and story progress
- Full collision: enemies block corridors and can be fought the way the game
  intends, for either player
- Each player has their own health, mana, inventory, equipment and progression
- Story cutscenes play for both players; a scene one player has lived through
  is never forced on the other twice
- Travel between levels, including the holes and doors the story pushes you
  through
- The other player's health shown as a second life orb above your own
- Reviving a downed partner by standing over them
- Reconnecting after a dropped connection

## What does not work yet

Honest list. These are known, not surprises:

- **Loot duplicates.** Both machines run the same loot scripts, so two players
  can each find their own copy in the same corpse.
- **Quest flags can drift.** They are shared when you join, but afterwards
  anything only one machine executed - a lever the guest clicked, an enemy that
  died - may not reach the other.
- **Anything the second player does alone is forgotten.** Areas visited without
  the host are a private copy. Play together.
- **Two players only.**
- **No chat, no trading.** To give something, drop it on the floor.
- **Never tested at real internet latency.** Everything so far has been LAN.
- **Windows only.**

## Building from source

MSYS2 mingw64, GCC 16. Unity build and LTO must stay off:

```
cmake -DUNITY_BUILD=OFF -DUSE_LTO=OFF -DICON_TYPE=ico ..
ninja arx
```

ENet comes from `mingw-w64-x86_64-enet`.

The co-op layer lives in `src/net/` - five pieces: the session, the shared
world, the other player, motion smoothing, and the wire format. Everything
else is changes to the engine, which git will show you as a diff.

## Reporting a bug

Please include `arx.log` from **both** machines - most co-op bugs are only
visible when the two are read side by side. On Windows it lives in
`Documents\My Games\Arx Libertatis` or `Saved Games\Arx Libertatis`.

## Licence and credit

GPLv3, inherited from [Arx Libertatis](https://arx-libertatis.org/), the open
source engine this is built on. Their work made all of this possible, and the
original game is Arkane Studios'. Source is here in full, as the licence
requires.
