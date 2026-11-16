# Upstream provenance

The starting point is the Abuse 0.8 distribution supplied in the parent directory.
Its README identifies version 0.8; source headers credit Crack dot Com, Anthony
Kruize, Sam Hocevar and other original contributors. No upstream Git history was
available in the supplied distribution, so no upstream commit ID is claimed.

`upstream-sha256.txt` records SHA-256 checksums of the original source/header files
copied into this project, before modernization edits. Source lists come from the
original Automake manifests; unused implementation files, generated Makefiles,
Autotools machinery and game assets were not imported. Some shared original
headers are retained even when not yet used by a compiled target.

License information from the supplied distribution is preserved in `COPYING`,
`COPYING.GPL`, `COPYING.WTFPL` and `AUTHORS`. The port derives from the GPL-2.0-or-later
SDL platform implementation; the combined engine must be distributed consistently
with those terms. Other files retain their original public-domain or WTFPL notices.
No original game assets are included in this repository. The supplied `COPYING`
contains the original data attribution and redistribution notes.

Dependencies are fetched at immutable revisions:

- SDL 3.4.0: `a962f40bbba175e9716557a25d5d7965f134a3d3`.
- SDL_mixer 3.2.0: `cedfeef30e93db35eee6b25759117da63f8e5a4f`.

These are upstream dependencies with their own license notices. They live in the
build directory and are not vendored or committed into this engine repository.
