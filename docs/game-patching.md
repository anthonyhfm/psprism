# Game-function patching

Generated full-recomp projects contain a `patches/` directory. Every `.cpp`
file in it is built for both the PSP and native host targets. Include the
canonical framework header:

```cpp
#include <psprecomp/patch.hpp>
```

## Patching Ghidra functions

Copy the recovered return type and parameters into a normal C++ function. A
default Ghidra name embeds an image-relative address and needs no separate
address declaration:

```cpp
void FUN_00053ba8(int param_1, int param_2) {
    // replacement game logic
}

RECOMP_PATCH_GHIDRA(FUN_00053ba8);
```

A descriptive name is resolved from the project's code map:

```cpp
void DaxRenderWorld_Initialize() {
    // replacement initialization
}

RECOMP_PATCH_GHIDRA(DaxRenderWorld_Initialize);
```

The code map must contain the same symbol, for example:

```text
function 0x53ba8 DaxRenderWorld_Initialize
```

An explicit registration is useful when the replacement has a different C++
name:

```cpp
int patched_damage(int kind, int amount) {
    return kind == 1 ? amount * 3 : amount;
}

RECOMP_PATCH_FUNCTION(psprecomp::patch::image_offset(0x4200), patched_damage);
```

Use `image_offset()` for relocatable PRXs. Use `absolute_address()` only when
Ghidra and the PSP loader both use a fixed virtual address. A plain integer
uses the common Ghidra convention automatically: values below `0x04000000`
are image offsets and PSP-range values are absolute addresses.

## PSP ABI types

Typed hooks marshal the PSP EABI rather than the host ABI. They support:

- integral and enum values up to 64 bits;
- `float` and `double`;
- guest data pointers, translated to the current host mapping;
- register and spilled stack arguments;
- integer, float, double, 64-bit, pointer and `void` returns.

The PSP EABI uses `$a0`-`$a3` plus `$t0`-`$t3` for the first eight integer
words, `$f12`-`$f19` for the first eight floats, aligned GPR pairs for 64-bit
arguments, and stack slots thereafter. Structs by value and variadic
prototypes are intentionally rejected because their recovered ABI is often
ambiguous. Pass structures by pointer. Represent guest code pointers and
callbacks as `std::uint32_t` addresses instead of native function pointers.
An unmapped non-null pointer stops an incoming hook with a guest memory fault;
an outgoing call reports `CallError::unmapped_pointer`. Use `std::uint32_t` for
opaque guest addresses that are not meant to be dereferenced.

## Calling game functions and the original body

`DEFINE_GAME_FUNCTION` creates a strongly typed callable from a recovered
address and prototype. Its argument list contains types only:

```cpp
DEFINE_GAME_FUNCTION(original_FUN_00053ba8,
                     psprecomp::patch::image_offset(0x53ba8),
                     void, int, int);

void FUN_00053ba8(int param_1, int param_2) {
    // Run the translated original without recursively entering this patch.
    original_FUN_00053ba8.original(param_1, param_2);
}

RECOMP_PATCH_GHIDRA(FUN_00053ba8);
```

Calling `original_FUN_00053ba8(...)` normally honors registered patches.
Calling `.original(...)` bypasses the patch at that address. From inside a
replacement, the shorter equivalent is:

```cpp
psprecomp::patch::call_original<void>(param_1, param_2);
```

Original calls run the dispatcher, imports and interpreter fallback until the
guest function returns. `psprecomp::patch::last_call_error()` reports missing
state, invalid addresses, unmapped pointers, stack faults and execution
failures.

## Globals

Use `GameGlobal<T>` for reverse-engineered global data:

```cpp
DEFINE_GAME_GLOBAL(g_player_health,
                   psprecomp::patch::image_offset(0x195010),
                   std::uint32_t);

void give_health() {
    g_player_health = 999;
    const auto current = g_player_health.get();
    std::uint32_t* direct = g_player_health.pointer();
    (void)current;
    (void)direct;
}
```

For initial values, register an initializer. It runs after the guest image is
loaded and relocated and after imports are patched:

```cpp
void apply_patch_data() {
    g_player_health = 999;
}

RECOMP_PATCH_INITIALIZER(apply_patch_data);
```

Low-level helpers remain available as `read_guest<T>`, `write_guest<T>`,
`guest_to_host` and `host_to_guest`. They use the active guest thread's state.
The active state, current patch and original-call bypass are thread-local.
For a `GameGlobal<T*>`, `get()` and `set()` translate the stored 32-bit guest
pointer. Its `pointer()` member is intentionally unavailable because a native
pointer-to-pointer does not have the layout of a PSP pointer slot.

## Raw hooks

When a prototype is not yet known, a raw hook can inspect the CPU state:

```cpp
bool raw_hook(psprecomp::State& state) {
    state.gpr[2] = state.gpr[4] * 2; // return value in $v0
    state.pc = state.gpr[31];
    state.branch_pending = false;
    return true;
}

RECOMP_PATCH_RAW(psprecomp::patch::image_offset(0x7000), raw_hook);
```

Prefer typed hooks once the Ghidra prototype is understood. Duplicate exact
address or name registrations fail project startup instead of silently
depending on C++ static-initialization order.

Patches target function entries, not arbitrary instructions inside a function.
The complete original-call and startup-initializer facilities apply to the
normal full-recompile mode. Hybrid PSP overlay projects support typed entry
replacements, but do not currently support calling a detoured original body or
running patch initializers.
