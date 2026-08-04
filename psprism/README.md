# psprism

`psprism` is the editable host runtime shipped with every PSPRecomp project.
It translates PSP user-mode imports into host operating-system services. The
first backend targets macOS; Windows and Linux backends will follow behind the
same public interface.

The copy in a generated game belongs to that game. Add compatibility quirks
there when a title needs behavior that should not yet become a global default.

Currently implemented building blocks include logging, process exit, clocks,
delays, controller-neutral input, UMD status and synchronous/async-style file
I/O against the exported `disc/` and `.psprism/ms0/` trees. Unknown imports are
reported once and return PSP's `SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED` value.

Savedata, message dialogs and the on-screen keyboard use asynchronous Qt 6
widget trees rendered into the game window. This keeps fullscreen gameplay
uninterrupted while retaining a desktop-quality dialog UI. Savedata is
presented as a horizontal carousel with visible neighboring slots, PSP metadata
and `PIC1.PNG`/`ICON0.PNG` artwork. Secure savedata modes currently preserve
payloads without reproducing the PSP encryption layer.

Qt is an optional system-dialog renderer for desktop targets only. It does not
own the game window, graphics backend, filesystem, controller implementation or
portable runtime API. Non-desktop targets such as PSP or future Wii/homebrew
ports select their own host dialog backend and build with
`PSPRISM_DESKTOP_DIALOGS=OFF`.

The macOS game window accepts standard game controllers. Its keyboard fallback
is arrows for the D-pad, WASD for the analog stick, I/J/K/L for
Triangle/Square/Cross/Circle, Q/E for L/R, Return for Start and Right Shift for
Select.
