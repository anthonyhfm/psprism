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

The macOS frontend accepts standard game controllers. Its keyboard fallback is
arrows for the D-pad, WASD for the analog stick, I/J/K/L for
Triangle/Square/Cross/Circle, Q/E for L/R, Return for Start and Right Shift for
Select.
