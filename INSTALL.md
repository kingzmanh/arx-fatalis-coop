# Installing

**You need to own Arx Fatalis.** This mod contains no game content - no art, no
sound, no levels, no speech. It is only the engine, and it needs your copy of
the game to have anything to show you.

- [Arx Fatalis on GOG](https://www.gog.com/game/arx_fatalis)
- [Arx Fatalis on Steam](https://store.steampowered.com/app/1560/Arx_Fatalis/)

Both players need their own copy, and both need the same build of this mod -
different versions refuse to connect rather than misbehave halfway through.

---

## The important part first

**This does not touch your Arx Fatalis installation.** It is a separate program
that reads the game's files where they already sit. Nothing is copied over,
nothing is patched, nothing is replaced. If you decide you hate it, deleting one
folder undoes everything - your original game will not know it happened.

---

## Installing

### If you have a release build

1. Unzip it wherever you like. Your Documents folder is fine; it does not need
   to go anywhere near the game.
2. Run `arx.exe`.
3. It looks for Arx Fatalis by itself and usually finds it. If it does not, it
   says so and lists where it looked.

If it cannot find the game, tell it directly:

```
arx.exe -d "C:\GOG Games\Arx Fatalis"
```

Point it at the folder containing the game's `data` directory - the one holding
`data.pak`, `loc.pak` and the rest.

### If you are building it yourself

You will need [MSYS2](https://www.msys2.org/). From the **MSYS2 MinGW 64-bit**
terminal:

```bash
pacman -S --needed git mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 mingw-w64-x86_64-freetype \
  mingw-w64-x86_64-openal mingw-w64-x86_64-glm mingw-w64-x86_64-boost \
  mingw-w64-x86_64-libepoxy mingw-w64-x86_64-enet mingw-w64-x86_64-opus \
  mingw-w64-x86_64-zlib mingw-w64-x86_64-libpng
```

Then:

```bash
git clone https://github.com/kingzmanh/arx-fatalis-coop.git
cd arx-fatalis-coop
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`build/arx.exe` is the result. Run it from the MSYS2 MinGW 64-bit terminal, or
copy these DLLs next to it from `C:\msys64\mingw64\bin` so it runs from
anywhere:

```
SDL2.dll  libenet-7.dll  libopenal-1.dll  libopus-0.dll  libfreetype-6.dll
zlib1.dll  libpng16-16.dll  libbz2-1.dll  libbrotlicommon.dll  libbrotlidec.dll
libgcc_s_seh-1.dll  libstdc++-6.dll  libwinpthread-1.dll  libglib-2.0-0.dll
libgraphite2.dll  libharfbuzz-0.dll  libiconv-2.dll  libintl-8.dll
libpcre2-8-0.dll
```

---

## Playing together

**One of you hosts.** Open the co-op menu, press HOST GAME, and tell your friend
your IP address. The mod asks your router to open the port by itself, so most
people need to set up nothing at all.

**The other joins.** Type that address, press JOIN GAME.

**If it will not connect,** the usual reason is that your internet provider has
put you behind a second router you cannot open ports on. It is common, and it is
not something either of you did wrong. The simple answer is a free virtual LAN:

1. Both install [Radmin VPN](https://www.radmin-vpn.com/) (or Hamachi).
2. One creates a network, the other joins it.
3. Use the address Radmin shows - it looks like `26.x.x.x` - instead of your
   real one.

That works regardless of what your providers are doing.

**Voice chat** is on the same menu. Tick VOICE CHAT, hold the talk key - **V**
unless you change it - and speak. It carries from your character, so it fades
with distance. Use MIC TEST first: if the meter does not move when you talk,
click **MIC:** to try a different microphone, because most machines have several
and only one of them is real.

---

## Uninstalling

**Delete the folder you unzipped.** That is the whole thing. Your Arx Fatalis
installation was never modified, so there is nothing to put back.

Two leftovers, if you want everything gone:

**Saves and settings**

```
C:\Users\<your name>\Saved Games\Arx Libertatis\
```

This holds your co-op saves, your key bindings and the log file. Note that
ordinary Arx Libertatis uses the same folder - if you also play that, deleting
this removes those saves too. If you only ever played this mod, it is safe.

**Nothing else.** No registry entries, no files in Program Files, nothing left
in the game's own folders, no background service. It never installed itself
anywhere in the first place.

**The port on your router** closes by itself when you stop hosting. If the game
crashed mid-session it may stay open until the router expires it, usually within
a few hours. Nothing needs doing about it.

---

## When something goes wrong

It will - see the warning at the top of the [README](README.md).

The log is the useful thing:

```
C:\Users\<your name>\Saved Games\Arx Libertatis\arx.log
```

If you report a problem, that file plus what you were doing at the time and
which player you were is far more use than a description alone. Most of what is
written up in [FIXES.md](FIXES.md) was solved from exactly those three things.
