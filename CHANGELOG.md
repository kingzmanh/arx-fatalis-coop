# Changelog

Every release keeps its own build. Older versions are never replaced, so if a
new one breaks something for you, the previous download is still there.

From 0.13 on, changes come in three honest columns: **what works** has been
seen working, **known issues** are broken and understood, and **needs testing**
was built with care but has not yet been proven in a live game - if you try
one of those and it misbehaves, that report is exactly what we need.

## 0.14

Cutscenes and the jail hole, fixed by playing them until they behaved.

**What works** (played and confirmed)

- The hole in the jail floor works for both of you. It used to be a one-way
  trip for whoever jumped first: the script disarms the hole behind itself,
  and in co-op that disarming landed on the other player's world too, leaving
  them standing in a hole that had stopped being a hole. Now each of you gets
  your own jump - and jumping second no longer freezes you at the bottom for a
  moment while the level reloads underneath you.
- Cutscenes play for whoever walks into them, including the ones nobody
  triggers on purpose. Some scenes have no cause to read - a prisoner notices
  you standing there, or starts talking when he finishes walking to a spot -
  and those always seized player one, camera, black bars and all. They now
  belong to whichever of you the scene is happening to, and so does the head
  turn that goes with them.
- The camera films the right player. Scenes name the ROLE now, not the body:
  "the player" means the one watching the scene, so the camera stops framing
  your partner across the room. It also glides through its sweeps instead of
  jerking, and a scene that ends gives BOTH of you your controls back at the
  same moment.
- Starting a New Quest really starts one. The list of story moments already
  lived was cleared in memory but never on disk, so a brand new game skipped
  scenes it had never played - and a skipped scene left the screen locked
  behind a camera nobody released.
- Picking up a glowing item no longer leaves it glowing forever in your pack.

**Known issues**

- Some cutscenes may still not play properly for player two. The ones in the
  jail are fixed and proven; the rest of the game's scenes have not been
  played through by two people yet, and this is exactly the kind of report
  that helps.
- Selling to a shop pays both players, and gold dropped by one of you and
  picked up by the other is counted twice. Money can be made out of nothing
  this way; nothing else breaks.
- Repeating lines - a sleeping guard's snoring, idle muttering - are
  remembered as if they were story moments, so they may fall silent after
  their first play.

**Needs testing** (built with care, not yet proven in a live game)

- In-game chat. **Y** opens a gold line at the bottom of the screen in the
  game's own font, **Enter** sends it, **Escape** throws it away. What you
  send appears on both screens with your name in front. The world does not
  pause while you type, and while the line is open the game ignores your
  controls, so spelling "sword" does not draw one.

**Changed**

- Same network protocol (30) as 0.13; the two versions still connect.

## 0.13

**Needs testing** (new in this release - built, reviewed, not yet played)

- When the story force-moves one player, both go. Captures, the snake-women
  sending you below, the endgame - the scripts that grab "the player" and put
  them somewhere assumed one hero, and would have left the other player
  stranded wherever the story was a moment ago. Now the partner arrives at the
  same place, whichever of you triggered it. Doors, levers and elevators are
  untouched - the other player can always use those for themselves.
- A cutscene that repositions its viewer now moves the right player. Scenes
  like reading Falan's note place "the player" before the camera rolls - and
  because scripts run on the host, the second player's scene used to move the
  FIRST player's body. The move (and the facing) now goes to whoever the scene
  belongs to; the other player feels nothing.
- Quests that ask you to pay now work for both players. Paying the barmaid,
  the goblin lord, or the ice dragon's toll used to happen only on the giver's
  screen - the world never heard about it. Payments now run where the quest
  lives, the wallet that is checked and emptied is the giver's own, and what
  the payment buys happens for both of you.

**Changed**

- Network protocol 29 -> 30. Both players need this build; older ones are
  refused at the handshake rather than left to desync.
- The save inspector that ships with the engine's tools could not show a
  level's saved state ("bad version: 0") - an inverted check, present
  upstream. Fixed; the game itself never runs that code.

**Known issues**

- Never properly tested over the internet. Almost all testing has been two
  windows on one computer. Real latency will find bugs that local testing
  cannot.
- Loot can duplicate - both players may find their own copy.
- Enemies sometimes pick the wrong target in crowded fights.

## 0.12

**Added**

- The second player creates their own character on the first join - face,
  attributes, skills, the same screen the first player gets at New Game. Asked
  once per playthrough; every later join restores the character you made. And
  your face is yours: choosing one no longer changes how the other player looks.

**Fixed**

- The world now lives around the second player. Creatures near them alone used
  to be frozen and invisible until you stood right on top of them - the engine's
  room-graph distance claimed a pig at arm's length was a ten-thousand-unit walk
  away, so nothing near you was simulated or sent. Everything within range of
  either player is awake now.

**Changed**

- Network protocol unchanged (29): 0.11 and 0.12 can play together. Both players
  on the same version is still the recommendation.

**Known issues**

- Never properly tested over the internet; almost all testing has been two
  windows on one computer
- Loot can duplicate - both players may find their own copy
- Enemies sometimes pick the wrong target in crowded fights

## 0.11

**Added**

- An installer, for anyone who would rather not copy files into a game folder by
  hand. It finds Arx Fatalis on its own - Steam, including libraries on other
  drives, GOG, or a copy you built yourself - checks the folder really is the
  game before touching anything, and leaves a proper uninstaller behind. The zip
  is still there and is the same files; use whichever you prefer.

**Fixed**

- Creatures now fight whoever is actually hitting them. With both players
  present a creature committed to the first and stayed there - the second could
  stand behind it hacking away while it walked at someone who had not moved,
  because the only thing being weighed was who stood nearer. Whoever lands a
  blow now holds its attention for a few seconds, before any question of sight
  or distance.
- Quest items glow for the second player. Things the game lights up to say "this
  one matters" were plain scenery on their screen unless the glow happened to be
  on before the level loaded.

**Changed**

- Network protocol 28 -> 29. Both players need this build; older ones are
  refused at the handshake rather than left to desync.

**Known issues**

- Never properly tested over the internet; almost all testing has been two
  windows on one computer
- Animals only appear once the host has been near them, and only move while the
  host is nearby
- Loot can duplicate - both players may find their own copy

## 0.10

**Added**

- Cutscenes play for whoever walks into them. Trip a scene as the second player
  and it is yours - camera, black bars, dialogue and all - while the other
  player carries on with whatever they were doing. A slider on the co-op menu
  switches this to *player one only* if you would rather the host saw everything.
- Either player can skip a scene they are watching, and it ends for both.
- Talking to a character is answered by the one who actually remembers you, so
  people you have already met carry on where you left off.

**Fixed**

- A cutscene the second player triggered played on neither screen. A scene in
  this game is not one thing but six - an NPC's script, a camera entity's own
  script, a path, a target, a deferred jump and the queued events between them -
  and each had to be told whose scene it was. See FIXES.md for the full list;
  the short version is that the camera never moved, never aimed, and the
  subtitles were switched off by a machine that was not the one watching.
- The black bars stayed on screen after a scene ended.
- The goblin lord had nothing to say to the second player, before or after being
  given a quest item.

**Changed**

- Network protocol 24 -> 28. Both players need this build; older ones are
  refused at the handshake rather than left to desync.

**Known issues**

- Never properly tested over the internet; almost all testing has been two
  windows on one computer
- Animals only appear once the host has been near them, and only move while the
  host is nearby
- Loot can duplicate - both players may find their own copy
- Enemies sometimes pick the wrong target

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
