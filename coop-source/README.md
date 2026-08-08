# Arx Fatalis Co-op

**Two players, one Arx.** A co-op mod for [Arx Libertatis](https://arx-libertatis.org/),
the open source version of Arkane Studios' *Arx Fatalis*.

> ### ⚠️ WORK IN PROGRESS - EXPECT ANYTHING
>
> This is early. It will have problems. Things will break in ways nobody has
> seen yet, because almost nobody has played it yet. If that sounds like fun,
> read on. If you want something polished, come back later.

![Both players, both screens](docs/screenshots/two-players.png)

*One room, two screens. Each player sees the other move, fight, and pick things up.*

---

## What this is

Arx Fatalis is a single player game. This makes it a two player one.

You can join anywhere in the game, not just at the beginning - whatever point
the host is at, you turn up there and carry on together.

One warning: if you join partway through for the first time, your character is
still level 1, and you will be dropped somewhere you are not ready for. Starting
from the beginning together is much better.

![Waking up together](docs/screenshots/waking-up.png)

## Honest words about who made this

**I do not know how to code.** I built this by working with AI, step by step,
over a long time - testing, breaking things, finding out why, and going again.
It took far longer than I expected. There is a file in this repository called
[FIXES.md](FIXES.md) that lists every problem found and how it was solved,
which is probably the most honest record of how this was actually built.

So: this is not the work of an experienced engine programmer. It is the work of
someone stubborn with good tools. It runs, two people can play it, and I am
proud of that - but there will be things in here that make a real developer
wince.

**If anyone wants to help, I would genuinely appreciate it.** Bug reports,
fixes, or just telling me what broke and what you were doing at the time. That
last one is worth more than you would think.

![The story, together](docs/screenshots/the-story.png)

## What works

- **Playing the whole game together** - though nobody has finished a full
  playthrough yet, so treat that as untested rather than proven
- **Seeing each other** - position, animation, the weapon in their hand
- **Fighting together** - creatures notice both of you and fight both of you
- **Items** - picking up, dropping, throwing, carrying
- **Doors, levers, and travelling between areas** together
- **Both health bars** on screen, so you can see when your friend is in trouble
- **Proximity voice chat** - speak and it comes out of your character's mouth,
  fading with distance and coming from the direction you are standing in
- **Automatic port forwarding** - most routers will open the port by themselves,
  so hosting usually needs no setup

## What does not work yet

Being straight with you, because you will find these anyway:

- **Never properly tested over the internet.** Almost all testing has been two
  windows on one computer. Real latency will find bugs that local testing cannot.
- **Loot can duplicate** in some cases - both players may find their own copy.
- **Some quest flags only move on one machine**, which can leave the two of you
  out of step on story progress.
- **What the second player does alone is not always remembered** if they explore
  away from the host.
- **Enemies sometimes pick the wrong target** after one player leaves a room.

## Playing it

**Full instructions, including how to remove it again, are in
[INSTALL.md](INSTALL.md).** The short version:

**You need to own Arx Fatalis.** This mod contains no game content at all -
no art, no sound, no levels. Buy it on
[GOG](https://www.gog.com/game/arx_fatalis) or
[Steam](https://store.steampowered.com/app/1560/Arx_Fatalis/), then point this
at your installation the same way you would Arx Libertatis.

**To host:** open the co-op menu, press HOST GAME, and give your friend your IP
address. The mod will try to open the port on your router automatically.

**To join:** type their address and press JOIN GAME.

**If it will not connect:** some internet providers put you behind a second
router that you cannot open ports on. The simple answer is a free virtual LAN
like [Radmin VPN](https://www.radmin-vpn.com/) or Hamachi - both players install
it, join the same network, and use the address it gives you.

![Two of you](docs/screenshots/together.png)

## Voice chat

Turn on VOICE CHAT in the co-op menu and hold **V** to speak.

It is proximity based, not a phone call. Your voice comes out of your character,
so it gets quieter the further apart you are and arrives from the direction you
are standing in. Walk far enough away and your friend cannot hear you at all.

There is a **MIC TEST** on the same menu with a level meter, so you can check
your microphone before you rely on it. If nothing moves, click **MIC:** to try
a different microphone - modern machines are full of ones that look real and
are not.

## Credits

This is built on other people's work, and a lot of it. See
**[CREDITS.md](CREDITS.md)** for the full list - but the short version:

**[Arx Libertatis](https://arx-libertatis.org/)** is the reason this exists at
all. A team of volunteers spent over a decade rebuilding Arx Fatalis into
something that still runs and can still be changed. Every line of co-op here
sits on top of that. Thank you.

**[Arkane Studios](https://www.arkane-studios.com/)** made Arx Fatalis in 2002,
and it is still unlike anything else.

## Licence

GPL v3, the same as Arx Libertatis - see [LICENSE](LICENSE). That means the
source is here, and anything built from it must stay open too.

---

*This is an unofficial fan project. Not affiliated with, endorsed by, or
connected to Arkane Studios or Bethesda Softworks. Arx Fatalis is their
trademark.*
