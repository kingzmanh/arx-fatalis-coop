# Changelog

Every release keeps its own build. Older versions are never replaced, so if a
new one breaks something for you, the previous download is still there.

## 0.8

**Fixed**

- The other player's stance reset about once a second while they carried a
  shield. Equipping a shield starts a looping hold animation on its own layer,
  and that layer was sent without its loop flag or its playhead - so it stopped
  looping, and was dragged back to the start every time it drifted a second from
  the zero it was being told. Every layer now carries both.
- A shield that was put down or swapped left the body drawing one that no longer
  existed.

**Changed**

- Network protocol 21 -> 22. Both players need this build; older ones are
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
