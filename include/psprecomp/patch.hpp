#pragma once

#include <psprecomp/runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <tuple>
#include <type_traits>

namespace psprecomp::patch {

inline constexpr std::uint32_t CALL_RETURN_SENTINEL = 0xfffffff8U;
inline constexpr std::uint64_t DEFAULT_CALL_BLOCK_LIMIT = 10'000'000ULL;

enum class AddressKind : std::uint8_t { automatic, image_offset, absolute };

struct GuestAddress {
    std::uint32_t value{};
    AddressKind kind{AddressKind::automatic};
};

constexpr GuestAddress guest_address(std::uint32_t value) {
    return {value, AddressKind::automatic};
}
constexpr GuestAddress image_offset(std::uint32_t value) {
    return {value, AddressKind::image_offset};
}
constexpr GuestAddress absolute_address(std::uint32_t value) {
    return {value, AddressKind::absolute};
}

template <typename T, typename = void>
struct PointeeWidth : std::integral_constant<std::size_t, 1U> {};

template <typename T>
struct PointeeWidth<T, std::void_t<decltype(sizeof(T))>>
    : std::integral_constant<std::size_t, sizeof(T)> {};

template <typename Pointer>
inline constexpr std::size_t pointee_width =
    PointeeWidth<std::remove_cv_t<std::remove_pointer_t<Pointer>>>::value;

inline bool add_address(std::uint32_t left, std::uint32_t right,
                        std::uint32_t& result) {
    if (right > std::numeric_limits<std::uint32_t>::max() - left) return false;
    result = left + right;
    return true;
}

inline bool resolve_address(const State& state, GuestAddress address,
                            std::uint32_t& resolved) {
    if (address.kind == AddressKind::absolute) {
        resolved = address.value;
        return true;
    }
    if (address.kind == AddressKind::image_offset)
        return add_address(state.memory_base, address.value, resolved);
    // Relocatable Ghidra programs normally use small image offsets, while
    // fixed PSP executables use virtual addresses at 0x04xxxxxx or above.
    if (address.value >= 0x04000000U) {
        resolved = address.value;
        return true;
    }
    return add_address(state.memory_base, address.value, resolved);
}

using RawHookFn = bool (*)(State&);
using InitializerFn = void (*)(State&);

struct PatchNode {
    GuestAddress target{};
    const char* target_name{nullptr};
    const char* hook_name{nullptr};
    RawHookFn hook{nullptr};
    PatchNode* next{nullptr};
};

struct InitializerNode {
    const char* name{nullptr};
    InitializerFn initializer{nullptr};
    InitializerNode* next{nullptr};
};

inline PatchNode* g_patch_list = nullptr;
inline InitializerNode* g_initializer_list = nullptr;
inline thread_local State* g_active_state = nullptr;
inline thread_local const PatchNode* g_active_patch = nullptr;
inline thread_local const PatchNode* g_bypassed_patch = nullptr;
inline thread_local std::uint32_t g_active_patch_address{};

inline State*& active_state() { return g_active_state; }

class StateGuard {
public:
    explicit StateGuard(State& state) : previous_(g_active_state) {
        g_active_state = &state;
    }
    StateGuard(const StateGuard&) = delete;
    StateGuard& operator=(const StateGuard&) = delete;
    ~StateGuard() { g_active_state = previous_; }

private:
    State* previous_{};
};

inline bool register_raw_patch(PatchNode& node, GuestAddress target,
                               const char* hook_name, RawHookFn hook) {
    node.target = target;
    node.target_name = nullptr;
    node.hook_name = hook_name;
    node.hook = hook;
    node.next = g_patch_list;
    g_patch_list = &node;
    return true;
}

inline bool register_raw_patch(PatchNode& node, std::uint32_t target,
                               const char* hook_name, RawHookFn hook) {
    return register_raw_patch(node, guest_address(target), hook_name, hook);
}

inline bool register_named_patch(PatchNode& node, const char* target_name,
                                 const char* hook_name, RawHookFn hook) {
    node.target = {};
    node.target_name = target_name;
    node.hook_name = hook_name;
    node.hook = hook;
    node.next = g_patch_list;
    g_patch_list = &node;
    return true;
}

inline bool register_initializer(InitializerNode& node, const char* name,
                                 InitializerFn initializer) {
    node.name = name;
    node.initializer = initializer;
    node.next = g_initializer_list;
    g_initializer_list = &node;
    return true;
}

inline bool parse_ghidra_function_name(const char* name,
                                       std::uint32_t& address) {
    if (name == nullptr || std::strncmp(name, "FUN_", 4) != 0) return false;
    const char* cursor = name + 4;
    if (*cursor == '\0') return false;
    std::uint32_t value = 0;
    std::size_t digits = 0;
    while (*cursor != '\0' && digits < 8U) {
        const char ch = *cursor++;
        std::uint32_t digit = 0;
        if (ch >= '0' && ch <= '9') digit = static_cast<std::uint32_t>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') digit = static_cast<std::uint32_t>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') digit = static_cast<std::uint32_t>(ch - 'A' + 10);
        else return false;
        value = (value << 4U) | digit;
        ++digits;
    }
    if (*cursor != '\0') return false;
    address = value;
    return true;
}

inline bool address_matches(const PatchNode& patch, const State& state,
                            std::uint32_t current_pc) {
    std::uint32_t resolved = 0;
    return patch.target_name == nullptr &&
           resolve_address(state, patch.target, resolved) &&
           canonical_address(resolved) == canonical_address(current_pc);
}

inline const PatchNode* find_patch(const State& state, std::uint32_t current_pc,
                                   const char* function_name = nullptr) {
    // Explicit address registrations have deterministic priority over names.
    for (const PatchNode* patch = g_patch_list; patch; patch = patch->next) {
        if (patch != g_bypassed_patch && address_matches(*patch, state, current_pc))
            return patch;
    }
    for (const PatchNode* patch = g_patch_list; patch; patch = patch->next) {
        if (patch == g_bypassed_patch || patch->target_name == nullptr) continue;
        if (function_name && std::strcmp(patch->target_name, function_name) == 0)
            return patch;
        std::uint32_t ghidra_address = 0;
        std::uint32_t resolved = 0;
        if (parse_ghidra_function_name(patch->target_name, ghidra_address) &&
            resolve_address(state, guest_address(ghidra_address), resolved) &&
            canonical_address(resolved) == canonical_address(current_pc))
            return patch;
    }
    return nullptr;
}

// Compatibility lookup for code without a State. Runtime dispatch should use
// the State-aware overload so image-relative patches relocate correctly.
inline const PatchNode* find_patch(std::uint32_t address) {
    for (const PatchNode* patch = g_patch_list; patch; patch = patch->next) {
        if (patch == g_bypassed_patch) continue;
        if (!patch->target_name && patch->target.value == address) return patch;
        std::uint32_t ghidra_address = 0;
        if (parse_ghidra_function_name(patch->target_name, ghidra_address) &&
            ghidra_address == address) return patch;
    }
    return nullptr;
}

inline const PatchNode* find_patch_by_name(const char* name) {
    if (!name) return nullptr;
    for (const PatchNode* patch = g_patch_list; patch; patch = patch->next) {
        if ((patch->target_name && std::strcmp(patch->target_name, name) == 0) ||
            (patch->hook_name && std::strcmp(patch->hook_name, name) == 0))
            return patch;
    }
    return nullptr;
}

inline bool validate_registry() {
    for (const PatchNode* left = g_patch_list; left; left = left->next) {
        if (!left->hook || (left->target_name && left->target_name[0] == '\0'))
            return false;
        for (const PatchNode* right = left->next; right; right = right->next) {
            if (left->target_name && right->target_name &&
                std::strcmp(left->target_name, right->target_name) == 0)
                return false;
            if (!left->target_name && !right->target_name &&
                left->target.kind == right->target.kind &&
                left->target.value == right->target.value)
                return false;
        }
    }
    return true;
}

inline bool resolved_patch_target(const PatchNode& patch, const State& state,
                                  std::uint32_t& resolved) {
    if (patch.target_name == nullptr)
        return resolve_address(state, patch.target, resolved);
    std::uint32_t ghidra_address = 0;
    return parse_ghidra_function_name(patch.target_name, ghidra_address) &&
           resolve_address(state, guest_address(ghidra_address), resolved);
}

inline bool validate_registry(const State& state) {
    if (!validate_registry()) return false;
    for (const PatchNode* left = g_patch_list; left; left = left->next) {
        std::uint32_t left_target = 0;
        if (!resolved_patch_target(*left, state, left_target)) continue;
        for (const PatchNode* right = left->next; right; right = right->next) {
            std::uint32_t right_target = 0;
            if (resolved_patch_target(*right, state, right_target) &&
                canonical_address(left_target) ==
                    canonical_address(right_target))
                return false;
        }
    }
    return true;
}

class ActivePatchGuard {
public:
    ActivePatchGuard(State& state, const PatchNode& patch,
                     std::uint32_t actual_address)
        : state_guard_(state), previous_patch_(g_active_patch),
          previous_address_(g_active_patch_address) {
        g_active_patch = &patch;
        g_active_patch_address = actual_address;
    }
    ActivePatchGuard(const ActivePatchGuard&) = delete;
    ActivePatchGuard& operator=(const ActivePatchGuard&) = delete;
    ~ActivePatchGuard() {
        g_active_patch = previous_patch_;
        g_active_patch_address = previous_address_;
    }

private:
    StateGuard state_guard_;
    const PatchNode* previous_patch_{};
    std::uint32_t previous_address_{};
};

class BypassGuard {
public:
    explicit BypassGuard(const PatchNode* patch) : previous_(g_bypassed_patch) {
        g_bypassed_patch = patch;
    }
    BypassGuard(const BypassGuard&) = delete;
    BypassGuard& operator=(const BypassGuard&) = delete;
    ~BypassGuard() { g_bypassed_patch = previous_; }

private:
    const PatchNode* previous_{};
};

inline bool invoke_patch(const PatchNode& patch, State& state,
                         std::uint32_t actual_address) {
    ActivePatchGuard guard(state, patch, actual_address);
    return patch.hook && patch.hook(state);
}

inline bool run_initializers(State& state) {
    if (!validate_registry(state)) return false;
    StateGuard guard(state);
    for (InitializerNode* node = g_initializer_list; node; node = node->next) {
        if (!node->initializer) return false;
        node->initializer(state);
        if (state.stop_reason != StopReason::running) return false;
    }
    return true;
}

// Guest memory helpers. Invalid native mappings return null/zero instead of
// manufacturing truncated 64-bit host pointers.
inline void* guest_to_host(State& state, std::uint32_t address,
                           std::size_t size = 1) {
    if (void* pointer = mapped_address(state, address, size)) return pointer;
    if (state.direct_memory_access && direct_address_ok(state, address, size))
        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
    return nullptr;
}

inline const void* guest_to_host(const State& state, std::uint32_t address,
                                 std::size_t size = 1) {
    return mapped_address(state, address, size);
}

inline void* guest_to_host(std::uint32_t address, std::size_t size = 1) {
    if (g_active_state) return guest_to_host(*g_active_state, address, size);
#if defined(__PSP__)
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(address));
#else
    static_cast<void>(address);
    static_cast<void>(size);
    return nullptr;
#endif
}

inline bool host_region_to_guest(const void* pointer, const std::uint8_t* host,
                                 std::size_t region_size,
                                 std::uint32_t guest_base,
                                 std::uint32_t& result,
                                 std::size_t width = 1U) {
    if (!pointer || !host) return false;
    const auto value = reinterpret_cast<std::uintptr_t>(pointer);
    const auto begin = reinterpret_cast<std::uintptr_t>(host);
    if (value < begin) return false;
    const auto offset = value - begin;
    if (offset > region_size || width > region_size - offset) return false;
    return add_address(guest_base, static_cast<std::uint32_t>(value - begin), result);
}

inline std::uint32_t host_to_guest(const State& state, const void* pointer,
                                   std::size_t width = 1U) {
    if (!pointer) return 0;
    std::uint32_t result = 0;
    if (host_region_to_guest(pointer, state.memory, state.memory_size,
                             state.memory_base, result, width) ||
        host_region_to_guest(pointer, state.scratchpad, state.scratchpad_size,
                             0x00010000U, result, width) ||
        host_region_to_guest(pointer, state.video_memory, state.video_memory_size,
                             0x04000000U, result, width) ||
        host_region_to_guest(pointer, state.volatile_memory,
                             state.volatile_memory_size, 0x08400000U, result,
                             width))
        return result;
    const auto raw = reinterpret_cast<std::uintptr_t>(pointer);
    return state.direct_memory_access && width != 0U &&
                   raw <= std::numeric_limits<std::uint32_t>::max() &&
                   width - 1U <=
                       std::numeric_limits<std::uint32_t>::max() - raw
               ? static_cast<std::uint32_t>(raw)
               : 0U;
}

inline std::uint32_t host_to_guest(const void* pointer,
                                   std::size_t width = 1U) {
    if (g_active_state) return host_to_guest(*g_active_state, pointer, width);
#if defined(__PSP__)
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
#else
    static_cast<void>(pointer);
    return 0;
#endif
}

template <typename T>
inline T read_guest(State& state, std::uint32_t address) {
    using CleanT = std::remove_cv_t<T>;
    static_assert(std::is_trivially_copyable_v<CleanT>);
    if constexpr (std::is_pointer_v<CleanT>) {
        static_assert(!std::is_function_v<std::remove_pointer_t<CleanT>>,
                      "represent guest function pointers as uint32_t");
        const auto guest_pointer = load32(state, address);
        if (guest_pointer == 0) return nullptr;
        auto* pointer = guest_to_host(
            state, guest_pointer, pointee_width<CleanT>);
        if (pointer == nullptr) note_memory_fault(state, guest_pointer);
        return reinterpret_cast<T>(pointer);
    } else if constexpr (sizeof(CleanT) == 1) {
        const auto raw = load8(state, address);
        T value{};
        std::memcpy(&value, &raw, 1);
        return value;
    } else if constexpr (sizeof(CleanT) == 2) {
        const auto raw = load16(state, address);
        T value{};
        std::memcpy(&value, &raw, 2);
        return value;
    } else if constexpr (sizeof(CleanT) == 4) {
        const auto raw = load32(state, address);
        T value{};
        std::memcpy(&value, &raw, 4);
        return value;
    } else if constexpr (sizeof(CleanT) == 8) {
        const std::uint64_t raw = static_cast<std::uint64_t>(load32(state, address)) |
            (static_cast<std::uint64_t>(load32(state, address + 4U)) << 32U);
        T value{};
        std::memcpy(&value, &raw, 8);
        return value;
    } else {
        T value{};
        auto* bytes = reinterpret_cast<std::uint8_t*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = load8(state, address + static_cast<std::uint32_t>(i));
        return value;
    }
}

template <typename T>
inline T read_guest(std::uint32_t address) {
    if (g_active_state) return read_guest<T>(*g_active_state, address);
#if defined(__PSP__)
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(
                            static_cast<std::uintptr_t>(address)), sizeof(value));
    return value;
#else
    static_cast<void>(address);
    return T{};
#endif
}

template <typename T>
inline void write_guest(State& state, std::uint32_t address, T value) {
    using CleanT = std::remove_cv_t<T>;
    static_assert(std::is_trivially_copyable_v<CleanT>);
    if constexpr (std::is_pointer_v<CleanT>) {
        static_assert(!std::is_function_v<std::remove_pointer_t<CleanT>>,
                      "represent guest function pointers as uint32_t");
        const auto guest_pointer = host_to_guest(
            state, reinterpret_cast<const void*>(value),
            pointee_width<CleanT>);
        if (value != nullptr && guest_pointer == 0) {
            note_memory_fault(state, 0U);
            return;
        }
        store32(state, address, guest_pointer);
    } else if constexpr (sizeof(CleanT) == 1) {
        std::uint8_t raw{};
        std::memcpy(&raw, &value, 1);
        store8(state, address, raw);
    } else if constexpr (sizeof(CleanT) == 2) {
        std::uint16_t raw{};
        std::memcpy(&raw, &value, 2);
        store16(state, address, raw);
    } else if constexpr (sizeof(CleanT) == 4) {
        std::uint32_t raw{};
        std::memcpy(&raw, &value, 4);
        store32(state, address, raw);
    } else if constexpr (sizeof(CleanT) == 8) {
        std::uint64_t raw{};
        std::memcpy(&raw, &value, 8);
        store32(state, address, static_cast<std::uint32_t>(raw));
        store32(state, address + 4U, static_cast<std::uint32_t>(raw >> 32U));
    } else {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i)
            store8(state, address + static_cast<std::uint32_t>(i), bytes[i]);
    }
}

template <typename T>
inline void write_guest(std::uint32_t address, T value) {
    if (g_active_state) {
        write_guest<T>(*g_active_state, address, value);
        return;
    }
#if defined(__PSP__)
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),
                &value, sizeof(value));
#else
    static_cast<void>(address);
    static_cast<void>(value);
#endif
}

inline const char* guest_string(State& state, std::uint32_t address) {
    return reinterpret_cast<const char*>(guest_to_host(state, address, 1));
}
inline const char* guest_string(std::uint32_t address) {
    return reinterpret_cast<const char*>(guest_to_host(address, 1));
}

template <typename T>
class GameGlobal {
public:
    constexpr explicit GameGlobal(GuestAddress address) : address_(address) {}
    constexpr explicit GameGlobal(std::uint32_t address)
        : address_(guest_address(address)) {}

    T get(State& state) const {
        std::uint32_t resolved = 0;
        return resolve_address(state, address_, resolved)
                   ? read_guest<T>(state, resolved) : T{};
    }
    T get() const { return g_active_state ? get(*g_active_state) : T{}; }
    void set(State& state, T value) const {
        std::uint32_t resolved = 0;
        if (resolve_address(state, address_, resolved))
            write_guest<T>(state, resolved, value);
    }
    void set(T value) const { if (g_active_state) set(*g_active_state, value); }
    T* pointer(State& state) const {
        static_assert(!std::is_pointer_v<std::remove_cv_t<T>>,
                      "GameGlobal<T*>::pointer() cannot expose a 32-bit "
                      "guest pointer slot as a native pointer-to-pointer; "
                      "use get() or set() instead");
        std::uint32_t resolved = 0;
        return resolve_address(state, address_, resolved)
                   ? reinterpret_cast<T*>(guest_to_host(state, resolved, sizeof(T)))
                   : nullptr;
    }
    T* pointer() const { return g_active_state ? pointer(*g_active_state) : nullptr; }
    operator T() const { return get(); }
    GameGlobal& operator=(T value) { set(value); return *this; }
    constexpr GuestAddress address() const { return address_; }

private:
    GuestAddress address_{};
};

namespace detail {

inline constexpr std::size_t EABI_GPR_ARGUMENT_COUNT = 8;
inline constexpr std::size_t EABI_FPR_ARGUMENT_COUNT = 8;

struct AbiCursor {
    std::size_t gpr{}, fpr{}, stack{};
    bool valid{true};
};

template <typename T>
using clean_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
inline constexpr bool is_float_argument = std::is_same_v<clean_t<T>, float>;

template <typename T>
inline constexpr bool is_wide_argument =
    !std::is_pointer_v<clean_t<T>> && sizeof(clean_t<T>) == 8U;

template <typename T>
inline constexpr bool supported_argument = !std::is_reference_v<T> &&
    (std::is_pointer_v<clean_t<T>> || std::is_integral_v<clean_t<T>> ||
     std::is_enum_v<clean_t<T>> || is_float_argument<T> ||
     std::is_same_v<clean_t<T>, double>) && sizeof(clean_t<T>) <= 8U;

inline std::uint32_t load_stack_word(State& state, std::size_t word) {
    return load32(state, state.gpr[29] + static_cast<std::uint32_t>(word * 4U));
}
inline void store_stack_word(State& state, std::size_t word,
                             std::uint32_t value) {
    store32(state, state.gpr[29] + static_cast<std::uint32_t>(word * 4U), value);
}

template <typename T>
inline T from_word(std::uint32_t word) {
    T value{};
    std::memcpy(&value, &word, sizeof(T));
    return value;
}
template <typename T>
inline std::uint32_t to_word(T value) {
    std::uint32_t word{};
    std::memcpy(&word, &value, sizeof(T));
    return word;
}
template <typename T>
inline std::uint64_t to_wide_word(T value) {
    std::uint64_t word{};
    std::memcpy(&word, &value, sizeof(word));
    return word;
}
template <typename T>
inline T from_wide_word(std::uint64_t word) {
    T value{};
    std::memcpy(&value, &word, sizeof(word));
    return value;
}

template <typename T>
inline T extract_single_arg(State& state, AbiCursor& cursor) {
    using CleanT = clean_t<T>;
    static_assert(supported_argument<CleanT>,
                  "unsupported PSP patch argument; use scalars or data pointers");
    if constexpr (is_float_argument<CleanT>) {
        const auto raw = cursor.fpr < EABI_FPR_ARGUMENT_COUNT
            ? state.fpr[12U + cursor.fpr++]
            : load_stack_word(state, cursor.stack++);
        return from_word<T>(raw);
    } else if constexpr (std::is_pointer_v<CleanT>) {
        static_assert(!std::is_function_v<std::remove_pointer_t<CleanT>>,
                      "represent guest callbacks as uint32_t addresses");
        const auto address = cursor.gpr < EABI_GPR_ARGUMENT_COUNT
            ? state.gpr[4U + cursor.gpr++]
            : load_stack_word(state, cursor.stack++);
        if (address == 0) return nullptr;
        auto* pointer = guest_to_host(state, address, pointee_width<CleanT>);
        if (pointer == nullptr) {
            cursor.valid = false;
            note_memory_fault(state, address);
        }
        return reinterpret_cast<T>(pointer);
    } else if constexpr (is_wide_argument<CleanT>) {
        cursor.gpr = (cursor.gpr + 1U) & ~std::size_t{1};
        std::uint32_t low = 0, high = 0;
        if (cursor.gpr + 2U <= EABI_GPR_ARGUMENT_COUNT) {
            low = state.gpr[4U + cursor.gpr];
            high = state.gpr[5U + cursor.gpr];
        } else {
            cursor.stack = (cursor.stack + 1U) & ~std::size_t{1};
            low = load_stack_word(state, cursor.stack);
            high = load_stack_word(state, cursor.stack + 1U);
            cursor.stack += 2U;
        }
        cursor.gpr += 2U;
        return from_wide_word<T>(static_cast<std::uint64_t>(low) |
            (static_cast<std::uint64_t>(high) << 32U));
    } else {
        const auto word = cursor.gpr < EABI_GPR_ARGUMENT_COUNT
            ? state.gpr[4U + cursor.gpr++]
            : load_stack_word(state, cursor.stack++);
        return from_word<T>(word);
    }
}

template <typename... Args>
inline std::tuple<Args...> extract_arguments(State& state) {
    AbiCursor cursor;
    return std::tuple<Args...>{extract_single_arg<Args>(state, cursor)...};
}

template <typename T>
inline void inject_single_arg(State& state, T value, AbiCursor& cursor) {
    using CleanT = clean_t<T>;
    static_assert(supported_argument<CleanT>,
                  "unsupported PSP game-function argument");
    if constexpr (is_float_argument<CleanT>) {
        const auto raw = to_word(value);
        if (cursor.fpr < EABI_FPR_ARGUMENT_COUNT)
            state.fpr[12U + cursor.fpr++] = raw;
        else store_stack_word(state, cursor.stack++, raw);
    } else if constexpr (std::is_pointer_v<CleanT>) {
        static_assert(!std::is_function_v<std::remove_pointer_t<CleanT>>,
                      "represent guest callbacks as uint32_t addresses");
        const auto address = host_to_guest(
            state, reinterpret_cast<const void*>(value),
            pointee_width<CleanT>);
        if (value != nullptr && address == 0) cursor.valid = false;
        if (cursor.gpr < EABI_GPR_ARGUMENT_COUNT)
            state.gpr[4U + cursor.gpr++] = address;
        else store_stack_word(state, cursor.stack++, address);
    } else if constexpr (is_wide_argument<CleanT>) {
        cursor.gpr = (cursor.gpr + 1U) & ~std::size_t{1};
        const auto raw = to_wide_word(value);
        const auto low = static_cast<std::uint32_t>(raw);
        const auto high = static_cast<std::uint32_t>(raw >> 32U);
        if (cursor.gpr + 2U <= EABI_GPR_ARGUMENT_COUNT) {
            state.gpr[4U + cursor.gpr] = low;
            state.gpr[5U + cursor.gpr] = high;
        } else {
            cursor.stack = (cursor.stack + 1U) & ~std::size_t{1};
            store_stack_word(state, cursor.stack, low);
            store_stack_word(state, cursor.stack + 1U, high);
            cursor.stack += 2U;
        }
        cursor.gpr += 2U;
    } else {
        const auto word = to_word(value);
        if (cursor.gpr < EABI_GPR_ARGUMENT_COUNT)
            state.gpr[4U + cursor.gpr++] = word;
        else store_stack_word(state, cursor.stack++, word);
    }
}

template <typename T>
constexpr void count_single_arg(AbiCursor& cursor) {
    using CleanT = clean_t<T>;
    static_assert(supported_argument<CleanT>);
    if constexpr (is_float_argument<CleanT>) {
        if (cursor.fpr++ >= EABI_FPR_ARGUMENT_COUNT) ++cursor.stack;
    } else if constexpr (is_wide_argument<CleanT>) {
        cursor.gpr = (cursor.gpr + 1U) & ~std::size_t{1};
        if (cursor.gpr + 2U > EABI_GPR_ARGUMENT_COUNT) {
            cursor.stack = (cursor.stack + 1U) & ~std::size_t{1};
            cursor.stack += 2U;
        }
        cursor.gpr += 2U;
    } else {
        if (cursor.gpr++ >= EABI_GPR_ARGUMENT_COUNT) ++cursor.stack;
    }
}

template <typename... Args>
constexpr std::size_t stack_words_for_arguments() {
    AbiCursor cursor;
    (count_single_arg<Args>(cursor), ...);
    return cursor.stack;
}

template <typename... Args>
inline bool inject_arguments(State& state, Args... args) {
    AbiCursor cursor;
    (inject_single_arg(state, args, cursor), ...);
    return cursor.valid;
}

template <typename Ret>
inline Ret extract_return(State& state) {
    using CleanT = clean_t<Ret>;
    if constexpr (std::is_void_v<CleanT>) return;
    else if constexpr (is_float_argument<CleanT>) return from_word<Ret>(state.fpr[0]);
    else if constexpr (std::is_pointer_v<CleanT>) {
        static_assert(!std::is_function_v<std::remove_pointer_t<CleanT>>);
        if (state.gpr[2] == 0) return nullptr;
        auto* pointer = guest_to_host(
            state, state.gpr[2], pointee_width<CleanT>);
        if (pointer == nullptr) note_memory_fault(state, state.gpr[2]);
        return reinterpret_cast<Ret>(pointer);
    } else if constexpr (is_wide_argument<CleanT>) {
        return from_wide_word<Ret>(static_cast<std::uint64_t>(state.gpr[2]) |
            (static_cast<std::uint64_t>(state.gpr[3]) << 32U));
    } else {
        static_assert((std::is_integral_v<CleanT> || std::is_enum_v<CleanT>) &&
                      sizeof(CleanT) <= 4U);
        return from_word<Ret>(state.gpr[2]);
    }
}

template <typename Ret>
inline void inject_return(State& state, Ret value) {
    using CleanT = clean_t<Ret>;
    if constexpr (is_float_argument<CleanT>) state.fpr[0] = to_word(value);
    else if constexpr (std::is_pointer_v<CleanT>) {
        static_assert(!std::is_function_v<std::remove_pointer_t<CleanT>>);
        state.gpr[2] = host_to_guest(
            state, reinterpret_cast<const void*>(value),
            pointee_width<CleanT>);
        if (value != nullptr && state.gpr[2] == 0U)
            note_memory_fault(state, 0U);
    } else if constexpr (is_wide_argument<CleanT>) {
        const auto raw = to_wide_word(value);
        state.gpr[2] = static_cast<std::uint32_t>(raw);
        state.gpr[3] = static_cast<std::uint32_t>(raw >> 32U);
    } else {
        static_assert((std::is_integral_v<CleanT> || std::is_enum_v<CleanT>) &&
                      sizeof(CleanT) <= 4U);
        state.gpr[2] = to_word(value);
    }
}

template <typename Fn, Fn* function>
struct TypedHookInvoker;

template <typename Ret, typename... Args, Ret (*function)(Args...)>
struct TypedHookInvoker<Ret(Args...), function> {
    static bool invoke(State& state) {
        auto arguments = extract_arguments<Args...>(state);
        if (state.stop_reason != StopReason::running) return true;
        if constexpr (std::is_void_v<Ret>) std::apply(function, arguments);
        else inject_return(state, std::apply(function, arguments));
        if (state.stop_reason == StopReason::running) {
            state.pc = state.gpr[31];
            state.branch_pending = false;
        }
        // The address was handled even if the replacement deliberately
        // stopped the guest or encountered a fault. Returning false would
        // incorrectly execute the untranslated original after the hook.
        return true;
    }
};

template <auto function>
struct InitializerInvoker {
    static void invoke(State& state) {
        StateGuard guard(state);
        if constexpr (std::is_invocable_v<decltype(function), State&>) function(state);
        else {
            static_assert(std::is_invocable_v<decltype(function)>);
            function();
        }
    }
};

template <typename Ret>
inline Ret failed_call_result() {
    if constexpr (std::is_void_v<Ret>) return;
    else return Ret{};
}

} // namespace detail

namespace bridge {
bool execute_function_at(State&, std::uint32_t address, std::uint64_t max_blocks);
}

enum class CallError : std::uint8_t {
    none, no_active_state, invalid_address, unmapped_pointer, stack_fault,
    execution_failed,
};
inline thread_local CallError g_last_call_error = CallError::none;
inline CallError last_call_error() { return g_last_call_error; }

template <typename Ret = void, typename... Args>
inline Ret call_game_function_impl(GuestAddress guest_target,
                                   const PatchNode* bypassed_patch,
                                   Args... args) {
    State* current = g_active_state;
    if (!current) {
        g_last_call_error = CallError::no_active_state;
        return detail::failed_call_result<Ret>();
    }
    State& state = *current;
    std::uint32_t target = 0;
    if (!resolve_address(state, guest_target, target)) {
        g_last_call_error = CallError::invalid_address;
        return detail::failed_call_result<Ret>();
    }

    constexpr std::size_t stack_words = detail::stack_words_for_arguments<Args...>();
    constexpr std::uint32_t stack_bytes = static_cast<std::uint32_t>(
        (stack_words * 4U + 15U) & ~std::size_t{15U});
    const std::uint32_t saved_sp = state.gpr[29];
    const std::uint32_t saved_ra = state.gpr[31];
    const std::uint32_t saved_pc = state.pc;
    const bool saved_branch_pending = state.branch_pending;
    const std::uint32_t saved_branch_target = state.branch_target;
    const StopReason saved_stop_reason = state.stop_reason;

    if constexpr (stack_bytes != 0U) {
        if (saved_sp < stack_bytes) {
            g_last_call_error = CallError::stack_fault;
            return detail::failed_call_result<Ret>();
        }
        state.gpr[29] = saved_sp - stack_bytes;
    }
    state.stop_reason = StopReason::running;
    const bool arguments_valid = detail::inject_arguments(state, args...);
    if (!arguments_valid || state.stop_reason != StopReason::running) {
        const bool stack_fault = state.stop_reason != StopReason::running;
        state.gpr[29] = saved_sp;
        state.gpr[31] = saved_ra;
        state.pc = saved_pc;
        state.branch_pending = saved_branch_pending;
        state.branch_target = saved_branch_target;
        if (!stack_fault) state.stop_reason = saved_stop_reason;
        g_last_call_error = stack_fault ? CallError::stack_fault
                                        : CallError::unmapped_pointer;
        return detail::failed_call_result<Ret>();
    }

    state.gpr[31] = CALL_RETURN_SENTINEL;
    state.pc = target;
    state.branch_pending = false;
    BypassGuard bypass_guard(bypassed_patch);
    const bool executed = bridge::execute_function_at(
        state, target, DEFAULT_CALL_BLOCK_LIMIT);

    // Read the ABI result before restoring control-flow state. Caller-saved
    // argument/result registers intentionally retain the callee's values.
    using ResultStorage = std::conditional_t<std::is_void_v<Ret>,
                                             std::uint8_t, Ret>;
    ResultStorage result{};
    if constexpr (!std::is_void_v<Ret>) {
        if (executed)
            result = detail::extract_return<Ret>(state);
    }
    const bool result_valid = state.stop_reason == StopReason::running;
    state.gpr[29] = saved_sp;
    state.gpr[31] = saved_ra;
    state.pc = saved_pc;
    state.branch_pending = saved_branch_pending;
    state.branch_target = saved_branch_target;
    if (!executed) {
        g_last_call_error = CallError::execution_failed;
        return detail::failed_call_result<Ret>();
    }
    if (!result_valid) {
        g_last_call_error = CallError::unmapped_pointer;
        return detail::failed_call_result<Ret>();
    }
    state.stop_reason = saved_stop_reason;
    g_last_call_error = CallError::none;
    if constexpr (std::is_void_v<Ret>) return;
    else return result;
}

template <typename Ret = void, typename... Args>
inline Ret call_game_function(GuestAddress target, Args... args) {
    return call_game_function_impl<Ret>(target, nullptr, args...);
}
template <typename Ret = void, typename... Args>
inline Ret call_game_function(std::uint32_t target, Args... args) {
    return call_game_function<Ret>(guest_address(target), args...);
}

template <typename Ret = void, typename... Args>
inline Ret call_original_game_function(GuestAddress target, Args... args) {
    const PatchNode* bypass = nullptr;
    if (g_active_patch) {
        std::uint32_t resolved = 0;
        if (resolve_address(*g_active_state, target, resolved) &&
            canonical_address(resolved) == canonical_address(g_active_patch_address))
            bypass = g_active_patch;
    }
    if (!bypass && g_active_state) {
        std::uint32_t resolved = 0;
        if (resolve_address(*g_active_state, target, resolved))
            bypass = find_patch(*g_active_state, resolved);
    }
    return call_game_function_impl<Ret>(target, bypass, args...);
}
template <typename Ret = void, typename... Args>
inline Ret call_original_game_function(std::uint32_t target, Args... args) {
    return call_original_game_function<Ret>(guest_address(target), args...);
}

template <typename Ret = void, typename... Args>
inline Ret call_original(Args... args) {
    if (!g_active_state || !g_active_patch) {
        g_last_call_error = CallError::no_active_state;
        return detail::failed_call_result<Ret>();
    }
    return call_game_function_impl<Ret>(absolute_address(g_active_patch_address),
                                        g_active_patch, args...);
}

template <typename Signature>
class GameFunction;

template <typename Ret, typename... Args>
class GameFunction<Ret(Args...)> {
public:
    constexpr explicit GameFunction(GuestAddress address) : address_(address) {}
    constexpr explicit GameFunction(std::uint32_t address)
        : address_(guest_address(address)) {}
    Ret operator()(Args... args) const { return call_game_function<Ret>(address_, args...); }
    Ret original(Args... args) const {
        return call_original_game_function<Ret>(address_, args...);
    }
    constexpr GuestAddress address() const { return address_; }

private:
    GuestAddress address_{};
};

#define PSPRECOMP_PATCH_CONCAT_IMPL(a, b) a##b
#define PSPRECOMP_PATCH_CONCAT(a, b) PSPRECOMP_PATCH_CONCAT_IMPL(a, b)

#define RECOMP_PATCH_RAW(addr, hook_fn)                                      \
    static ::psprecomp::patch::PatchNode                                     \
        PSPRECOMP_PATCH_CONCAT(_recomp_patch_node_, __LINE__);               \
    [[maybe_unused]] static const bool                                       \
        PSPRECOMP_PATCH_CONCAT(_recomp_patch_reg_, __LINE__) =               \
        ::psprecomp::patch::register_raw_patch(                              \
            PSPRECOMP_PATCH_CONCAT(_recomp_patch_node_, __LINE__),           \
            (addr), #hook_fn, (hook_fn))

#define RECOMP_PATCH_FUNCTION(addr, function)                                \
    static ::psprecomp::patch::PatchNode                                     \
        PSPRECOMP_PATCH_CONCAT(_recomp_patch_node_fn_, __LINE__);            \
    [[maybe_unused]] static const bool                                       \
        PSPRECOMP_PATCH_CONCAT(_recomp_patch_reg_fn_, __LINE__) =            \
        ::psprecomp::patch::register_raw_patch(                              \
            PSPRECOMP_PATCH_CONCAT(_recomp_patch_node_fn_, __LINE__),        \
            (addr), #function,                                               \
            &::psprecomp::patch::detail::TypedHookInvoker<                   \
                decltype(function), function>::invoke)

#define RECOMP_PATCH_FUNCTION_BY_NAME(name_string, function)                 \
    static ::psprecomp::patch::PatchNode                                     \
        PSPRECOMP_PATCH_CONCAT(_recomp_patch_node_named_, __LINE__);         \
    [[maybe_unused]] static const bool                                       \
        PSPRECOMP_PATCH_CONCAT(_recomp_patch_reg_named_, __LINE__) =         \
        ::psprecomp::patch::register_named_patch(                            \
            PSPRECOMP_PATCH_CONCAT(_recomp_patch_node_named_, __LINE__),     \
            (name_string), #function,                                        \
            &::psprecomp::patch::detail::TypedHookInvoker<                   \
                decltype(function), function>::invoke)

#define RECOMP_PATCH_GHIDRA(function)                                        \
    RECOMP_PATCH_FUNCTION_BY_NAME(#function, function)

#define RECOMP_PATCH_INITIALIZER(function)                                   \
    static ::psprecomp::patch::InitializerNode                               \
        PSPRECOMP_PATCH_CONCAT(_recomp_patch_init_node_, __LINE__);          \
    [[maybe_unused]] static const bool                                       \
        PSPRECOMP_PATCH_CONCAT(_recomp_patch_init_reg_, __LINE__) =          \
        ::psprecomp::patch::register_initializer(                            \
            PSPRECOMP_PATCH_CONCAT(_recomp_patch_init_node_, __LINE__),      \
            #function,                                                       \
            &::psprecomp::patch::detail::InitializerInvoker<function>::invoke)

#define DEFINE_GAME_FUNCTION(name, addr, Ret, ...)                           \
    inline constexpr ::psprecomp::patch::GameFunction<Ret(__VA_ARGS__)>      \
        name{(addr)}

#define DEFINE_GAME_GLOBAL(name, addr, Type)                                 \
    inline ::psprecomp::patch::GameGlobal<Type> name{(addr)}

} // namespace psprecomp::patch
