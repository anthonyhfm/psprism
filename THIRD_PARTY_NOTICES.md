# Third-Party Notices

psprism includes or interfaces with the components listed below. Their
licenses apply to those components independently of psprism's
`GPL-3.0-or-later` license. Copyright notices in the individual source files
remain authoritative.

## ATRAC3/ATRAC3+ decoder

- Location: `refract/third_party/at3_standalone/`
- Origin: [FFmpeg](https://ffmpeg.org/) decoder sources, adapted as a
  standalone library by [PPSSPP](https://github.com/hrydgard/ppsspp)
- License: GNU Lesser General Public License 2.1 or later
- License text: `refract/third_party/at3_standalone/COPYING.LGPLv2.1`

The decoder files retain notices for Fabrice Bellard, Michael Niedermayer,
Loren Merritt, Maxim Poliakovski, Benjamin Larsson, and other contributors as
applicable to each file.

## PSPSDK headers

- Location: `refract/include/pspsdk/`
- Origin: the [PSPSDK project](https://github.com/pspdev/pspsdk) and the
  upstream projects identified in individual headers
- Predominant license: BSD 3-Clause
- License text: `refract/include/pspsdk/LICENSE.BSD-3-Clause`

The following bundled headers carry a different explicit license notice:

- `cfwmacros.h`, `rebootexconfig.h`, `systemctrl.h`, and `systemctrl_ark.h`:
  PRO CFW material under GNU GPL version 3 or later.
- `isoctrl.h`: Unified UMDemu API material under GNU GPL version 3 or later.
- `vitapops.h`: Adrenaline material under GNU GPL version 3 or later.
- `pspirkeyb.h` and `pspirkeyb_rawkeys.h`:
  [libpspirkeyb](https://github.com/pspdev/pspirkeyb) material under GNU LGPL
  version 2.1; a copy is at
  `refract/include/pspsdk/COPYING.LGPLv2.1`.

The top-level `LICENSE` contains the GNU GPL version 3 text. Preserve all
in-file copyright and license notices when redistributing these headers.

## Dependencies not vendored by psprism

A separately installed PPSSPP build is loaded at runtime for supported PSP
decryption. PPSSPP is licensed under GNU GPL version 2 or later and is not
copied into this repository. PSPSDK is a separately installed toolchain for
PSP targets. Native builds can link to separately installed FFmpeg and Qt
libraries. These projects are not relicensed by psprism; their own licenses
and the configuration chosen by a distributor govern redistribution of them
and of binaries linked against them.
