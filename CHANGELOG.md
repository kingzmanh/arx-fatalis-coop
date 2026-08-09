# Changelog

Every release keeps its own build. Older versions are never replaced, so if a
new one breaks something for you, the previous download is still there.

## 0.9

**Added**

- A developer console, off by default. Turn it on under Options -> Control with
  the "Console" checkbox, then press ` to open or close it. It can teleport you
  to a level and a marker, spawn any item by name, and suppress the opening
  cinematic so the areas behind it can be reached. `here` copies your current
  position to the clipboard as a command you can paste back later to return to
  the exact same spot.

**Fixed**

- Giving a quest item to someone no longer does nothing. The second player can
  hand over the signed form, the gem dealer's badge, or anything else a quest
  asks for, and it advances for both of you. Whatever is handed back in return
  goes to the player who did the giving, even when the script pays up after the
  conversation rather than during it.
- Cutscenes triggered by the second player now actually play. The host performs
  the scene and the other player is sent a copy to watch; before, each machine
  left it to the other and it played on neither.
- Creatures breathe on the second player's screen. Idle breathing is played once
  and restarted rather than looped, and that restart was invisible over the
  network, so every creature held one pose forever.

**Changed**

- Network protocol 23 -> 24. Both players need this build; older ones are
  refused at the handshake rather than left to desync.

**Known issues**

- Animals only appear once the host has been near them, and only move while the
  host is nearby
- Giving gold instead of a quest item still only counts on the giver's machine
- Loot can duplicate - both players may find their own copy
- Some quest flags only land on one machine
- Enemies sometimes pick the wrong target
- Never properly tested over the internet; almost all testing has been two
  windows on one computer

## 0.8

**Added**

- Magic is learned together. When either player learns a rune the other learns
  it too, and connecting shares whatever each of you already knew, so nobody has
  to go and find the same rune twice. Nothing is ever taken away - runes are only
  ever added.

**Fixed**

- The other player's stance reset about once a second while they carried a
  shield. Equipping a shield starts a looping hold animation on its own layer,
  and that layer was sent without its loop flag or its playhead - so it stopped
  looping, and was dragged back to the start every time it drifted a second from
  the zero it was being told. Every layer now carries both.
- A shield that was put down or swapped left the body drawing one that no longer
  existed.

**Changed**

- Network protocol 21 -> 23. Both players need this build; older ones are
  refused at the handshake rather than left to desync.

**Known issues**

- Animals only appear once the host has been near them, and only move while the
  host is nearby
- The hob-goblin quest item vanishes without advancing the quest
- Loot can duplicate - both players may find their own copy
- Some quest flags only land on one machine
- Enemies sometimes pick the wrong target
- Never properly tested over the internet; almost all testing has been two
  windows on one computer

## 0.7

**Fixed**

- Cooked food was invisible to the other player until something moved it
- Armour could not be seen on the other player
- Armour stopped showing after moving to another area
- The host's armour was destroyed when the second player joined
- The second player spawned holding a copy of the host's weapon
- An item picked up off the ground could end up duplicated
- Fires lit by one player showed only a glow to the other, with no flames
- Saved games rolled back story progress when loaded
- A carried shield could not be seen on the other player
- The other player's idle used the first-person animation
- The second player could be locked under the black bars with no way out
- The host could be locked the same way when the second player crossed a trigger
- The second player was left with no cursor and no hands after a cutscene

**Changed**

- One-shot story triggers now fire once for the world rather than once per
  player, so a scene cannot replay with its props already gone
- The second player no longer holds itself still for a scene it cannot play;
  it watches the copy the host sends instead

**Known issues**

- The other player's breathing animation looks half-finished while they carry
  a shield
- Animals only appear once the host has been near them, and only move while the
  host is nearby
- The hob-goblin quest item vanishes without advancing the quest
- Loot can duplicate - both players may find their own copy
- Some quest flags only land on one machine
- Enemies sometimes pick the wrong target
- Never properly tested over the internet; almost all testing has been two
  windows on one computer

## 0.6

**Fixed**

- Missing fonts and engine data that made the game fail to start for anyone who
  was not building it themselves
- The package no longer overwrites the `arx.exe` of an existing installation

**Changed**

- First release that can be extracted and double clicked, with every library it
  needs beside it
