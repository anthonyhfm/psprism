# Licensing and proprietary-content boundary

Copyright (C) 2026 Anthony Hofmeister

Except for the separately identified third-party material, psprism, Refract,
the project generator, build files, documentation, and original project
templates are licensed under the GNU General Public License, version 3 or (at
your option) any later version (`GPL-3.0-or-later`). The complete license text
is in [LICENSE](LICENSE). Contributors retain copyright in their contributions,
which are accepted under the same project license unless explicitly stated
otherwise.

Third-party files remain under their own licenses. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the notices shipped beside
those files.

## Exported PSPRecomp projects

The GPL covers the psprism-authored project skeleton, build integration,
runtime, and user-authored patches when those patches do not carry another
compatible notice. An exported repository should retain `LICENSE`,
`LICENSING.md`, and `THIRD_PARTY_NOTICES.md`.

The GPL does **not** grant any rights in a user-supplied PSP game. In
particular, the following are not licensed by psprism and must not be committed
or distributed unless the relevant rightsholders separately permit it:

- disc images and executable dumps;
- decrypted executables and embedded guest-memory images;
- statically translated or decompiled game code;
- extracted graphics, audio, video, text, levels, and other game assets; and
- game names, logos, characters, screenshots, and other trademarks or
  copyrighted promotional material.

Generated translation output may contain or closely represent material from
the supplied executable. It is private build output, not GPL-licensed source
code merely because psprism produced it. Public game-port repositories should
contain only the clean project skeleton, original patches, metadata needed to
identify a supported revision, and instructions for users to hydrate locally
from their own lawfully obtained copy.

Compiled applications and rebuilt disc images can contain proprietary game
material. Do not publish them without permission from the applicable
rightsholders. The repository's ignore rules are intended to keep hydration
and build output untracked, but every publisher remains responsible for
reviewing the files and Git history they release.

## No affiliation

psprism and Refract are unofficial compatibility and preservation projects.
They are not affiliated with, authorized by, sponsored by, or endorsed by
Sony Interactive Entertainment, Ready at Dawn, or any game publisher or
rightsholder. PlayStation, PSP, and game titles remain the property of their
respective owners.

This document describes the intended scope of the project's licenses; it is
not legal advice.
