# Credits

Almost none of this is mine. The co-op layer is a small thing sitting on top of
a great deal of other people's work, and this is who did it.

---

## Arx Libertatis

**The reason this project can exist.**

Arx Fatalis' source code was released in 2011, and a team of volunteers has
spent over a decade since turning it into something that still builds, still
runs on modern machines, and can still be understood well enough to change.
Every single thing in this mod is built on that.

- Website: https://arx-libertatis.org/
- Source: https://github.com/arx/ArxLibertatis
- Licence: GNU General Public License v3

The full contributor list is in [AUTHORS](AUTHORS) and it is a long one. It has
been kept exactly as it was, because it should be.

**This project is a modified version of Arx Libertatis.** The changes are the
co-op layer (`src/net/`) and the places in the engine it had to reach into.

## Arx Fatalis

**Arkane Studios**, 2002. The game itself - its world, art, sound, writing and
design - belongs to them, and none of it is included here. You need your own
copy to play.

- https://www.arkane-studios.com/

This is an unofficial fan project with no connection to Arkane Studios or
Bethesda Softworks.

---

## Libraries

### ENet
Reliable UDP networking - the layer everything between the two players travels
over.
Copyright © 2002-2020 Lee Salzman. MIT licence.
http://enet.bespin.org/

### libplum
Asks the host's router to open the port by itself, through UPnP, NAT-PMP or PCP.
Copyright © Paul-Louis Ageneau. Mozilla Public License 2.0.
https://github.com/paullouisageneau/libplum

**Modified.** Two changes were made, both because the original assumes a machine
has one route to the internet, which is not true of any machine with a VPN
installed. The changes are kept as a patch against upstream 0.6.0 at
[thirdparty/libplum-multihomed.patch](thirdparty/libplum-multihomed.patch), as
MPL 2.0 requires:

1. `net_get_default_gateway` now asks Windows which route it would really use,
   instead of taking the first default route in the table.
2. The UPnP discovery socket is pinned to the interface that actually reaches
   the internet.

### Opus
The codec proximity voice chat speaks through.
Copyright © Xiph.Org Foundation, Skype Limited, Octasic, Jean-Marc Valin,
Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding, Erik de
Castro Lopo. BSD 3-clause licence.
https://opus-codec.org/

### SDL 2
Window, input and microphone capture.
Copyright © 1997-2024 Sam Lantinga. zlib licence.
https://www.libsdl.org/

### OpenAL Soft
Positional audio - what makes a voice fade with distance and arrive from the
right direction.
LGPL v2 or later.
https://openal-soft.org/

### FreeType
Text rendering.
Copyright © 1996-2024 David Turner, Robert Wilhelm, and Werner Lemberg.
FreeType Licence / GPL v2.
https://freetype.org/

### zlib
Compression.
Copyright © 1995-2024 Jean-loup Gailly and Mark Adler. zlib licence.
https://zlib.net/

### Bundled in this repository

- **DejaVu fonts** - see [LICENSE.DejaVu](LICENSE.DejaVu)
- **stb** by Sean Barrett - see [LICENSE.stb](LICENSE.stb)
- **fast_float** - see [LICENSE.fast_float](LICENSE.fast_float)
- **Blast** (PKWare decompression) by Mark Adler

---

## And honestly

This mod was built by someone who does not know how to code, working with AI
over a long stretch of time. That is worth saying plainly, because the work
above was done by people who very much do, and it would be wrong to let this
sit alongside theirs without the difference being clear.

If you know what you are doing and something in here makes you wince, you are
probably right. Pull requests welcome.
