# No automated builds here

Arx Libertatis' own CI lived in this folder and built the engine on Linux and
macOS. This co-op mod is Windows only, so those builds tested platforms it does
not target and failed on dependencies that only matter to them - which is a red
cross on every commit that means nothing.

They have been removed rather than repaired. If this ever grows a Linux build
worth testing, take them back from upstream:

    git checkout upstream/master -- .github/workflows

Building on Windows is described in INSTALL.md.
