#pragma once

#include <atomic>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32

#include <memory>

#pragma push_macro("NOMINMAX")
#pragma push_macro("WIN32_LEAN_AND_MEAN")
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma pop_macro("NOMINMAX")
#pragma pop_macro("WIN32_LEAN_AND_MEAN")

#else // if POSIX

#include <cstring> // For `std::strerror()`.
#include <dirent.h>
#include <fcntl.h> // For `fcntl()`.
#include <spawn.h> // For `posix_spawn()` and friends.
#include <sys/types.h> // For `pid_t`.
#include <sys/wait.h> // For `waitpid()`.
#include <unistd.h>

#if __APPLE__
#  include <crt_externs.h> // For `_NSGetEnviron()`.
#  include <Availability.h> // For SDK version macros.
#endif

#endif // POSIX


#ifndef _WIN32

// MacOS version checks BS.
#ifdef __APPLE__

// Do we have `__builtin_available`? Should be true on Clang and false on GCC.
#ifndef EM_PROC_HAVE_MACOS_BUILTIN_AVAILABLE
#  ifdef __has_builtin
#    if __has_builtin(__builtin_available)
#      define EM_PROC_HAVE_MACOS_BUILTIN_AVAILABLE 1
#    else
#      define EM_PROC_HAVE_MACOS_BUILTIN_AVAILABLE 0
#    endif
#  else
#    define EM_PROC_HAVE_MACOS_BUILTIN_AVAILABLE 0
#  endif
#endif

#if EM_PROC_HAVE_MACOS_BUILTIN_AVAILABLE
// Checks at runtime that MacOS version is at least this number.
// Note that apparently compilers can optimize this away if the min version is set high enough, so don't bother doing that manually.
#  define DETAIL_EM_PROC_MACOS_BUILTIN_AVAILABLE(x) __builtin_available(macOS x##.0, iOS x##.0, tvOS x##.0, watchOS x##.0, visionOS x##.0, *)
#endif

// Is min targeted Apple OS version at least `x`?
#define DETAIL_EM_PROC_MACOS_VERSION_MIN_REQUIRED_AT_LEAST(x) \
    __MAC_OS_X_VERSION_MIN_REQUIRED  >= (x##0000) || \
    __IPHONE_OS_VERSION_MIN_REQUIRED >= (x##0000) || \
    __TV_OS_VERSION_MIN_REQUIRED     >= (x##0000) || \
    __WATCH_OS_VERSION_MIN_REQUIRED  >= (x##0000) || \
    __VISION_OS_VERSION_MIN_REQUIRED >= (x##0000)
// Is max targeted Apple OS version at least `x`?
#define DETAIL_EM_PROC_MACOS_VERSION_MAX_ALLOWED_AT_LEAST(x) \
    __MAC_OS_X_VERSION_MAX_ALLOWED  >= (x##0000) || \
    __IPHONE_OS_VERSION_MAX_ALLOWED >= (x##0000) || \
    __TV_OS_VERSION_MAX_ALLOWED     >= (x##0000) || \
    __WATCH_OS_VERSION_MAX_ALLOWED  >= (x##0000) || \
    __VISION_OS_VERSION_MAX_ALLOWED >= (x##0000)

// If we have `__builtin_available`, then we check the min version, and then do runtime checks. Otherwise we check the max version and don't do runtime checks.
#if EM_PROC_HAVE_MACOS_BUILTIN_AVAILABLE
#  define DETAIL_EM_PROC_MACOS_VERSION_FOR_FEATURES_AT_LEAST(x) DETAIL_EM_PROC_MACOS_VERSION_MIN_REQUIRED_AT_LEAST(x)
#else
#  define DETAIL_EM_PROC_MACOS_VERSION_FOR_FEATURES_AT_LEAST(x) DETAIL_EM_PROC_MACOS_VERSION_MIN_REQUIRED_AT_LEAST(x)
#endif

#else // if not __APPLE__
#  define DETAIL_EM_PROC_MACOS_VERSION_FOR_FEATURES_AT_LEAST(x) false
#endif // __APPLE__


// Do we have `WCOREDUMP`?
#ifndef EM_PROC_CAN_DETECT_CORE_DUMPS
#  ifdef WCOREDUMP // Manual says to ifdef this: https://linux.die.net/man/2/waitpid
#    define EM_PROC_CAN_DETECT_CORE_DUMPS 1
#  else
#    define EM_PROC_CAN_DETECT_CORE_DUMPS 0
#  endif
#endif


// Do we have `pipe2()`?

#ifndef EM_PROC_HAVE_PIPE2
#  if defined(__linux__) && defined(_GNU_SOURCE)
#    define EM_PROC_HAVE_PIPE2 1
#  elif DETAIL_EM_PROC_MACOS_VERSION_FOR_FEATURES_AT_LEAST(27)
#    define EM_PROC_HAVE_PIPE2 1
#    if EM_PROC_HAVE_MACOS_BUILTIN_AVAILABLE
#      define EM_PROC_HAVE_PIPE2_RUNTIME DETAIL_EM_PROC_MACOS_BUILTIN_AVAILABLE(27)
#    endif
#  else
#    define EM_PROC_HAVE_PIPE2 0
#  endif
#endif

// `DETAIL_EM_PROC_DISPATCH_PIPE2(have_pipe2, no_pipe2)` expands to `have_pipe2` or `no_pipe2`. The condition can either be runtime or compile-time, depending on the platform.
#if EM_PROC_HAVE_PIPE2
#  ifdef EM_PROC_HAVE_PIPE2_RUNTIME
#    define DETAIL_EM_PROC_DISPATCH_PIPE2(have_pipe2, no_pipe2) (EM_PROC_HAVE_PIPE2_RUNTIME ? (have_pipe2) : (no_pipe2))
#  else
#    define DETAIL_EM_PROC_DISPATCH_PIPE2(have_pipe2, no_pipe2) have_pipe2
#  endif
#else
#  define DETAIL_EM_PROC_DISPATCH_PIPE2(have_pipe2, no_pipe2) no_pipe2
#endif


// Do we have `posix_spawn_file_actions_addclosefrom_np()`?
// At the time of writing, it exists on Linux but not on MacOS.

#ifndef EM_PROC_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCLOSEFROM_NP
// In theory, this function can be disabled with a macro. The condition is shared with `posix_spawn_file_actions_addchdir_np()`.
// In practice, since we don't handle missing `posix_spawn_file_actions_addchdir_np()` at all, I'm not bothering with this condition here.
#  ifdef __linux__
#    define EM_PROC_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCLOSEFROM_NP 1
#  else
#    define EM_PROC_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCLOSEFROM_NP 0
#  endif
#endif


// Can we access `/proc/self/fd`?
// Incorrectly setting this to true is not an issue. If this is true and `/proc/self/fd` fails to open, we fall back to another approach.
#ifndef EM_PROC_HAVE_PROC_SELF_FD
#  ifndef __APPLE__
#    define EM_PROC_HAVE_PROC_SELF_FD 1
#  else
#    define EM_PROC_HAVE_PROC_SELF_FD 0
#  endif
#endif


#endif // POSIX


#ifdef _WIN32
// Usage: `EM_PROC_NATIVE("blah")`.
// Makes a string literal wide on Windows, and returns it as is on other platforms.
#define EM_PROC_NATIVE(x) L"" x ""
#else
// Usage: `EM_PROC_NATIVE("blah")`.
// Makes a string literal wide on Windows, and returns it as is on other platforms.
#define EM_PROC_NATIVE(x) "" x ""
#endif

// Some user-facing OS macros if you prefer.
#ifdef _WIN32
#define EM_PROC_PLATFORM_WIN 1
#define EM_PROC_PLATFORM_POSIX 0
#else
#define EM_PROC_PLATFORM_WIN 0
#define EM_PROC_PLATFORM_POSIX 1
#endif

// Synthesizes operators for a enum of flags: `&`, `|`, and `~`. Also multiplication by a bool.
#define DETAIL_EM_PROC_FLAG_ENUM(name_) DETAIL_EM_PROC_FLAG_ENUM_CUSTOM(, name_)

// Same, but works at class scope.
#define DETAIL_EM_PROC_FLAG_ENUM_IN_CLASS(name_) DETAIL_EM_PROC_FLAG_ENUM_CUSTOM(friend, name_)

// Same, but lets you specify a custom decl-specifier-seq.
#define DETAIL_EM_PROC_FLAG_ENUM_CUSTOM(prefix_, name_) \
    [[nodiscard, maybe_unused]] prefix_ constexpr name_ operator&(name_ a, name_ b) {return name_(::std::underlying_type_t<name_>(a) & ::std::underlying_type_t<name_>(b));} \
    [[nodiscard, maybe_unused]] prefix_ constexpr name_ operator|(name_ a, name_ b) {return name_(::std::underlying_type_t<name_>(a) | ::std::underlying_type_t<name_>(b));} \
    [[nodiscard, maybe_unused]] prefix_ constexpr name_ operator~(name_ a) {return name_(~::std::underlying_type_t<name_>(a));} \
    [[maybe_unused]] prefix_ constexpr name_ &operator&=(name_ &a, name_ b) {return a = a & b;} \
    [[maybe_unused]] prefix_ constexpr name_ &operator|=(name_ &a, name_ b) {return a = a | b;} \
    [[nodiscard, maybe_unused]] prefix_ constexpr name_ operator*(name_ a, bool b) {return b ? a : name_{};} \
    [[nodiscard, maybe_unused]] prefix_ constexpr name_ operator*(bool a, name_ b) {return a ? b : name_{};} \
    [[maybe_unused]] prefix_ constexpr name_ &operator*=(name_ &a, bool b) {return a = a * b;} \
    [[nodiscard, maybe_unused]] prefix_ constexpr name_ operator<<(name_ a, int b) {return name_(::std::underlying_type_t<name_>(a) << b);} \
    [[nodiscard, maybe_unused]] prefix_ constexpr name_ operator>>(name_ a, int b) {return name_(::std::underlying_type_t<name_>(a) >> b);} \
    [[maybe_unused]] prefix_ constexpr name_ &operator<<=(name_ &a, int b) {return a = a << b;} \
    [[maybe_unused]] prefix_ constexpr name_ &operator>>=(name_ &a, int b) {return a = a >> b;} \

// Changes here relative to SDL:
// * Better error reporting from starting background (detached) processes. See: https://github.com/libsdl-org/SDL/issues/16188
// * Use return values instead of `errno` in a few places. But it seems in glibc those functions do set errno, even though it's not documented in the manual, so I'm not sure this ever matters.
// * Don't bother with android-specific code to obtain extra env variables from the application manifest, whatever that is.
// * Refuse to use `kill(pid, 0)` to wait for background (detached) processes. Since PIDs can be recycled, this seems unreliable.
// * Added "have core dump" check when a process stops due to a signal.
// * On Windows, the `CREATE_NO_WINDOW` flag that disables console allocation is not implied by process background-ness (detached-ness). It doesn't seem terribly useful, and we expose it separately.
// * Use `pipe2()` instead of `pipe()` when possible.

// Differences to reproc:
// * We don't try to use sockets as pipes as reproc does on Windows. Seems hacky, and they have several suspicious bug reports that look like they could be caused by those, that they didn't respond to.
//   They did that to support polling (so checking which pipes are ready without actually reading/writing to them), but we can use IO completion ports instead. Those seem to force you to queue an operation,
//     instead of just reporting the pipe status (like polling does on Linux), so the API has to be designed around it.

namespace em::Proc
{
    class BasicIoStream;
}

// Doing it here to avoid having to exit the namespace in the middle of the header.
// Don't want to set up some generic customization point either, just for my library.
template <>
struct std::hash<em::Proc::BasicIoStream>
{
    [[nodiscard]] std::size_t operator()(const em::Proc::BasicIoStream &stream) const;
};

namespace em::Proc
{
    // Some tag types:

    // Construct an object in an invalid state, storing the provided error message.
    struct TagErrorMessage {explicit TagErrorMessage() = default;};
    inline constexpr TagErrorMessage error_message{};
    // Take ownership of some owning handle.
    struct TagTakeOwnership {explicit TagTakeOwnership() = default;};
    inline constexpr TagTakeOwnership take_ownership{};
    // Store a handle or pointer without taking ownership of it.
    struct TagNonOwning {explicit TagNonOwning() = default;};
    inline constexpr TagNonOwning non_owning{};

    #ifdef _WIN32
    struct TagWindowsOnly {explicit TagWindowsOnly() = default;};
    // This is passed as the first argument to constructors to acknowledge that you understand that they're Windows-specific, and need to be `#if`ed.
    // Normal functions the the `_Win` suffix (or `_Posix` for POSIX) because it looks nicer.
    inline constexpr TagWindowsOnly windows_only{};
    #endif


    #ifdef _WIN32
    using NativeChar = wchar_t;
    using Pid = DWORD; // Process ID type. This is always some integer type.
    #else
    using NativeChar = char;
    using Pid = pid_t; // Process ID type. This is always some integer type.
    #endif


    // Stores the global library settings. See `GlobalSettings()` below.
    struct Settings
    {
        // Stores a single flag of the settings.
        class Flag
        {
            // I don't know if this use of atomics makes sense. Should be good enough.

          public:
            Flag(bool value) : storage(value) {}

            Flag(const Flag &) = delete;
            Flag &operator=(const Flag &) = delete;

            // Returns true on success, false if the flag was already consumed.
            bool Set(bool value)
            {
                std::uint8_t cur = storage.load(std::memory_order_relaxed);

                while (true)
                {
                    if (cur & 2)
                        return false; // Already was consumed.

                    if (storage.compare_exchange_weak(cur, value, std::memory_order_relaxed))
                        return true;
                }
            }

            // Returns the current value of the flag.
            // If `out_already_consumed` is specified, updates it to indicate if the library already consumed the value of the flag. In that case it's too late to change it, and `Set()` will always fail.
            [[nodiscard]] bool Get(bool *out_already_consumed = nullptr) const
            {
                std::uint8_t cur = storage.load(std::memory_order_relaxed);
                if (out_already_consumed)
                    *out_already_consumed = bool(cur & 2);
                return bool(cur & 1);
            }

            // Returns the current value of the flag, same as `Get()`, but also prevents further changes to it.
            // Normally you shouldn't call this yourself, the library will do it for you.
            [[nodiscard]] bool Consume()
            {
                return bool(storage.fetch_or(2, std::memory_order_relaxed) & 1);
            }

          private:
            // Lowest bit is value, second bit is whether it was already read.
            std::atomic_uint8_t storage = 1;
        };

        #ifdef _WIN32
        // Set to false to not call `SetErrorMode(SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS)`.
        // Calling that prevents some implicit error messageboxes from being displayed (mainly when opening files?), and instead just reports the errors to the application.
        // SDL sets this temporarily for opening files, but since this is process-wide, I think I'd rather do it once.
        Flag disable_error_messageboxes_win = true;
        #endif

        // Sets all the flags to false, to opt out of this library touching any process-wide settings.
        // Returns true on success, or false if some of the flags were already consumed.
        bool DisableAllFlags()
        {
            bool ok = true;

            #ifdef _WIN32
            if (!disable_error_messageboxes_win.Set(false))
                ok = false;
            #endif

            return ok;
        }
    };

    // Returns the global library settings that you can modify. You should set them before the first use of the library.
    //
    // By default, this library will change some process-wide platform-specific settings to sane values.
    // You can opt out of that by changing the respective flag to false, or `GlobalSettings().DisableAllFlags();` to set all flags to false.
    //
    // The library checks each flag exactly once, when it needs it. After that it can't be changed.
    //
    // If you use this library from multiple DLLs (possibly in a header included in multiple DLLs), each DLL will have its own copy of the settings, and will try to apply them again for itself too, even if another one already did.
    // But the underlying settings are process-wide, not DLL-wide, so make sure you set the flags to the same values in every DLL to get sane behavior.
    // (A false flag does nothing instead of undoing the process-wide changes, so if the settings are not consistent across DLLs, those process-wide settings should converge on the OR of the flags across DLLs.)
    [[nodiscard]] inline Settings &GlobalSettings()
    {
        static Settings ret;
        return ret;
    }


    namespace detail
    {
        template <typename T>
        constexpr bool is_basic_string_view = false;
        template <typename T>
        constexpr bool is_basic_string_view<std::basic_string_view<T>> = true;

        // True if `T` is not a specialization of `basic_string_view` but is convertible to one with CTAD.
        template <typename T>
        concept ConvertibleToStringView = !is_basic_string_view<std::remove_cvref_t<T>> && requires(T &&t){std::basic_string_view(decltype(t)(t));};
    }

    #ifdef _WIN32
    // This is primarily for internal use, and exposed as a courtesy. You can also use `class NativeString` defined below.
    // Converts between `std::string` and `std::wstring` in both directions.
    // By default, replaces invalid characters in the input with placeholders.
    // If `success` is specified, instead returns an empty string if the input has invalid characters, and writes `false` to `success`. On success, writes `true`.
    template <typename Char>
    [[nodiscard]] std::basic_string<std::conditional_t<std::is_same_v<Char, char>, wchar_t, char>> ConvertString_Win(std::basic_string_view<Char> in, bool *success = nullptr)
    {
        using RetChar = std::conditional_t<std::is_same_v<Char, char>, wchar_t, char>;

        // Put this here for NVRO purposes.
        std::basic_string<RetChar> ret;

        if (in.empty())
        {
            // This is special-cased because `MultiByteToWideChar()` returns 0 to indicate failure.
            if (success)
                *success = true;
            return ret;
        }

        const DWORD flags = success ? MB_ERR_INVALID_CHARS : 0;

        auto Convert = [&](RetChar *out, int out_size) -> int
        {
            if constexpr (std::is_same_v<Char, wchar_t>)
                return WideCharToMultiByte(CP_UTF8, flags, in.data(), (int)in.size(), out, out_size, nullptr, nullptr);
            else
                return MultiByteToWideChar(CP_UTF8, flags, in.data(), (int)in.size(), out, out_size);
        };

        int expected_size = Convert(nullptr, 0);
        if (expected_size == 0)
        {
            // If `!success`, this shouldn't be possible. Then assert.
            assert(success && "`MultiByteToWideChar()` failed when calculating the buffer size.");

            if (success)
                *success = false;
            return ret;
        }

        ret.resize(std::size_t(expected_size)); // `expected_size` only includes space for null-terminator if the input size included it, so not in our case.

        int actual_size = Convert(ret.data(), expected_size);
        if (actual_size == 0)
        {
            // If `!success`, this shouldn't be possible. Then assert.
            assert(success && "`MultiByteToWideChar()` failed when encoding.");

            ret.clear(); // Avoid returning half-baked string.

            if (success)
                *success = false;
            return ret;
        }

        assert(actual_size == expected_size); // Can they ever not be equal?
        ret.resize(std::size_t(actual_size)); // If not, this will shrink the string.

        if (success)
            *success = true;
        return ret;
    }

    // A convenience overload to take things convertible to `std::basic_string_view` (as opposed to `std::basic_string_view` itself) without specifying the template argument.
    [[nodiscard]] auto ConvertString_Win(detail::ConvertibleToStringView auto &&in, bool *success = nullptr)
    {
        return ConvertString_Win(std::basic_string_view(decltype(in)(in)), success);
    }
    #endif

    // We use this instead of `std::string` in a few places.
    // On Windows this stores `std::wstring` rather than `std::string`, but it's still constructible from `std::string` on all platforms (on Windows this converts from UTF-8).
    // On Windows you can construct it from `std::wstring` directly if you have that. (Similarly narrow and wide `string_view`s, etc.)
    // This is currently only used to store environment variables, but you're also free to use it in your own code.
    struct NativeString
    {
        using UnderlyingType = std::basic_string<NativeChar>;

        // `std::wstring` on Windows, `std::string` on POSIX.
        UnderlyingType native;


        // Constructs an empty string.
        [[nodiscard]] NativeString() {}

        NativeString(std::nullptr_t) = delete;

        #ifdef _WIN32
        // Set the value as a UTF-8 string.
        // If `success` is not specified, then invalid characters in the input will be replaced with placeholders in the result.
        // If `success` is specfied, then invalid characters cause the result to be empty instead. Writes true to `success` on success and false on failure.
        // Not specifying `success` is completely fine.
        [[nodiscard]] NativeString(std::string_view value, bool *success = nullptr) : native(ConvertString_Win(value, success)) {}
        [[nodiscard]] NativeString(const std::string &value, bool *success = nullptr) : NativeString(std::string_view(value), success) {}
        [[nodiscard]] NativeString(const char *value, bool *success = nullptr) : NativeString(std::string_view(value), success) {}

        // This assigns to the underlying string directly.
        // Since this doesn't need to change the encoding, so there's no `success` parameter.
        [[nodiscard]] NativeString(std::wstring value) : native(std::move(value)) {}
        // Add more overloads to help with implicit conversions.
        [[nodiscard]] NativeString(std::wstring_view value) : native(value) {}
        [[nodiscard]] NativeString(const wchar_t *value) : native(value) {}
        #else
        // Set the string value.
        // On POSIX those never fail. If `success` is specified, writes `true` to it.
        // See the comments on the Windows version fow how it can fail there.
        [[nodiscard]] NativeString(std::string value, bool *success = nullptr)      : native(std::move(value)) {if (success) *success = true;}
        [[nodiscard]] NativeString(std::string_view value, bool *success = nullptr) : native(value)            {if (success) *success = true;}
        [[nodiscard]] NativeString(const char *value, bool *success = nullptr)      : native(value)            {if (success) *success = true;}
        #endif

        #ifdef _WIN32
        // Returns the value as a UTF-8 string.
        // If `success` is not specified, then invalid characters in the input will be replaced with placeholders in the result.
        // If `success` is specfied, then invalid characters cause the result to be empty instead. Writes true to `success` on success and false on failure.
        // Not specifying `success` is completely fine.
        [[nodiscard]] std::string get(bool *success = nullptr) const
        {
            return ConvertString_Win(native, success);
        }
        #else
        // Returns the stored string.
        // Never fails on POSIX, so if `success` is specified, always writes true to it. See the Windows comment for how this can fail on Windows.
        [[nodiscard]] const std::string &get(bool *success = nullptr) const &
        {
            if (success)
                *success = true;
            return native;
        }
        [[nodiscard]] std::string &&get(bool *success = nullptr) &&
        {
            if (success)
                *success = true;
            return std::move(native);
        }
        #endif

        // Compare with itself.
        // Fun fact: having custom `==`s below forces us to explicitly default this `==`. Defaulting the `<=>` alone is no longer enough because of those other `==`s.
        friend bool                 operator== (const NativeString &, const NativeString &) = default;
        friend std::strong_ordering operator<=>(const NativeString &, const NativeString &) = default;

        // Compare with the native string type, in all the different forms.
        friend bool                 operator== (const NativeString &a, const UnderlyingType &b) {return a.native == b;}
        friend std::strong_ordering operator<=>(const NativeString &a, const UnderlyingType &b) {return a.native <=> b;}

        friend bool                 operator== (const NativeString &a, std::basic_string_view<NativeChar> b) {return a.native == b;}
        friend std::strong_ordering operator<=>(const NativeString &a, std::basic_string_view<NativeChar> b) {return a.native <=> b;}

        friend bool                 operator== (const NativeString &a, const NativeChar *b) {return a.native == b;}
        friend std::strong_ordering operator<=>(const NativeString &a, const NativeChar *b) {return a.native <=> b;}
    };

    namespace detail
    {
        template <typename ...P>
        struct Overload : P... {using P::operator()...;};
        template <typename ...P>
        Overload(P...) -> Overload<P...>; // Keep this for older compilers, just in case.


        // Returns the array size for `argv`-style arrays.
        // If `ptr` is null, returns zero.
        [[nodiscard]] inline std::size_t PtrArraySize(const auto *const *ptr)
        {
            std::size_t ret = 0;
            if (ptr) // Return zero if `ptr` is null.
            {
                while (ptr[ret])
                    ret++;
            }
            return ret;
        }

        [[nodiscard]] auto &&UnderlyingString(auto &&str)
        {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(str)>, NativeString>)
                return decltype(str)(str).native;
            else
                return decltype(str)(str);
        }

        // Stores either a non-owning `Char *` (which may or may not be const!) or an owning `std::basic_string<std::remove_const_t<Char>>`.
        // NOTE: The two variants with `String = const ??;` have the public typedefs `MaybeOwningString` and `MaybeOwningNativeString`.
        template <typename Char>
        requires
            std::is_same_v<std::remove_const_t<Char>, char>
            #ifdef _WIN32
            || std::is_same_v<std::remove_const_t<Char>, wchar_t>
            #endif
        class MaybeOwningMaybeMutString
        {
            using UnqualChar = std::remove_const_t<Char>;
            using String = std::basic_string<UnqualChar>;

            String string;
            Char *ptr_override = nullptr;

            static constexpr bool is_wide =
                #ifdef _WIN32
                std::is_same_v<UnqualChar, wchar_t>;
                #else
                false;
                #endif

          public:
            [[nodiscard]] constexpr MaybeOwningMaybeMutString() {}

            MaybeOwningMaybeMutString(std::nullptr_t) = delete;

            #ifdef _WIN32
            // From narrow strings.
            // On Windows, those can run into encoding errors. By default, they replace bad characters with placeholders.
            // But if `success` is specified, they instead return an empty string on failure, and set `success` to `false`. On success, they set it to `true`.
            [[nodiscard]] MaybeOwningMaybeMutString(const std::string &str, bool *success = nullptr) requires is_wide : string(ConvertString_Win(str, success)) {}
            [[nodiscard]] MaybeOwningMaybeMutString(std::string_view str, bool *success = nullptr) requires is_wide : string(ConvertString_Win(str, success)) {}
            [[nodiscard]] MaybeOwningMaybeMutString(const char *str, bool *success = nullptr) requires is_wide : string(ConvertString_Win(str, success)) {}
            #endif

            // From narrow strings.
            // On POSIX those never fail. See the Windows versions above for more details.
            [[nodiscard]] MaybeOwningMaybeMutString(std::string str, bool *success = nullptr) requires(!is_wide) : string(std::move(str)) {if (success) *success = true;}
            [[nodiscard]] MaybeOwningMaybeMutString(std::string_view str, bool *success = nullptr) requires(!is_wide) : string(str) {if (success) *success = true;}
            [[nodiscard]] MaybeOwningMaybeMutString(const char *str, bool *success = nullptr) requires(!is_wide) : string(str) {if (success) *success = true;}

            #ifdef _WIN32
            // From wide strings.
            // Those never fail, so no `bool *success`.
            [[nodiscard]] MaybeOwningMaybeMutString(String str) requires is_wide : string(std::move(str)) {}
            [[nodiscard]] MaybeOwningMaybeMutString(std::wstring_view str) requires is_wide : string(str) {}
            [[nodiscard]] MaybeOwningMaybeMutString(const wchar_t *str) requires is_wide : string(str) {}
            #endif

            // Non-owning narrow.
            [[nodiscard]] MaybeOwningMaybeMutString(TagNonOwning, const char *ptr) requires(!is_wide) : ptr_override(ptr) {}
            [[nodiscard]] MaybeOwningMaybeMutString(TagNonOwning, const char *ptr) requires is_wide : MaybeOwningMaybeMutString(ptr) {} // This just ignores `non_owning`.

            // Non-owning wide.
            [[nodiscard]] MaybeOwningMaybeMutString(TagNonOwning, const wchar_t *ptr) requires is_wide : ptr_override(ptr) {}

            // From `NativeString`. Why not.
            [[nodiscard]] MaybeOwningMaybeMutString(NativeString str) requires is_wide
                : string(std::move(str.native))
            {}

            // Convert back to `NativeString`.
            [[nodiscard]] operator NativeString() const
            {
                return NativeString(GetStringView());
            }


            [[nodiscard]] MaybeOwningMaybeMutString(const MaybeOwningMaybeMutString &other) = default;

            [[nodiscard]] MaybeOwningMaybeMutString(MaybeOwningMaybeMutString &&other) noexcept
                : string(std::move(other.string)), ptr_override(other.ptr_override)
            {
                other.string = {};
                other.ptr_override = nullptr;
            }

            MaybeOwningMaybeMutString &operator=(const MaybeOwningMaybeMutString &other) = default;

            MaybeOwningMaybeMutString &operator=(MaybeOwningMaybeMutString &&other) noexcept
            {
                if (&other == this) // A courtesy, not strictly necessary.
                    return *this;

                string = std::move(other.string);
                ptr_override = other.ptr_override;

                other.string = {};
                other.ptr_override = nullptr;
                return *this;
            }

            [[nodiscard]] Char *GetMutPointer() requires(!std::is_const_v<Char>)
            {
                return ptr_override ? ptr_override : string.data();
            }
            [[nodiscard]] Char *GetPointer() const requires std::is_const_v<Char>
            {
                return ptr_override ? ptr_override : string.c_str();
            }

            [[nodiscard]] std::basic_string_view<UnqualChar> GetStringView() const
            {
                // Not ternary to avoid having to cast the branches to `basic_string_view`.
                if (ptr_override)
                    return ptr_override;
                else
                    return string;
            }

            // This is usually not necessary, unless you're modifying the string directly.
            [[nodiscard]] String &GetOwnedString()
            {
                assert(!ptr_override);
                return string;
            }
        };

        template <typename T>
        constexpr bool is_MaybeOwningMaybeMutString = false;
        template <typename Char>
        constexpr bool is_MaybeOwningMaybeMutString<MaybeOwningMaybeMutString<Char>> = true;

        template <typename T>
        struct StringToCharTypeImpl {using type = typename T::value_type;};
        template <>
        struct StringToCharTypeImpl<NativeString> {using type = NativeChar;};
        template <typename T>
        struct StringToCharTypeImpl<MaybeOwningMaybeMutString<T>> {using type = std::remove_const_t<T>;};

        template <typename T> requires std::is_same_v<std::remove_cvref_t<T>, T>
        using StringToCharType = typename StringToCharTypeImpl<T>::type;


        #ifndef _WIN32
        // Stores an argv-like array of strings. Can be owning or non-owning.
        // Can't be null. Passing a null pointer makes it empty.
        template <typename String>
        class StringPtrArray
        {
            using Char = StringToCharType<String>;

            std::vector<String> storage;
            std::vector<const Char *> pointers;

            // If this is specified, it replaces `pointers.data()` as the final value.
            const Char *const *ptr_override = nullptr;

            [[nodiscard]] static const Char *StringToPtr(const String &str)
            {
                if constexpr (is_MaybeOwningMaybeMutString<String>)
                    return str.GetPointer(); // Not bothering with `GetMutablePointer`, the mutable variants shouldn't appear here.
                else
                    return str.c_str();
            }

          public:
            // Constructs an invalid instance. `GetPointer()` will return null on those.
            [[nodiscard]] constexpr StringPtrArray() {}

            [[nodiscard]] StringPtrArray(const StringPtrArray &other)
            {
                *this = other;
            }

            StringPtrArray &operator=(const StringPtrArray &other)
            {
                if (!other)
                {
                    // `*this = {};` would call the move assignment. I'd rather do it myself.
                    storage = {};
                    pointers = {};
                    ptr_override = nullptr;
                    return *this;
                }

                if (other.ptr_override)
                {
                    ptr_override = other.ptr_override;
                    storage = {};
                    pointers = {};
                    return *this;
                }

                // Zero the instance if something throws.
                struct ExceptionGuard
                {
                    StringPtrArray *self;
                    ~ExceptionGuard()
                    {
                        if (self)
                            *self = {};
                    }
                };
                ExceptionGuard exception_guard{this};

                storage = other.storage;

                std::size_t num_elems = storage.size();
                pointers.resize(num_elems + 1);
                for (std::size_t i = 0; i < num_elems; i++)
                    pointers[i] = StringToPtr(storage[i]);
                pointers[num_elems] = nullptr; // Since we're reusing the existing array, must explicitly reset this to null in case it wasn't before.

                ptr_override = nullptr;

                exception_guard.self = nullptr; // Disarm the guard.

                return *this;
            }

            [[nodiscard]] StringPtrArray(StringPtrArray &&other) noexcept
            {
                *this = std::move(other);
            }

            StringPtrArray &operator=(StringPtrArray &&other) noexcept
            {
                if (&other == this) // Not strictly required, just good manners.
                    return *this;

                if (!other)
                {
                    // `*this = {};` would infinitely recurse.
                    storage = {};
                    pointers = {};
                    ptr_override = nullptr;
                    return *this;
                }

                if (other.ptr_override)
                {
                    ptr_override = std::exchange(other.ptr_override, nullptr);
                    storage = {};
                    pointers = {};
                    return *this;
                }

                // Moved-from vectors are empty in practice.
                storage = std::move(other.storage);
                pointers = std::move(other.pointers);
                ptr_override = nullptr;
                return *this;
            }

            // Constructs an array of `num_elems` strings. Each string is produced by calling `make_elem(i)`,
            //   which must return `std::basic_string<Char>` (or `NativeString` if `UseNativeString == true`), or something convertible to it.
            // `make_elem` is always called in order, so it can ignore `i` if that's more convenient.
            [[nodiscard]] StringPtrArray(std::size_t num_elems, auto &&make_elem)
                : StringPtrArray([&]{
                    std::vector<String> new_storage;
                    new_storage.reserve(num_elems);
                    for (std::size_t i = 0; i < num_elems; i++)
                        new_storage.push_back(make_elem(std::size_t(i))); // Cast to make `i` an rvalue, just in case.
                    return new_storage;
                }())
            {}

            // Construct directly from the underlying vector.
            [[nodiscard]] StringPtrArray(std::vector<String> vec)
                : storage(std::move(vec))
            {
                std::size_t num_elems = storage.size();

                pointers.resize(num_elems + 1); // One extra null pointer at the end.
                for (std::size_t i = 0; i < num_elems; i++)
                    pointers[i] = StringToPtr(storage[i]);
            }

            [[nodiscard]] StringPtrArray(const Char *const *ptr)
                : StringPtrArray(PtrArraySize(ptr), [&](std::size_t i){return ptr[i];})
            {}

            [[nodiscard]] StringPtrArray(TagNonOwning, const Char *const *ptr)
                : ptr_override(ptr)
            {}

            // Returns true if this is a valid instance.
            [[nodiscard]] explicit operator bool() const {return ptr_override || !pointers.empty();}

            [[nodiscard]] const Char *const *GetPointer() const
            {
                if (!*this)
                    return nullptr;

                return ptr_override ? ptr_override : pointers.data();
            }
        };
        #endif


        #ifdef _WIN32
        // The `error` parameter should come from `GetLastError()`.
        [[nodiscard]] inline std::string WinApiErrorToString(DWORD error)
        {
            // Try getting the message in English first, and then try in the default language.
            // Discussion https://stackoverflow.com/q/12715646/2752075 shows that firstly `0` doesn't mean English, and secondly that it's not guaranteed that English strings are available at all.
            for (DWORD lang : {(DWORD)MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), DWORD(0)})
            {
                struct Guard
                {
                    wchar_t *message = nullptr;
                    ~Guard()
                    {
                        if (message)
                            LocalFree(message);
                    }
                };
                Guard guard;

                auto status = FormatMessageW(
                    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                    nullptr,
                    error,
                    lang,
                    // When `FORMAT_MESSAGE_ALLOCATE_BUFFER` is specfieid, this parameter changes meaning from `wchar_t *` to `wchar_t **`, so the manual says we need the cast.
                    (wchar_t *)&guard.message,
                    // Output buffer size. When `FORMAT_MESSAGE_ALLOCATE_BUFFER` is specified, it instead means the minimum amount of memory to allocate in the result, so 0 is fine.
                    0,
                    nullptr
                );

                if (status > 0)
                {
                    std::wstring_view ret_wide(guard.message);

                    // SDL says it can end with `\r\n`.
                    if (ret_wide.ends_with(L"\r\n"))
                        ret_wide.remove_suffix(2);

                    std::string ret = ConvertString_Win(ret_wide); // Ignoring encoding errors here, having placeholder characters in the string is fine.
                    std::erase(ret, '\r'); // For a good measure.

                    return ret;
                }
            }

            // If the message wasn't available in any language:
            return std::to_string(error) + " (no error message is available)";
        }
        #endif
    }

    // Ugh. I might simplify all those to `NativeString` one day.

    // Similar to `NativeString`, but can also store non-owning pointers (use constructors with `em::Proc::non_owning` tag).
    // You're not really intended to create instances of this in user code, prefer `class NativeString`. You can think of this class as of a `NativeString` equivalent.
    using MaybeOwningNativeString = detail::MaybeOwningMaybeMutString<const NativeChar>;
    // This variant wants the pointer to be non-const, and is forced to copy if you give it a const pointer. You don't need to worry about this.
    using MaybeOwningMutNativeString = detail::MaybeOwningMaybeMutString<NativeChar>;

    // Similar to `std::string`, but can also store non-owning pointers (use constructors with `em::Proc::non_owning` tag).
    // You're not really intended to create instances of this in user code.
    // Unlike `MaybeOwningNativeString`, this stores a narrow string and can't be constructed from a wide string directly.
    using MaybeOwningString = detail::MaybeOwningMaybeMutString<const char>;

    // Similar to `MaybeOwningNativeString`, but more aggressively tries to be non-owning when possible. It's supposed to be used as a function parameter, so it's also not default-constructible.
    // You're not really intended to create instances of this in user code, prefer `class NativeString`. You can think of this class as of a `NativeString` equivalent.
    struct NativeCStringViewParam : MaybeOwningNativeString
    {
        // Intentionally not default-constructible.

        NativeCStringViewParam(std::nullptr_t) = delete;

        [[nodiscard]] NativeCStringViewParam(std::string_view str) : MaybeOwningNativeString(str) {} // Isn't necessarily null-terminated, have to copy.
        [[nodiscard]] NativeCStringViewParam(const std::string &str) : MaybeOwningNativeString(non_owning, str.c_str()) {}
        [[nodiscard]] NativeCStringViewParam(const char *str) : MaybeOwningNativeString(non_owning, str) {}

        #ifdef _WIN32
        [[nodiscard]] NativeCStringViewParam(std::wstring_view str) : MaybeOwningNativeString(str) {} // Isn't necessarily null-terminated, have to copy.
        [[nodiscard]] NativeCStringViewParam(const std::wstring &str) : MaybeOwningNativeString(non_owning, str.c_str()) {}
        [[nodiscard]] NativeCStringViewParam(const wchar_t *str) : MaybeOwningNativeString(non_owning, str) {}
        #endif

        // This preserves owning-ness.
        // Note that the reverse operation is implicit because of the inheritance.
        [[nodiscard]] NativeCStringViewParam(MaybeOwningNativeString other) : MaybeOwningNativeString(std::move(other)) {}
    };

    // A base class that stores an optional error string.
    // Primarily for internal use. A lot of our classes inherit from this.
    // NOTE: When inheriting from this, you probably want to inherit ctors: `using StoresErrorMessage::StoresErrorMessage;`.
    // NOTE: When inheriting from this, if you override the move constructors, don't forget to move the message too!
    class StoresErrorMessage
    {
      protected:
        // You can assign to this directly if you prefer.
        std::string error_string;

      public:
        // Stores no error.
        [[nodiscard]] constexpr StoresErrorMessage() {}

        // Stores an error message.
        [[nodiscard]] StoresErrorMessage(TagErrorMessage, std::string error_string) : error_string(std::move(error_string)) {}

        // Copyable and movable. Moved-from instances are guaranteed to not retain errors.

        [[nodiscard]] StoresErrorMessage(const StoresErrorMessage &) = default;
        [[nodiscard]] StoresErrorMessage(StoresErrorMessage &&other) noexcept : error_string(std::move(other.error_string)) {other.error_string = {};}

        StoresErrorMessage &operator=(const StoresErrorMessage &) = default;
        StoresErrorMessage &operator=(StoresErrorMessage &&other) noexcept
        {
            if (&other != this) // I know this check isn't required, but it's more sane this way.
            {
                error_string = std::move(other.error_string);
                other.error_string = {};
            }
            return *this;
        }

        // Is this instance in an invalid state, storing an error message?
        [[nodiscard]] bool HasError() const noexcept {return !error_string.empty();}

        // Returns the error message, or empty if `HasError() == false`.
        [[nodiscard]] const std::string &ErrorMessage() const noexcept {return error_string;}
    };

    // Explains the result of a read or write operation.
    enum class IoResult
    {
        // Read or write successful.
        // This usually means that at least one byte was processed, but zero bytes is possible too (Windows docs say that it can happen when reading when the other end of the pipe calls `WriteFile()` with zero length).
        ok,

        // No data to read or write, try again later.
        // This should only happen for non-blocking streams.
        retry_later,

        // Nothing more to read. For a pipe, this means the remote end is closed. For a file, this is EOF. For files this can only happen on read, but for pipes it can happen on both reads and writes.
        end_reached,

        // You passed zero bytes, nothing to do.
        no_data,

        // Something broke,
        error,
    };

    [[nodiscard]] inline const char *to_string(IoResult result)
    {
        switch (result)
        {
            case IoResult::ok:           return "ok";
            case IoResult::retry_later:  return "retry_later";
            case IoResult::end_reached: return "end_reached";
            case IoResult::no_data:      return "no_data";
            case IoResult::error:        return "error";
        }
        assert(false && "Invalid enum.");
        return "??";
    }

    // A base class for files, pipes, etc.
    class BasicIoStream : public StoresErrorMessage
    {
      public:
        #ifdef _WIN32
        using handle_t = HANDLE;
        // There's some weirdness with what counts as a valid handle. `INVALID_HANDLE_VALUE` is basically `(HANDLE)-1`, but `nullptr` also seems to be invalid. Using `INVALID_HANDLE_VALUE` seems better to me.
        // Also this can't be constexpr! D:<
        inline static const handle_t invalid_handle = INVALID_HANDLE_VALUE;
        #else
        using handle_t = int;
        static constexpr handle_t invalid_handle = -1; // `0` is stdin.
        #endif

      private:
        struct State
        {
            handle_t handle = invalid_handle;
            bool owns_handle = false;
        };
        State state;

      public:
        [[nodiscard]] constexpr BasicIoStream() {}

        using StoresErrorMessage::StoresErrorMessage;

        // Takes ownership of an existing handle. Mainly for internal use.
        [[nodiscard]] BasicIoStream(TagTakeOwnership, handle_t handle)
            : BasicIoStream(non_owning, handle)
        {
            if (*this) // Only set the ownership flag on non-null handles.
                state.owns_handle = true;
        }

        // Stores an existing handle without taking ownership. Rarely useful.
        [[nodiscard]] BasicIoStream(TagNonOwning, handle_t handle)
        {
            state.handle = handle;

            #if _WIN32
            // On Windows, null is also an invalid handle, in addition to `INVALID_HANDLE_VALUE` which is `(HANDLE)-1`.
            // Instead of expanding `operator bool` to check that, I'd rather adjust it here.
            if (state.handle == nullptr)
                state.handle = invalid_handle;
            #endif
        }

        [[nodiscard]] BasicIoStream(BasicIoStream &&other) noexcept
            : StoresErrorMessage(std::move(other)),
            state(std::move(other.state))
        {
            other.state = {};
        }

        BasicIoStream &operator=(BasicIoStream other) noexcept
        {
            std::swap(static_cast<StoresErrorMessage &>(*this), static_cast<StoresErrorMessage &>(other));
            std::swap(state, other.state);
            return *this;
        }

        // Not protected, because it's very useful to be able to do `= {...}` on `BasicIoStream &`, etc.
        ~BasicIoStream()
        {
            if (state.owns_handle)
            {
                // Don't care about the error message at this point.
                #ifdef _WIN32
                [[maybe_unused]] bool ok = CloseHandle(state.handle); // Unlike POSIX `close()`, this returns non-zero on success.
                assert(ok);
                #else
                [[maybe_unused]] bool ok = close(state.handle) == 0;
                assert(ok);
                #endif
            }
        }

        [[nodiscard]] explicit operator bool() const
        {
            return state.handle != invalid_handle;
        }

        // If this instance is null, writes a error message nothing this fact in `ErrorMessage()` and returns true.
        // If not null, returns false.
        // Mainly for internal use.
        [[nodiscard]] bool StoreErrorIfNull()
        {
            if (!*this)
            {
                if (!HasError()) // Don't clobber the existing error, if any.
                    error_string = "This IO stream instance is null.";
                return true;
            }
            return false;
        }

        [[nodiscard]] handle_t Handle() const {return state.handle;}
        [[nodiscard]] bool IsOwning() const {return state.owns_handle;}

        // Returns the current handle, and then releases ownership of it, if any, and resets this instance to null.
        // It's then your job to free it.
        [[nodiscard]] handle_t ReleaseHandle_Unsafe() &&
        {
            state.owns_handle = false;
            return std::exchange(state.handle, invalid_handle);
        }

        [[nodiscard]] friend bool operator==(const BasicIoStream &a, const BasicIoStream &b)
        {
            return a.state.handle == b.state.handle;
        }
        [[nodiscard]] friend std::strong_ordering operator<=>(const BasicIoStream &a, const BasicIoStream &b)
        {
            return a.state.handle <=> b.state.handle;
        }

      protected:
        // Perform a read or write.
        // `out_result` is optional.
        // Returns true when the result is `IoResult::ok` (regardless of `out_result` being specified or not).
        // On failure, resets the handle to null so you don't miss the error.
        // Don't call this on the async streams on POSIX.
        // On Windows, `async_data_win` must be non-null for async streams, and null for sync streams.
        template <bool Read>
        bool ReadOrWriteUnbuffered(
            std::conditional_t<Read, std::span<char>, std::span<const char>> data,
            std::size_t &pos,
            IoResult *out_result
            #ifdef _WIN32
            , OVERLAPPED *async_data_win = nullptr
            #endif
        )
        {
            if (StoreErrorIfNull())
            {
                if (out_result)
                    *out_result = IoResult::error;
                return false;
            }

          [[maybe_unused]] retry:

            // Note that `pos == data.size()` is a valid no-op, but `pos > data.size()` is an assert (and a no-op in release builds).
            assert(pos <= data.size());
            if (pos >= data.size())
            {
                if (out_result)
                    *out_result = IoResult::no_data;
                return false;
            }

            std::size_t remaining_size = data.size() - pos;

            #ifdef _WIN32

            if (remaining_size > 0xffff'ffff)
                remaining_size = 0xffff'ffff; // This has to fit in 32-bit DWORD.

            DWORD processed_size = 0;

            bool ok = [&]{
                if constexpr (Read)
                    return ReadFile(Handle(), data.data() + pos, DWORD(remaining_size), &processed_size, async_data_win);
                else
                    return WriteFile(Handle(), data.data() + pos, DWORD(remaining_size), &processed_size, async_data_win);
            }();

            if (!ok)
            {
                auto error = GetLastError();

                // Should retry later?
                if (error == ERROR_NO_DATA)
                {
                    // This better not happen randomly on blocking pipes. It shouldn't.
                    if (out_result)
                        *out_result = IoResult::retry_later;
                    return false;
                }

                // EOF or pipe disconnected?
                if (
                    // Mostly certain about those:
                    error == ERROR_HANDLE_EOF || error == ERROR_BROKEN_PIPE ||
                    // Less certain about this:
                    error == ERROR_PIPE_NOT_CONNECTED
                )
                {
                    if (out_result)
                        *out_result = IoResult::end_reached;
                    return false;
                }

                // Surely an error at this point.
                *this = {error_message, std::string(Read ? "`ReadFile()` failed: " : "`WriteFile()` failed: ") + detail::WinApiErrorToString(error)};
                if (out_result)
                    *out_result = IoResult::error;
                return false;
            }

            // Docs say that `processed_size == 0` is possible at this point on read, if the other end called `WriteFile` with zero length.
            // We don't mind.

            assert(std::size_t(processed_size) <= remaining_size);
            pos += std::size_t(processed_size);

            if (out_result)
                *out_result = IoResult::ok;
            return true;

            #else

            auto result = [&]{
                if constexpr (Read)
                    return read(Handle(), data.data() + pos, remaining_size);
                else
                    return write(Handle(), data.data() + pos, remaining_size);
            }();

            int errno_copy = 0;
            if (result < 0)
                errno_copy = errno;

            // Just in case, protect against `write()` returning zero. This should never happen.
            if constexpr (!Read)
            {
                if (result == 0)
                {
                    assert(false && "`write()` somehow returned zero."); // `write()` should never return zero, hmm. Only `read()` can return it, which indicates EOF.

                    // Fix up the results.
                    result = -1;
                    errno_copy = EAGAIN;
                }
            }

            // Error, or retry later, or writing to a broken pipe.
            if (result < 0)
            {
                // Need to retry later?
                if (
                    errno_copy == EAGAIN
                    #if EAGAIN != EWOULDBLOCK // It's unspecified if they're equal or not. `#if` just in case, to silence possible warnings.
                    || errno_copy == EWOULDBLOCK
                    #endif
                )
                {
                    if (out_result)
                        *out_result = IoResult::retry_later;
                    return false;
                }

                // Is this a write to a broken pipe?
                if constexpr (!Read)
                {
                    if (errno_copy == EPIPE)
                    {
                        if (out_result)
                            *out_result = IoResult::end_reached;
                        return false;
                    }
                }

                // Surely an error at this point.
                *this = {error_message, std::string(Read ? "`read()` failed: " : "`write()` failed: ") + std::strerror(errno_copy)};
                if (out_result)
                    *out_result = IoResult::error;
                return false;
            }

            // EOF.
            // We already ensured earlier that this is only possible if `Read == true`.
            if (result == 0)
            {
                if (out_result)
                    *out_result = IoResult::end_reached;
                return false;
            }

            // Success.
            // `result` is always positive at this point.
            assert(std::size_t(result) <= remaining_size);
            pos += std::size_t(result);

            if (out_result)
                *out_result = IoResult::ok;
            return true;

            #endif
        }
    };

    // Is an IO stream handle inheritable by child processes?
    enum class Inheritable : bool
    {
        // Never inherited. Must not be used as an IO stream redirection, nor passed to `MiscParams::inherit_handles`.
        no = false,

        // Inherited if used as an IO stream redirection, or if allowed by `MiscParams::inherit_handles`.
        //
        // Note that our default for inheriting handles differs from the default of the underlying OS APIs.
        // Our `MiscParams::inherit_handles` is set to inherit nothing by default, while the underlying APIs (if you call them directly or with another library) default to inherit all handles that have this flag enabled.
        always = true,

        // Inherited if used as an IO stream redirection. Must not be passed to `MiscParams::inherit_handles`. Unspecified if this gets inherited with `all_handles` or not (see OS-specific value below).
        as_stdio_stream =
        #ifdef _WIN32
            true, // Must be inheritable on Windows.
        #else
            false, // On POSIX, we end up passing it to `posix_spawn_file_actions_adddup2()`, which doesn't care about inheritability.
        #endif
    };

    // A base class for non-async IO streams.
    class BasicSyncIoStream : public BasicIoStream
    {
      public:
        using BasicIoStream::BasicIoStream;

        BasicSyncIoStream(BasicSyncIoStream &&) = default;
        BasicSyncIoStream &operator=(BasicSyncIoStream &&) = default;

        // Not protected, for the same reason as in `BasicIoStream`.
        ~BasicSyncIoStream() = default;

      public:
        // NOTE: Don't call this on streams (including ends of pipes) that you'll pass to child processes. You should only call this on streams that you'll use yourself.
        // Set whether this stream is blocking. I.e. when there is nothing more to read or write, should the read/write call block until it can continue, or return immediately.
        // Defaults to true.
        // Returns true on success, false on failure. On failure, resets this instance to null and stores the error on it, call `ErrorMessage()` to get it.
        bool SetBlocking(bool is_blocking)
        {
            if (StoreErrorIfNull())
                return false;

            #ifdef _WIN32

            DWORD new_mode = is_blocking ? 0 : PIPE_NOWAIT;

            // We don't try to preserve the existing settings (by calling `GetNamedPipeHandleState()`) since that's convoluted,
            //   and it seems there are no useful settings to preserve anyway. Especially on the handles we created ourselves.
            // SDL also doesn't attempt that.

            // This says "named pipe", but actually works fine on our unnamed pipes too. SDL does this too.
            // The docs even claim that anonymous pipes are internally implemented as named pipes, so they say that stuff like this tends to work.
            if (!SetNamedPipeHandleState(Handle(), &new_mode, nullptr, nullptr))
            {
                *this = {error_message, std::string("`SetNamedPipeHandleState()` failed to change `PIPE_NOWAIT`: ") + detail::WinApiErrorToString(GetLastError())};
                return false;
            }

            return true;

            #else

            // Note `F_{GET,SET}FL`, as opposed to `F_{GET,SET}FD`, which is a different thing.

            // Read existing flags.
            int flags = fcntl(Handle(), F_GETFL);
            if (flags == -1)
            {
                *this = {error_message, std::string("`fcntl(F_GETFL)` failed: ") + std::strerror(errno)};
                return false;
            }

            if (is_blocking)
                flags &= ~O_NONBLOCK;
            else
                flags |= O_NONBLOCK;

            // Write flags.
            if (fcntl(Handle(), F_SETFL, flags))
            {
                *this = {error_message, std::string("`fcntl(F_SETFL)` failed to change `O_NONBLOCK`: ") + std::strerror(errno)};
                return false;
            }

            return true;
            #endif
        }

        // Can this handle be inherited by a subprocess?
        // This is mainly for internal use. Prefer to set this directly when creating handles, all our functions should have parameters for that.
        //
        // Note that this can be racy if this was set to true and you change it to false, since while it was true, another thread could've created a subprocess that inherited this handle.
        // Returns true on success, false on failure. On failure, resets this instance to null and stores the error on it, call `ErrorMessage()` to get it.
        //
        // In the underlying OS APIs, on Windows this defaults to false, an on POSIX to true. But we override those defaults everywhere.
        bool SetInheritable(Inheritable inheritable)
        {
            if (StoreErrorIfNull())
                return false;

            #if _WIN32

            if (!SetHandleInformation(Handle(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT * bool(inheritable)))
            {
                *this = {error_message, std::string("`SetHandleInformation()` failed to change `HANDLE_FLAG_INHERIT`: ") + detail::WinApiErrorToString(GetLastError())};
                return false;
            }

            return true;

            #else
            // Note `F_{GET,SET}FD`, as opposed to `F_{GET,SET}FL`, which is a different thing.

            // Read existing flags.
            int flags = fcntl(Handle(), F_GETFD);
            if (flags == -1)
            {
                *this = {error_message, std::string("`fcntl(F_GETFD)` failed: ") + std::strerror(errno)};
                return false;
            }


            if (bool(inheritable))
                flags &= ~FD_CLOEXEC;
            else
                flags |= FD_CLOEXEC;

            // Write flags.
            if (fcntl(Handle(), F_SETFD, flags))
            {
                *this = {error_message, std::string("`fcntl(F_SETFD)` failed to change `FD_CLOEXEC`: ") + std::strerror(errno)};
                return false;
            }

            return true;
            #endif
        }
    };

    // An input stream. Normally either the read end of a pipe, or a file opened for reading.
    class InputStream : public BasicSyncIoStream
    {
        using BasicSyncIoStream::BasicSyncIoStream;

      public:
        // Tries to read some data, without any buffering.
        // You should probably set `pos` to zero initially.
        // Tries to fill `data` starting from `pos` and until the end. Increases `pos` to indicate how many bytes we were able to get.
        // If `out_result` is specified, writes status to it.
        // Returns true if the status is `ok` (regardless of `out_result` being null).
        // You can call this in a loop while it returns true if you prefer.
        bool ReadUnbuffered(std::span<char> data, std::size_t &pos, IoResult *out_result = nullptr)
        {
            return ReadOrWriteUnbuffered<true>(data, pos, out_result);
        }
    };

    // An output stream. Normally either the write end of a pipe, or a file opened for writing.
    class OutputStream : public BasicSyncIoStream
    {
        using BasicSyncIoStream::BasicSyncIoStream;

      public:
        // Tries to write some data, without any buffering.
        // You should probably set `pos` to zero initially.
        // Tries to write the part of `data` starting from `pos` and until the end. Increases `pos` to indicate how many bytes we were able to send.
        // If `out_result` is specified, writes status to it.
        // Returns true if the status is `ok` (regardless of `out_result` being null).
        // You can call this in a loop while it returns true if you prefer.
        bool WriteUnbuffered(std::span<char> data, std::size_t &pos, IoResult *out_result = nullptr)
        {
            return ReadOrWriteUnbuffered<true>(data, pos, out_result);
        }
    };

    // // A base class for async IO streams.
    // class BasicAsyncIoStream : public BasicIoStream
    // {
    //     using BasicIoStream::BasicIoStream;

    //     BasicAsyncIoStream(BasicAsyncIoStream &&) = default;
    //     BasicAsyncIoStream &operator=(BasicAsyncIoStream &&) = default;

    //     // Not protected, for the same reason as in `BasicIoStream`.
    //     ~BasicAsyncIoStream() = default;
    // };

    namespace detail
    {
        #ifdef _WIN32
        [[nodiscard]] inline HANDLE CurrentProcessHandle()
        {
            // My understanding is that this can never fail.
            // It should always return `(HANDLE)-1`.
            static const HANDLE ret = GetCurrentProcess();
            return ret;
        }
        #endif

        inline bool CloneStreamLow(const BasicIoStream &from, BasicIoStream &to, Inheritable inheritable)
        {
            // Clear the output first.
            to = {};

            if (from.HasError())
            {
                // Just copy the error message.
                to = {error_message, from.ErrorMessage()};
                return false; // This is an error.
            }

            if (!from)
                return true; // Not an error.

            #ifdef _WIN32

            BasicIoStream::handle_t new_handle = BasicIoStream::invalid_handle;
            if (!DuplicateHandle(CurrentProcessHandle(), from.Handle(), CurrentProcessHandle(), &new_handle, 0, bool(inheritable), DUPLICATE_SAME_ACCESS))
            {
                to = {error_message, std::string("`DuplicateHandle()` failed: ") + detail::WinApiErrorToString(GetLastError())};
                return false;
            }
            to = {take_ownership, new_handle};
            return true;

            #else

            // Here `fcntl(x, F_DUPFD_CLOEXEC, y)` is the equivalent of `dup3(x, ??, O_CLOEXEC)` that doesn't require you to manually specify the new handle.
            // And also `F_DUPFD_CLOEXEC` doesn't require MacOS 27+, unlike `dup3()`.

            // Note that `dup()`/`dup2()`/`dup3()` ignore the existing `O_CLOEXEC` flag on the original handle, if any.

            BasicIoStream::handle_t new_handle = bool(inheritable) ? dup(from.Handle()) : fcntl(from.Handle(), F_DUPFD_CLOEXEC, 0);
            if (new_handle < 0) // Both `dup()` and `fcntl()` should return -1 on failure.
            {
                to = {error_message, std::string(bool(inheritable) ? "`dup()` failed: " : "`fcntl(F_DUPFD_CLOEXEC)` failed: ") + std::strerror(errno)};
                return false;
            }
            to = {take_ownership, new_handle};
            return true;
            #endif
        }
    }

    template <typename T>
    concept SyncIoStreamType = std::same_as<T, InputStream> || std::same_as<T, OutputStream>;

    // Clones a stream, by cloning the underlying handle.
    // Primarily for internal use, provided here as a courtesy. You shouldn't need this.
    // Returns true on success. On failure writes an error message to `to`. Either way, the existing contents of `to` are destroyed.
    // If `from` is null, sets `to` to null as well. That's no an error.
    // One notable use of this is that on Windows, we set the "inheritable" flag for the copy even if the source doesn't have it.
    template <SyncIoStreamType T>
    bool CloneStream(const T &from, T &to, Inheritable inheritable)
    {
        return detail::CloneStreamLow(from, to, inheritable);
    }

    // Creates a pipe, assigning the two ends to `read` and `write`, which should be initially empty. If they aren't empty, their existing values are discarded.
    // On success, returns true and makes both `read` and `write` non-null.
    // On failure, returns false, makes both `read` and `write` null, and stores the error message in both `read` and `write`.
    inline bool MakePipe(InputStream &read, OutputStream &write, Inheritable read_inheritable, Inheritable write_inheritable)
    {
        // Firstly, destroy existing handles, if any.
        read = {};
        write = {};

        #ifdef _WIN32
        BasicIoStream::handle_t read_handle = BasicIoStream::invalid_handle;
        BasicIoStream::handle_t write_handle = BasicIoStream::invalid_handle;

        SECURITY_ATTRIBUTES attrs{};
        attrs.nLength = sizeof(attrs);
        attrs.bInheritHandle = true; // Note that this only ends up being used if `read_inheritable` or `write_inheritable` are true for the respective handles.

        // Note that on failure, `CreatePipe()` isn't guaranteed to leave the output parameters unchanged. So even if we allowed obtaining a pointer to the underlying handle from `BasicIoStream`,
        //   we still wouldn't be able to pass that here directly, unless we also called `ReleaseHandle_Unsafe()` on failure.

        if (CreatePipe(&read_handle, &write_handle, bool(read_inheritable) && bool(write_inheritable) ? &attrs : nullptr, 0)) // `0` is the buffer size. Passing zero leaves it defaulted.
        {
            // Success.
            read = {take_ownership, read_handle};
            write = {take_ownership, write_handle};
        }
        else
        {
            // Failure.
            read = {error_message, detail::WinApiErrorToString(GetLastError())};
            write = {error_message, read.ErrorMessage()};
            return false;
        }

        #else
        BasicIoStream::handle_t read_write_handles[2] = {BasicIoStream::invalid_handle, BasicIoStream::invalid_handle};

        bool pipe2_missing = false;

        if (
            DETAIL_EM_PROC_DISPATCH_PIPE2(
                // Note `O_CLOEXEC` rather than `FD_CLOEXEC`. The latter is for `fcntl()` only.
                // We can't pass `O_NONBLOCK` here, since we don't always need it, and when we do, it's only for one end of the pipe.
                //
                // We enable `O_CLOEXEC` if at least one half wants it. We then disable it for the other half if needed. This way there's no potential race for a subprocess in another thread to inherit this handle.
                pipe2(read_write_handles, O_CLOEXEC * (!bool(read_inheritable) || !bool(write_inheritable)))
            ,
                (pipe2_missing = true, pipe(read_write_handles))
            )
        )
        {
            // Failure.
            read = {error_message, std::string("`pipe()` failed: ") + std::strerror(errno)};
            write = {error_message, read.ErrorMessage()};
            return false;
        }
        else
        {
            // Pipe created successfully.
            read = {take_ownership, read_write_handles[0]};
            write = {take_ownership, read_write_handles[1]};
        }
        #endif

        // Adjust individual inheritability if needed.
        if (
            bool(read_inheritable) != bool(write_inheritable)
            #ifndef _WIN32
            || pipe2_missing
            #endif
        )
        {
            if (!bool(read_inheritable))
            {
                read.SetInheritable(Inheritable::no);
                if (read.HasError())
                {
                    // Copy the error message to the other handle.
                    write = {error_message, read.ErrorMessage()};
                    return false;
                }
            }

            if (!bool(write_inheritable))
            {
                write.SetInheritable(Inheritable::no);
                if (write.HasError())
                {
                    // Copy the error message to the other handle.
                    read = {error_message, write.ErrorMessage()};
                    return false;
                }
            }
        }

        return true;
    }


    // What to do if we're trying to open a file and it already exists?
    enum class ExistingFile
    {
        // Open the file but discard the old contents.
        overwrite,
        // Open the file and keep the contents. Write operations will append to the end.
        keep,
        // Refuse to open an existing the file.
        error,
    };

    // Additional settings for opening output files.
    enum class OutputFileFlags
    {
        #ifndef _WIN32
        // If specified, and `allow_creating == true`, the created file will have custom permissions, instead of our default 0644 (owner can read and write, group and others can only read).
        // In addition to this flag, you must also store the desired permissions in this enum, as the lowest 12 bits.
        // Note that `open()` automatically bitwise-subtracts something called `umask` from those bits, which is a per-process setting that defaults to 0022.
        //   So if you want to set those bits (write permissions for group and others), you will need to disable them in the umask first, using `umask()` function.
        custom_permissions_posix = 1 << 12,

        // The lowest 12 bits of this enum are reserved for POSIX permission flags, see `custom_permissions_posix` above.
        _custom_permissions_mask_posix = (1 << 12) - 12,
        #endif

        // Don't pass the append flag to the file.
        // For `ExistingFile::keep`, this will cause writes to the file to overwrite data starting from the beginning, as opposed to appending at the end.
        // For `ExistingFile::overwrite` this shouldn't make a difference, unless perhaps several things write to the same file? Didn't test.
        no_append = 1 << 13,
    };
    DETAIL_EM_PROC_FLAG_ENUM(OutputFileFlags)

    // This mirrors the parameter list of `OpenOutputFile()` for delayed calls, see below.
    struct OutputFileDesc
    {
        // The default values here matter, they should match what `OpenOutputFile()` defaults to.

        MaybeOwningNativeString filename;
        ExistingFile existing = ExistingFile::overwrite;
        bool allow_creating = true;
        OutputFileFlags flags = {};

        #ifndef _WIN32
        // Those don't need Windows variants, because Windows doesn't have the equivalent of `posix_spawn_file_actions_addopen()`,
        //   so we can always do everything in `OpenFile()`.

        // Mainly for internal use. Returns the flags for `open()` corresponding to this struct.
        // This carries information from all the fields, except `filename`.
        // There's no equivalent for input files. Those should just pass `O_RDONLY` as flags, and `0` as file permissions (since the permissions are unused if `O_CREAT` is not specified).
        // Also don't forget to conditionally add `O_CLOEXEC` to those flags (for input and output files).
        [[nodiscard]] int ToOpenFlags_Posix() const
        {
            return
                O_WRONLY |
                (O_APPEND * !bool(flags & OutputFileFlags::no_append)) |
                (O_CREAT * allow_creating) |
                (existing == ExistingFile::keep ? 0 : existing == ExistingFile::overwrite ? O_TRUNC : O_EXCL);
        }

        // Mainly for internal use. Returns the file permission bits for `open()` corresponding to this struct.
        // This only uses `flags` from this struct.
        // `mode_t` is `unsigned int`. That's what `posix_spawn_file_actions_addopen()` takes.
        [[nodiscard]] mode_t ToOpenPermissions_Posix() const
        {
            constexpr mode_t default_permissions = 0644; // Sync this default value with the comment on `OutputFileFlags::custom_permissions_posix`.
            return bool(flags & OutputFileFlags::custom_permissions_posix) ? mode_t(flags & OutputFileFlags::_custom_permissions_mask_posix) : default_permissions;
        }
        #endif
    };


    // Inherit an IO stream from the parent process instead of attaching a custom one. This is the default behavior.
    struct TagInherit {explicit TagInherit() = default;};
    inline constexpr TagInherit inherit{};

    // Merge one output stream into the other. Usually you put this on `stderr` to merge it into `stdout`, and then attach a custom stream to `stdout`.
    struct TagMergeOutput {explicit TagMergeOutput() = default;};
    inline constexpr TagMergeOutput merge_output{};

    // What to do with the input stream of a process.
    // `InputStream` is intentionally listed first, to make `= {}` submit a null stream.
    using InputStreamAttachment = std::variant<InputStream, TagInherit, MaybeOwningNativeString>;

    // What to do with one of the output streams of a process.
    // `OutputStream` is intentionally listed first, to make `= {}` submit a null stream.
    // Listing `MaybeOwningNativeString` for convenience. It's equivalent to passing the filename to `OutputFileDesc`.
    using OutputStreamAttachment = std::variant<OutputStream, TagInherit, TagMergeOutput, MaybeOwningNativeString, OutputFileDesc>;


    // Convenience variants of `MakePipe()` for when one or both ends are IO redirects.
    // Returns true on success.
    [[nodiscard]] inline bool MakePipe(InputStreamAttachment &read, OutputStream &write, Inheritable write_inheritable = Inheritable::no)
    {
        return MakePipe(read.emplace<InputStream>(), write, Inheritable::as_stdio_stream, write_inheritable);
    }
    [[nodiscard]] inline bool MakePipe(InputStream &read, OutputStreamAttachment &write, Inheritable read_inheritable = Inheritable::no)
    {
        return MakePipe(read, write.emplace<OutputStream>(), read_inheritable, Inheritable::as_stdio_stream);
    }
    [[nodiscard]] inline bool MakePipe(InputStreamAttachment &read, OutputStreamAttachment &write)
    {
        return MakePipe(read.emplace<InputStream>(), write.emplace<OutputStream>(), Inheritable::as_stdio_stream, Inheritable::as_stdio_stream);
    }



    using IoStreamRefVar = std::variant<std::reference_wrapper<InputStream>, std::reference_wrapper<OutputStream>>;

    // This is a low-level function combining the effects of `OpenInputFile()` and `OpenOutputFile()`. Prefer one of those, and see them for more information.
    // Opens a file and assigns it to `target`. `target` should initially be empty. If it isn't, its existing value is discarded.
    // Returns true on success. Then makes `target` non-null.
    // On failure returns false, and makes `target` null, and stores the error message in `target`.
    // For input streams, it's also an error to pass `existing != keep`.
    // NOTE: `allow_creating` doesn't have a default value here, since input streams are required to pass false, while output streams will usually pass true.
    //   This is also why it's placed before other parameters with default arguments.
    inline bool OpenFile(IoStreamRefVar target, NativeCStringViewParam filename, Inheritable inheritable, bool allow_creating, ExistingFile existing = ExistingFile::overwrite, OutputFileFlags flags = {})
    {
        BasicIoStream &target_base = std::visit([](auto &elem) -> BasicIoStream & {return elem;}, target);

        // In any case, reset the target first.
        // This also throws if it was `valueless_by_exception()`.
        target_base = {};

        // Validate `existing` enum.
        bool existing_mode_valid = existing == ExistingFile::overwrite || existing == ExistingFile::keep || existing == ExistingFile::error;
        assert(existing_mode_valid);
        if (!existing_mode_valid)
        {
            target_base = {error_message, "Invalid `ExistingFile` enum value."};
            return false;
        }

        // Perform stream-type-specific validation on the parameters.
        if (!std::visit(detail::Overload{
            [&](const std::reference_wrapper<InputStream> &elem)
            {
                // Input streams only allow one specific set of parameters.
                if (allow_creating || existing != ExistingFile::keep || flags != OutputFileFlags{})
                {
                    elem.get() = {error_message, "Input files require `allow_creating == false && existing == keep && flags == 0`."};
                    return false;
                }
                return true;
            },
            [&](const std::reference_wrapper<OutputStream> &elem)
            {
                // This specific combination would never allow the file to be opened, so we error on it ourselves.
                if (!allow_creating && existing == ExistingFile::error)
                {
                    elem.get() = {error_message, "Illegal mode combination for a file: `allow_creating == false && existing == error`."};
                    return false;
                }
                return true;
            },
        }, target))
        {
            return false;
        }

        #ifdef _WIN32

        // Disable some weird error messageboxes. Here I'm copying what SDL does, except that I'm setting it only once and permanently, since it's a process-wide setting.
        [[maybe_unused]] static const std::nullptr_t disable_error_messageboxes_once = []{
            if (GlobalSettings().disable_error_messageboxes_win.Consume())
                SetErrorMode(
                    GetErrorMode() |
                    // This seems reasonable to me.
                    SEM_FAILCRITICALERRORS |
                    // This I'm not sure about. SDL uses it, but it should only affect `OpenFile()`, not `CreateFile()`.
                    SEM_NOOPENFILEERRORBOX
                );
            return nullptr;
        }();

        SECURITY_ATTRIBUTES sec_attrs{};
        sec_attrs.nLength = sizeof sec_attrs;
        sec_attrs.bInheritHandle = true; // Allow inheriting this, since we'll normally use those files to redirect IO.

        HANDLE handle = CreateFileW(
            filename.GetPointer(),
            // Access mode.
            std::visit(detail::Overload{
                // Those are a bit moot. For reads and non-append writes we could alternatively use `GENERIC_READ` and `GENERIC_WRITE`,
                //   but it's unclear to me what's the append equivalent of `GENERIC_WRITE`. Since apparently `GENERIC_WRITE` is a lone bit (as opposed to a mask including `FILE_WRITE_DATA`),
                //   I can't just modify one bit in it. I could go to the docs and find what `GENERIC_WRITE` resolves to and paste that here, but eh.
                //   I don't think we need those extra permissions for anything anyway (at least if Claude is to be trusted, and it's not like we're doing anything advanced with the files).
                [](const InputStream &) -> DWORD {return FILE_READ_DATA;},
                [&](const OutputStream &) -> DWORD {return bool(flags & OutputFileFlags::no_append) ? FILE_WRITE_DATA : FILE_APPEND_DATA;},
            }, target),
            // What shared access to the same file to allow.
            std::visit(detail::Overload{
                [](const InputStream &) -> DWORD {return FILE_SHARE_READ;}, // Others can also read. Should we also allow writes here? Probably not.
                [&](const OutputStream &) -> DWORD {return bool(flags & OutputFileFlags::no_append) ? 0 : FILE_SHARE_WRITE;}, // If we're appending, I guess allow concurrent writes to also append?
            }, target),
            bool(inheritable) ? &sec_attrs : nullptr,
            allow_creating
                ? (existing == ExistingFile::keep ? OPEN_ALWAYS : existing == ExistingFile::error ? CREATE_NEW : CREATE_ALWAYS)
                : (existing == ExistingFile::keep ? OPEN_EXISTING : TRUNCATE_EXISTING),
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (handle == INVALID_HANDLE_VALUE)
        {
            // Failed to open.
            target_base = {error_message, std::string("`CreateFileW()` failed: ") + detail::WinApiErrorToString(GetLastError())};
            return false;
        }

        // Success.
        target_base = {take_ownership, handle};
        return true;

        #else

        OutputFileDesc output_file_desc;
        if (std::holds_alternative<std::reference_wrapper<OutputStream>>(target))
            output_file_desc = {.filename = {}/*Not needed to get the flags.*/, .existing = existing, .allow_creating = allow_creating, .flags = flags};

        int handle = open(
            filename.GetPointer(),
            // Here always pass append. Firstly it's simpler than manually seeking to the end of file, and secondly I hope it'll give better behavior if multiple processes open the same file (if that's legal in the first place?).
            std::visit(detail::Overload{
                [](const InputStream &)
                {
                    return O_RDONLY;
                },
                [&](const OutputStream &)
                {
                    return output_file_desc.ToOpenFlags_Posix();
                }
            }, target) | (!bool(inheritable) * O_CLOEXEC),
            // This `...` parameter is unused if `O_CREAT` is not passed, but it's easier to pass unconditionally.
            // Calling it when `output_file_desc` is default-constructed (so for input streams) should be fine too.
            output_file_desc.ToOpenPermissions_Posix()
        );

        if (handle < 0)
        {
            // Failed to open.
            target_base = {error_message, std::string("`open()` failed: ") + std::strerror(errno)};
            return false;
        }

        // Success.
        target_base = {take_ownership, handle};
        return true;
        #endif
    }

    // Opens a file for reading. Returns a null instance with an error stored in it on failure, call `.ErrorMessage()` for details.
    // Note that this function isn't very useful for redirecting process input, since that API also accepts a path direclty, and that's more optimal. This function exists only for completeness.
    [[nodiscard]] inline InputStream OpenInputFile(NativeCStringViewParam filename, Inheritable inheritable)
    {
        InputStream ret;
        OpenFile(ret, std::move(filename), inheritable, false, ExistingFile::keep);
        return ret;
    }

    // Opens a file for writing. Returns a null instance with an error stored in it on failure, call `.ErrorMessage()` for details.
    // Note that this function isn't very useful for redirecting process output, since that API also accepts a path direclty, and that's more optimal. This function exists only for completeness.
    // `existing` controls what happens if such file already exists. `keep` means it's opened, `trucate` means its opened but the existing contents are destroyed, `error` means this function fails.
    // `allow_creating` controls what happens if no such file exists. `true` means it's created, `false` means this function fails.
    // It's an error to pass `!allow_creating && existing == error`.
    // NOTE: Here the parameter order is different than in `OpenFile()`, because this one makes more sense, and we're not constrained by having to provide decent API for input files too.
    [[nodiscard]] inline OutputStream OpenOutputFile(NativeCStringViewParam filename, Inheritable inheritable, ExistingFile existing = ExistingFile::overwrite, bool allow_creating = true, OutputFileFlags flags = {})
    {
        OutputStream ret;
        OpenFile(ret, std::move(filename), inheritable, allow_creating, existing, flags);
        return ret;
    }
    [[nodiscard]] inline OutputStream OpenOutputFile(const OutputFileDesc &desc, Inheritable inheritable)
    {
        OutputStream ret;
        OpenFile(ret, std::move(desc.filename), inheritable, desc.allow_creating, desc.existing, desc.flags);
        return ret;
    }

    namespace detail
    {
        #ifdef _WIN32
        // Combines multiple command-line arguments into one string. Can operate either on wide or on narrow strings, doesn't matter.
        // `executable` is the optional override for the executable name, that's otherwise taken from the first argument. We may modify it, and may reset it to null if needed. (Currently only resetting.)
        // `executable` is not an `std::optional<std::basic_string_view<...>>` for convenience, since we always pass an owning optional string to it.
        // `executable` has to store a either a `detail::MaybeOwningMaybeMutString<...>` or `MaybeOwningNativeString` which is derived from it.
        // `num_args` is the number of arguments, including the program name. Passing 0 is legal and means you don't want to specify this.
        // `get_arg(i)` is called for `0 <= i < num_args`, and must return something convertible to `std::basic_string_view<Char>`, matching the character type of `executable`.
        // `get_arg` can be called multiple times for the same index, make sure that works and is fast.
        // If `batch_prepend_cmd == true`, we will prepend `cmd ... /c` to batch files, which is usually a good thing.
        // `batch_safety` is either 0 or 1 or 2:
        //   `0` allows unsafe characters in arguments without escaping them (`%` and `!`).
        //   `1` tries to escape them when possible (which can still break on some batch files).
        //   `2` bans unsafe characters.
        // The escaping algorithm is explained in: https://holyblackcat.github.io/blog/2026/09/05/escaping-createprocess-arguments.html
        //   That also explains those two knobs in more details.
        // `out_command` is the resulting combined string, or null if we think none is needed.
        // `out_error` is set to error on failure.
        // Returns true on success and false on failure. On success, `out_error` is left unchanged. On failure, `out_command` may be unmodified.
        // If this function fails, it writes the error message to `out_error`.
        template <typename Char>
        [[nodiscard]] bool AssembleCommandLine(
            std::optional<MaybeOwningMaybeMutString<const Char>> &executable,
            std::size_t num_args,
            bool batch_prepend_cmd,
            int batch_safety,
            std::optional<MaybeOwningMaybeMutString<Char>> &out_command,
            std::string &out_error,
            auto &&get_arg)
        {

            // This lambda returns the `i`th argument converted to `std::basic_string_view<...>`.
            auto GetArgView = [&](std::size_t i)
            {
                using LambdaReturnType = decltype(get_arg(i));

                if constexpr (is_MaybeOwningMaybeMutString<std::remove_cvref_t<LambdaReturnType>>)
                    return get_arg(i).GetStringView();
                else
                    return std::basic_string_view(get_arg(i));
            };

            // Now we can check the character type of the input arguments.
            static_assert(std::is_same_v<typename decltype(GetArgView(0))::value_type, Char>);


            // 1. Check for bad inputs:

            // 1.1. Check that at least one parameter is specified:
            if (!executable && num_args == 0)
            {
                out_error = "Must specify either an executable or a command.";
                return false;
            }

            auto GetExecutableView = [&]
            {
                return executable->GetStringView();
            };

            // 1.2. Check for null characters.
            if (executable && GetExecutableView().find('\0') != std::size_t(-1))
            {
                out_error = "Null character in the executable name.";
                return false;
            }
            for (std::size_t i = 0; i < num_args; i++)
            {
                if (GetArgView(i).find('\0') != std::size_t(-1))
                {
                    out_error = "Null character in the command.";
                    return false;
                }
            }

            // 2. Determine executable name.
            std::basic_string_view<Char> exe_name = executable ? GetExecutableView() : GetArgView(0);

            // 3. Check that the executable name doesn't end with garbage.
            if (exe_name.ends_with(' ') || exe_name.ends_with('.'))
            {
                out_error = "The program name can't end with a space or a dot.";
                return false;
            }


            // Checks if `object` equals to `target_lowercase`, case-insensitive.
            // `target_lowercase` must be in lowercase for this function to work correctly.
            // Using `initializer_list` for convenience, because we can't pass string literals here, because `Char` can vary. Braced lists of characters work fine though.
            auto EqualsCaseInsensitive = [](std::basic_string_view<Char> object, std::initializer_list<Char> target_lowercase) -> bool
            {
                if (object.size() != target_lowercase.size())
                    return false;

                for (std::size_t i = 0; i < object.size(); i++)
                {
                    Char obj_ch = object[i];
                    Char tgt_ch = target_lowercase.begin()[i];
                    if (!(
                        obj_ch == tgt_ch ||
                        (tgt_ch >= 'a' && tgt_ch <= 'z' && obj_ch == tgt_ch - 'a' + 'A')
                    ))
                    {
                        return false;
                    }
                }

                return true;
            };
            auto StartsWithCaseInsensitive = [&](std::basic_string_view<Char> object, std::initializer_list<Char> target_lowercase) -> bool
            {
                if (object.size() < target_lowercase.size())
                    return false;

                return EqualsCaseInsensitive(object.substr(0, target_lowercase.size()), target_lowercase);
            };
            auto EndsWithCaseInsensitive = [&](std::basic_string_view<Char> object, std::initializer_list<Char> target_lowercase) -> bool
            {
                if (object.size() < target_lowercase.size())
                    return false;

                return EqualsCaseInsensitive(object.substr(object.size() - target_lowercase.size()), target_lowercase);
            };

            // 4. Check for a direct CMD invocation.
            // Note `!executable`. We only want to check those if the name comes from `argv[0]`, since `executable` doesn't respect PATH and `cmd` and `cmd.exe` need that.
            bool is_cmd = !executable && (EqualsCaseInsensitive(exe_name, {'c','m','d'}) || EqualsCaseInsensitive(exe_name, {'c','m','d','.','e','x','e'}));

            // 5. Check for batch.
            bool is_batch = EndsWithCaseInsensitive(exe_name, {'.','b','a','t'}) || EndsWithCaseInsensitive(exe_name, {'.','c','m','d'});


            bool seen_d = false;
            std::optional<bool> flag_e;
            std::optional<bool> flag_v;
            bool seen_s = false;
            bool seen_c_or_k = false;

            // If the argument is bad, writes to `out_error` and returns true.
            auto ArgContainsBadChars = [&](std::basic_string_view<Char> arg) -> bool
            {
                // This function only checks CMD and batch arguments.
                if (!is_cmd && !is_batch)
                    return false;

                // In CMD, this only kicks in after `/c`-or-`/k`.
                if (is_cmd && !seen_c_or_k)
                    return false;

                Char bad_chars_array[4] = {'\n', '\r'};
                std::size_t bad_chars_array_pos = 2;

                // Populate `bad_chars_array`.
                if (batch_safety > 0)
                {
                    // If `/v` is unknown or true.
                    if (!flag_v || *flag_v)
                        bad_chars_array[bad_chars_array_pos++] = '!';

                    // If `batch_safety >= 2`, or if `/e` is unknown or false.
                    if (batch_safety >= 2 || (!flag_e || !*flag_e))
                        bad_chars_array[bad_chars_array_pos++] = '%';
                }

                const std::basic_string_view bad_chars(bad_chars_array, bad_chars_array_pos);

                if (auto pos = arg.find_first_of(bad_chars); pos != std::size_t(-1))
                {
                    // Note that this says "command" and not "argument", since this function applies to batch filenames too.
                    out_error = "Bad character in cmd/batch command: ";
                    char bad_char = char(arg[pos]); // The cast to `char` here is fine, because all possible bad characters are hardcoded above and are narrow.
                    if (bad_char == '\n')
                    {
                        out_error += "line break.";
                    }
                    else if (bad_char == '\r')
                    {
                        out_error += "carriage return.";
                    }
                    else
                    {
                        out_error += '`';
                        out_error += bad_char;
                        out_error += "`.";
                    }

                    return true;
                }

                return false;
            };

            // Don't read `exe_name` beyond this point, it might get invalidated.

            out_command = {};

            auto GetWritableOutCommand = [&]() -> auto &
            {
                assert(out_command);
                return out_command->GetOwnedString();
            };

            bool need_separator_before_next_arg = false;
            bool is_first_arg = true;
            bool need_final_closing_quote = false; // If this is true, we need a `"` at the end of the command.

            // This is in a lambda because we can update `executable` and `is_batch` below.
            auto ShouldQuoteEntireCommand = [&]{return executable && num_args != 0 && is_batch;};

            // Appends a single argument to the output. Assumes `out_command` isn't null, ensure that before calling this.
            // Returns true on failure, then you should exit this function too.
            auto WriteArgument = [&](std::basic_string_view<Char> arg) -> bool
            {
                auto &out = GetWritableOutCommand();

                while (true)
                {
                    // If this isn't empty at the end of the function, we replace `arg` with it, and do another iteration.
                    // We use this to split `/cfoo` into `/c foo`.
                    std::basic_string_view<Char> restart_with_arg;


                    // 8.3.1. Does this argument need CMD-specific handling?
                    bool arg_needs_cmd_handling = (is_cmd && seen_c_or_k) || is_batch;

                    // 8.3.2. CMD-specific validation, if needed.
                    // `ArgContainsBadChars()` embeds all the necessary conditions, so we can just call it.
                    if (ArgContainsBadChars(arg))
                        return true;

                    // 8.3.3. Track special arguments.
                    bool this_arg_is_c_or_k = false;
                    if (is_cmd && !is_first_arg && !seen_c_or_k)
                    {
                        if (arg.starts_with('/'))
                        {
                            // Note, using "starts with" for all arguments, and not checking `:` for arguments that take parameters. See the blog post for more details.

                            auto remainder = arg.substr(1);
                            if (StartsWithCaseInsensitive(remainder, {'c'}) || StartsWithCaseInsensitive(remainder, {'k'}))
                            {
                                seen_c_or_k = true;
                                this_arg_is_c_or_k = true;

                                // If the result happens to be empty, `restart_with_arg` is ignored, which is exactly what we want.
                                restart_with_arg = remainder.substr(1);

                                // Trim the part after `/c`-or-`/k`, which is what went into `restart_with_arg`.
                                arg = {arg.data(), 2};
                            }
                            else if (!seen_s && StartsWithCaseInsensitive(remainder, {'s'})) // This one is mandatory and is not guarded by `batch_prepend_cmd`.
                            {
                                seen_s = true;
                            }
                            // We care about this if we'd add our own `/d `if it's missing (`batch_prepend_cmd == true`).
                            else if (batch_prepend_cmd && !seen_d && StartsWithCaseInsensitive(remainder, {'d'}))
                            {
                                seen_d = true;
                            }
                            // We care about this if we'd add our own `/e `if it's missing (`batch_prepend_cmd == true`), or if we're doing `%` escaping (`batch_safety == 1`, at `2` it's not allowed, and at `0` it's not escaped).
                            else if ((batch_prepend_cmd || batch_safety == 1) && !flag_e && StartsWithCaseInsensitive(remainder, {'e'}))
                            {
                                // This weird check is exactly what CMD seems to be doing.
                                remainder.remove_prefix(1);
                                if (batch_safety <= 0)
                                    flag_e = true; // Don't care about the value, just need to engage the optional.
                                else
                                    flag_e = !StartsWithCaseInsensitive(remainder, {':','o','f','f'});
                            }
                            // We care about this if we'd add our own `/v `if it's missing (`batch_prepend_cmd == true`), or if we're checking for `!` with special meaning in the arguments (`batch_safety >= 1`).
                            else if ((batch_prepend_cmd || batch_safety >= 1) && !flag_v && StartsWithCaseInsensitive(remainder, {'v'}))
                            {
                                // This weird check is exactly what CMD seems to be doing.
                                remainder.remove_prefix(1);
                                if (batch_safety <= 0)
                                    flag_v = true; // Don't care about the value, just need to engage the optional.
                                else
                                    flag_v = !StartsWithCaseInsensitive(remainder, {':','o','f','f'});
                            }
                        }
                    }

                    // 8.3.4. If this is `/c` or `/k`, insert the extra arguments before it if the user didn't provide them.
                    if (this_arg_is_c_or_k)
                    {
                        if (batch_prepend_cmd) // Reuse this knob for this.
                        {
                            if (!seen_d)
                                out += std::basic_string_view(std::to_array<Char>({' ','/','d'}));

                            if (!flag_e)
                            {
                                out += std::basic_string_view(std::to_array<Char>({' ','/','e',':','o','n'}));
                                flag_e = true; // Note that `flag_e` is `std::optional<bool>`.
                            }

                            if (!flag_v)
                            {
                                out += std::basic_string_view(std::to_array<Char>({' ','/','v',':','o','f','f'}));
                                flag_v = false; // Note that `flag_v` is `std::optional<bool>`.
                            }
                        }

                        // This one is mandatory.
                        if (!seen_s)
                            out += std::basic_string_view(std::to_array<Char>({' ','/','s'}));
                    }

                    // 8.3.5. The separating space.
                    if (std::exchange(need_separator_before_next_arg, true))
                        out += ' ';

                    // 8.3.6. Should we quote this argument?
                    bool quote = false;
                    if (arg.empty() || (is_first_arg && (!executable || ShouldQuoteEntireCommand())))
                    {
                        quote = true;
                    }
                    else
                    {
                        static constexpr Char quotable_chars_array[] = {' ','\t','"', /*batch only:*/ '<','>','&','|','(',')','[',']','{','}','^','=',';','%','!','\'','+','`','~'};
                        // Need to pass `std::extent_v<...>`, or would have to add a null terminator.
                        const std::basic_string_view<Char> quotable_chars = arg_needs_cmd_handling ? std::basic_string_view<Char>(quotable_chars_array, std::extent_v<decltype(quotable_chars_array)>) : std::basic_string_view<Char>(quotable_chars_array, 3);

                        if (arg.find_first_of(quotable_chars) != std::size_t(-1))
                            quote = true;
                    }

                    // 8.3.7. Opening quote.
                    if (quote)
                        out += '"';

                    // 8.3.8. Write the escaped contents.
                    std::size_t arg_size = arg.size();
                    for (std::size_t i = 0; i < arg_size; i++)
                    {
                        Char ch = arg[i];

                        if (ch == '"')
                        {
                            if constexpr (std::is_same_v<Char, wchar_t>)
                                out += L"\"\"";
                            else
                                out +=  "\"\"";
                            continue;
                        }

                        // Escaping `%`.
                        if ((is_cmd && seen_c_or_k) || is_batch)
                        {
                            assert(ch != '%' || batch_safety <= 1); // Should've rejected it at 2.
                            // At `batch_safety == 0` we'll write `%` unescaped below.
                            // Using `>=` instead of `==` here just in case. `2` should be unreachable.
                            if (ch == '%' && batch_safety >= 1)
                            {
                                if constexpr (std::is_same_v<Char, wchar_t>)
                                    out += L"%%cd:~,%";
                                else
                                    out +=  "%%cd:~,%";

                                continue;
                            }
                        }

                        if (ch == '\\')
                        {
                            std::size_t num_backslashes = 1;
                            while (i + 1 < arg_size && arg[i+1] == '\\')
                            {
                                i++;
                                num_backslashes++;
                            }

                            // Is the next character is a quote? (Either natural or our own.)
                            // Then double the amount of backslashes.
                            if (i + 1 < arg_size ? arg[i+1] == '"' : quote)
                                num_backslashes *= 2;

                            for (std::size_t j = 0; j < num_backslashes; j++)
                                out += '\\';

                            continue;
                        }

                        out += ch;
                    }

                    // 8.3.9. Closing quote.
                    if (quote)
                        out += '"';

                    // 8.3.10. Insert the opening quote after `/c` or `/k` if needed.
                    if (this_arg_is_c_or_k)
                    {
                        need_separator_before_next_arg = false;
                        need_final_closing_quote = true;
                        out += ' ';
                        out += '"';
                    }

                    is_first_arg = false;


                    // Finally, do another iteration with a new argument if needed.
                    if (restart_with_arg.empty())
                        break;
                    arg = restart_with_arg;
                }

                return false;
            };

            // 6. Prepend CMD invocation to Batch files.
            if (batch_prepend_cmd && is_batch)
            {
                // Just handroll all of those for speed.
                seen_d = true;
                flag_e = true;
                flag_v = false;
                seen_s = true;
                seen_c_or_k = true;

                is_first_arg = false;
                need_final_closing_quote = true;

                assert(!out_command);
                if constexpr (std::is_same_v<Char, wchar_t>)
                    out_command = L"\"cmd\" /d /e:on /v:off /s /c \"";
                else
                    out_command =  "\"cmd\" /d /e:on /v:off /s /c \"";

                // If we have no user-provided arguments, write the executable as an argument.
                // I figured it's easier to use `WriteArgument()` here.
                if (num_args == 0)
                {
                    if (WriteArgument(GetExecutableView()))
                        return false;
                }

                // Either way, reset `executable`.
                executable = {};

                is_cmd = true;
                is_batch = false;
            }

            // 7. Batch/cmd name validation.
            // The `&& executable` may look redundant, but we could've appended some extra arguments on step 6 (which don't count against `num_args`) and reset `executable`,
            //   so it's possible that both `num_args == 0` and `!executable`.
            if (num_args == 0 && executable && ArgContainsBadChars(GetExecutableView()))
                return false;

            // 8. Assemble the command:

            // If by this point `out_command` is still null, make it non-null if necessary.
            if (num_args != 0 && !out_command)
                out_command.emplace();

            // 8.1. Quote the entire command if needed.
            if (ShouldQuoteEntireCommand())
            {
                GetWritableOutCommand() += '"';

                assert(!need_final_closing_quote);
                need_final_closing_quote = true;
            }

            // 8.2. Needs no code.

            // 8.3. Actually write the arguments.
            for (std::size_t i = 0; i < num_args; i++)
            {
                if (WriteArgument(GetArgView(i)))
                    return false;
            }

            // 8.4. Lastly, close the CMD quote if needed.
            if (need_final_closing_quote)
                GetWritableOutCommand() += '"';

            return true;
        }

        // Produces a windows-style environemtn string by concating `env[i]` into a `\0`-separated list, with `\0\0` at the end.
        // NOTE: Some places in this library bypass this function to generate the env string. This is unlike `AssembleCommandLine()`, which is used for all Windows command lines (that don't come as a single string).
        template <typename Char>
        [[nodiscard]] std::basic_string<Char> AssembleEnvironmentFromPtr(const Char *const *env)
        {
            if (!env)
                return {};

            // Not calculating the size in advance because I don't want to `strlen()` every element.
            std::basic_string<Char> ret;
            while (*env)
            {
                if (!**env)
                {
                    // If the string is empty, it would normally cause the rest of the strings to be ignored, since `\0\0` is the terminator.
                    // Instead we manually discard it here.
                    assert(false && "Empty string in an environment array.");
                    env++;
                    continue;
                }

                ret += *env++;
                ret += '\0'; // Intentional even after the last element. We want `\0\0` at the end.
            }

            return ret;
        }

        #endif


        #ifndef _WIN32

        // Environment:

        #ifndef __APPLE__
        extern "C" char **environ;
        #endif

        // This doesn't return `const char *const *` for convenience, but you probably shouldn't write to those pointers.
        [[nodiscard]] inline char **GetEnviron()
        {
            #ifdef __APPLE__
            // Use `*_NSGetEnviron()` instead of `environ`.
            // `environ` does kinda work on Macs, but `man environ` on Macs says that it doesn't work in shared libraries and in bundles (whatever those are), but this function works.
            // SDL uses this too.
            return *_NSGetEnviron();
            #else
            return environ;
            #endif
        }


        // The global `posix_spawnattr_t` instance.
        // We currently don't need to customize it per process.

        class PosixSpawnAttr : public StoresErrorMessage
        {
            struct State
            {
                #ifndef _WIN32
                bool spawn_attr_alive = false;
                posix_spawnattr_t spawn_attr{};
                #endif
            };
            State state;

          public:
            constexpr PosixSpawnAttr() {}

            PosixSpawnAttr(std::nullptr_t)
                : PosixSpawnAttr() // Run destructor on failure.
            {
                // Construct spawn attributes.
                if (int spawn_res = posix_spawnattr_init(&state.spawn_attr))
                {
                    // No useful messages for us to emit here, so just write the number.
                    // The manual doesn't mention this setting `errno`, so we use the return value instead. At least for `posix_spawn`, glibc sets the errno anyway, even though the manual doesn't say so, but for this function I can't check, because it never fails in glibc.
                    error_string = "`posix_spawnattr_init()` failed: " + std::to_string(spawn_res);
                    return;
                }
                state.spawn_attr_alive = true;

                { // Configure the signal mask for `POSIX_SPAWN_SETSIGMASK` below. See that for details.
                    sigset_t new_sigmask{};
                    if (sigemptyset(&new_sigmask))
                    {
                        error_string = std::string("`sigemptyset()` failed: ") + std::strerror(errno);
                        return;
                    }

                    // This seems to perform a deep copy, at least in glibc.
                    if (int error = posix_spawnattr_setsigmask(&state.spawn_attr, &new_sigmask))
                    {
                        error_string = std::string("`posix_spawnattr_setsigmask()` failed: ") + std::strerror(error);
                        return;
                    }
                }

                // Set flags.
                // `POSIX_SPAWN_SETSIGMASK` uses the mask we set with `posix_spawnattr_setsigmask` above.
                //   We need this for two reasons. Firstly, general sanity. Secondly, for detached background processes we temporarily disable signals before forking, and this restores them.
                // `POSIX_SPAWN_SETSIGDEF` resets the signal handling modes to the default values. Note that custom handlers seem to be detached automatically even without this.
                if (int error = posix_spawnattr_setflags(&state.spawn_attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF))
                {
                    error_string = std::string("`posix_spawnattr_setflags()` failed: ") + std::strerror(error);
                    return;
                }
            }

            [[nodiscard]] PosixSpawnAttr(PosixSpawnAttr &&other) noexcept
                : StoresErrorMessage(std::move(other)),
                state(std::move(other.state))
            {
                other.state = {};
            }

            PosixSpawnAttr &operator=(PosixSpawnAttr other) noexcept
            {
                std::swap(static_cast<StoresErrorMessage &>(*this), static_cast<StoresErrorMessage &>(other));
                std::swap(state, other.state);
                return *this;
            }

            ~PosixSpawnAttr()
            {
                if (state.spawn_attr_alive)
                    posix_spawnattr_destroy(&state.spawn_attr);
            }

            const posix_spawnattr_t &Underlying() const
            {
                assert(state.spawn_attr_alive && !HasError());
                return state.spawn_attr;
            }
        };

        // Returns the single global instance. Check it for errors before using!
        [[nodiscard]] inline const PosixSpawnAttr &CommonPosixSpawnAttr()
        {
            static PosixSpawnAttr ret = nullptr;
            return ret;
        }

        #endif
    }

    // A list of environment variables.
    using EnvMap = std::map<NativeString, NativeString, std::less<>>;

    // Takes a snapshot of the current environment variables.
    // It might be a good idea to only do this once and save the result somewhere.
    [[nodiscard]] inline EnvMap CurrentEnv()
    {
        EnvMap ret;

        #ifdef _WIN32
        struct Guard
        {
            wchar_t *ptr = GetEnvironmentStringsW();
            ~Guard()
            {
                // Not sure if the null check is needed. Just in case.
                if (ptr)
                    FreeEnvironmentStringsW(ptr);
            }
        };
        Guard guard;
        // SDL silently ignores the pointer being null, so we do too.
        if (guard.ptr)
        {
            // This stores null-terminated strings side by side, and is terminated with double null.
            wchar_t *cur = guard.ptr;
            for (std::wstring_view view(cur); !view.empty(); cur += view.size() + 1)
            {
                // SDL silently ignores the missing `=`, so we do too. It shouldn't be normally possible.
                if (auto pos = view.find(L'='); pos != std::string_view::npos)
                {
                    auto [iter, is_new] = ret.try_emplace(view.substr(0, pos));
                    if (is_new)
                        iter->second = view.substr(pos + 1);
                }
            }
        }

        #else
        const char *const *e = detail::GetEnviron();
        // SDL silently ignores `e` being null, so we do too.
        if (e)
        {
            while (*e)
            {
                std::string_view view = *e++;
                // SDL silently ignores the missing `=`, so we do too. It shouldn't be normally possible.
                // Even `putenv` is said to have a special case for the missing `=` on glibc, which causes it to unset that variable: https://linux.die.net/man/3/putenv
                if (auto pos = view.find('='); pos != std::string_view::npos)
                {
                    auto [iter, is_new] = ret.try_emplace(view.substr(0, pos));
                    if (is_new)
                        iter->second = view.substr(pos + 1);
                }
            }
        }
        #endif

        return ret;
    }

    // Explains why a process has exited.
    struct ExitReason
    {
        // Returns true if exited with code 0.
        [[nodiscard]] bool Success() const
        {
            return ExitedWithCode(0);
        }

        [[nodiscard]] bool ExitedWithCode(int code) const
        {
            auto value = std::get_if<Code>(&var);
            return value && value->code == code;
        }

        // A debug string to represent this exit reason.
        [[nodiscard]] std::string ToString() const
        {
            return std::visit(detail::Overload{
                [](const Detached &) -> std::string {return "Detached background process.";},
                [](const Code &elem) -> std::string {return "Exited with code " + std::to_string(elem.code) + ".";},
                #ifndef _WIN32
                [](const Signal_Posix &elem) -> std::string {std::string ret = "Exited due to signal " + std::to_string(elem.signal) + "."; if (elem.core_dumped) ret += " Core dumped."; return ret;},
                [](const Other_Posix &elem)  -> std::string {return "Exited for an unknown reason: " + std::to_string(elem.status) + ".";},
                #endif
                [](const Error &)    -> std::string {return "Library error.";},
            }, var);
        }


        // Exited normally with code.
        struct Code
        {
            int code = 0;
            friend auto operator<=>(Code, Code) = default; // Don't strictly need those operators on the variant members, but just in case.
        };

        #ifndef _WIN32
        // Terminated by a signal.
        struct Signal_Posix
        {
            int signal = 0;

            // Do we have a core dump? If `EM_PROC_CAN_DETECT_CORE_DUMPS` is not defined, this will always be false.
            bool core_dumped = false;

            friend auto operator<=>(Signal_Posix, Signal_Posix) = default;
        };

        // Exited for another reason.
        struct Other_Posix
        {
            // This value is straight from `waitpid()`.
            int status = 0;
            friend auto operator<=>(Other_Posix, Other_Posix) = default;
        };
        #endif

        // Don't know if exited or not, this is a detached background process.
        struct Detached
        {
            friend auto operator<=>(Detached, Detached) = default;
        };

        // Something is wrong with our library. No error message here, check `Process::ErrorMessage()` for more details.
        struct Error
        {
            friend auto operator<=>(Error, Error) = default;
        };

        using Var = std::variant<
            // `Detached` is listed first because of the dumb default constructibility checks failing for nested classes with member initializers.
            Detached,
            Code,
            #ifndef _WIN32
            Signal_Posix,
            Other_Posix,
            #endif
            Error
        >;
        Var var;

        ExitReason() : var(Code{0}) {} // I guess this is a good default value?
        ExitReason(Var var) : var(std::move(var)) {}
    };

    // A part of parameters of a process. Stores the command line of a process being created.
    class Command : public StoresErrorMessage
    {
      public:
        [[nodiscard]] constexpr Command() {}

        using StoresErrorMessage::StoresErrorMessage;

        #ifdef _WIN32
        // How to handle special symbols in CMD and batch arguments.
        enum class CmdBatchSafety_Win
        {
            // Always error on `%`, because while it's possible to escape, certain batch files still allow it to be misused even when escaped. (E.g. those that run nested `cmd /c ...` instances.)
            // Only allow `!` if `/v:off` is specified, since otherwise `!` can have special behavior that can be unsafe. (`/v:on` would enable this unsafe behavior, and omitting it would take the default from the registry,
            //   which defaults to off, but we don't check the registry and just don't allow `!` in that case).
            // When `Extras::cmd_batch_override_registry == true`, we add our own `/v:off`, making sure `!` is allowed by default.
            safe,
            // For `!`, same behavior as `safe`.
            // For `%`, allow and escape it, but only if `/e:on` is specified, which is necessary for the escape to work. (`e:off` would break our escape mechanism. Omitting it would take the default from the registry,
            //   which defaults to on, but we don't check the registry and just don't allow `%` in that case).
            // If `/e:on` is not specified, will error on `%`.
            // When `Extras::cmd_batch_override_registry == true`, we add our own `/e:on`, making sure `%` is allowed by default.
            relaxed,
            // Allow both `%` and `!` unconditionally, and don't escape them.
            unsafe_as_is,

            // Note, the numbering of those doens't match what `detail::AssembleCommandLine()` accepts, but I really want `safe` to have the value 0, to be the default value.
        };
        #endif

        struct Extras
        {
            #ifdef _WIN32
            // When running `cmd ... /c`, add some flags at `...` to ignore certain registry overrides, ensuring sane consistent behavior even if the user has something weird in the registry.
            // Will also prepend `cmd ... /c` when running batch files to prepend the same flags.
            // Note that this is only respect on constructor overloads that take a list of arguments. Those that take a single long string ignore this.
            bool cmd_batch_override_registry_win = true;

            // Note that this is only respect on constructor overloads that take a list of arguments. Those that take a single long string ignore this.
            CmdBatchSafety_Win cmd_safety_win = CmdBatchSafety_Win::safe;
            #endif
        };

        // Set the command to execute, and its arguments.
        // If `executable` is specified, it replaces `argv[0]` as the program to execute. The original `argv[0]` is then only passed to the program's `main`.
        // On POSIX, this has no special effects, other than making the executable and `argv[0]` different.
        // On Windows, specifying `executable` ignores `PATH` and disables the implicit `.exe` extension, and instead either uses the exact path,
        //   or searches the current directory (`argv[0]` also searches in the current directory on Windows). Also if you pass `\foo\bar.exe`, it searches the current drive.
        // Also on Windows `executable` allows passing overly long executable names. (Those must be prefixed with `\\?\` and canonicalized to use `\` instead of `/`,
        //   don't use multiple adjacent `\`, don't use `.` or `..` directory names, etc. But they remain case-insensitive.)
        #ifdef _WIN32
        [[nodiscard]] Command(std::span<const MaybeOwningNativeString> argv, std::optional<MaybeOwningNativeString> executable = {}, Extras extras = DefaultExtras())
        {
            detail_InitCommand_Win(executable, extras, argv.size(), [&](std::size_t i) -> const auto & {return argv[i];});
        }
        [[nodiscard]] Command(std::initializer_list<MaybeOwningNativeString> argv, std::optional<MaybeOwningNativeString> executable = {}, Extras extras = DefaultExtras())
            : Command(std::span(argv), std::move(executable), std::move(extras))
        {}
        // A vector version for consistency with POSIX.
        [[nodiscard]] Command(const std::vector<MaybeOwningNativeString> &argv, std::optional<MaybeOwningNativeString> executable = {}, Extras extras = DefaultExtras())
            : Command(std::span(argv), std::move(executable), std::move(extras))
        {}
        #else
        [[nodiscard]] Command(std::vector<MaybeOwningNativeString> argv, std::optional<MaybeOwningNativeString> executable = {}, Extras extras = DefaultExtras())
        {
            (void)extras;
            resulting_command = std::move(argv);
            resulting_executable = std::move(executable);
        }
        // Since those are constructors, having the `std::initializer_list` version allows `.command = {"foo", "bar"}`, which is nice.
        // Otherwise this would be unnecessary. Unlike on Windows, where the other overload takes a span rather than a vector, and spans still don't have a constructor from `initializer_list`.
        [[nodiscard]] Command(std::initializer_list<MaybeOwningNativeString> argv, std::optional<MaybeOwningNativeString> executable = {}, Extras extras = DefaultExtras())
        {
            (void)extras;
            resulting_command = std::vector<MaybeOwningNativeString>(std::move(argv)); // If only the init_list could actually be moved...
            resulting_executable = std::move(executable);
        }
        // A span version for consistency with Windows.
        [[nodiscard]] Command(std::span<const MaybeOwningNativeString> argv, std::optional<MaybeOwningNativeString> executable = {}, Extras extras = DefaultExtras())
        {
            (void)extras;
            resulting_command = {argv.size(), [&](std::size_t i) -> const auto & {return argv[i];}};
            resulting_executable = std::move(executable);
        }
        #endif
        // From `argv`. Note that this version copies the strings. There's another overload below that doesn't copy them (on POSIX).
        // Note, the `executable` is narrow here to match `argv`. This is to simplify our implementation, since the command line is assembled narrow here,
        //   and we might need to copy `executable` into the command line. I guess we could overload this with a `NativeString` executable, and narrow that
        //   ourselves, but it's probably not worth it. You can do it yourself if you need.
        // `Command({})` happens to call this overload and not the vector one. This isn't a big deal.
        [[nodiscard]] Command(const char *const *argv, std::optional<MaybeOwningString> executable = {}, Extras extras = DefaultExtras())
        {
            #ifdef _WIN32
            detail_InitCommand_Win(executable, extras, detail::PtrArraySize(argv), [&](std::size_t i) {return argv[i];});
            #else
            (void)extras;
            resulting_command = argv;
            resulting_executable = std::move(executable);
            #endif
        }

        // This version doesn't copy `argv` on POSIX, make sure it doesn't dangle until the process starts.
        // Note, the `executable` is narrow here to match `argv`. This is to simplify our implementation, since the command line is assembled narrow here,
        //   and we might need to copy `executable` into the command line. I guess we could overload this with a `NativeString` executable, and narrow that
        //   ourselves, but it's probably not worth it. You can do it yourself if you need.
        [[nodiscard]] Command(TagNonOwning, const char *const *argv, std::optional<MaybeOwningString> executable = {}, Extras extras = DefaultExtras())
        {
            #ifdef _WIN32
            detail_InitCommand_Win(executable, extras, detail::PtrArraySize(argv), [&](std::size_t i) {return argv[i];});
            #else
            (void)extras;
            resulting_command = {non_owning, argv};
            resulting_executable = std::move(executable);
            #endif
        }

        #ifdef _WIN32
        // Windows special: Command as a single string. Like `argv`, `command_str` must start with the executable name, which can be overridden with `executable`.
        // On Windows, if `executable` is specified, then `command` can be null. It's unclear if this does anything different compared to just passing 0 arguments.
        // NOTE: This ignores `extras.cmd_batch_override_registry_win` and `extras.cmd_safety_win`, but we're still passing `extras` for consistency.
        [[nodiscard]] Command(TagWindowsOnly, std::optional<MaybeOwningMutNativeString> command, std::optional<MaybeOwningNativeString> executable = {}, Extras extras = DefaultExtras())
        {
            (void)extras;
            resulting_command = std::move(command);
            resulting_executable = std::move(executable);
        }

        // Windows special: Command as a wide `argv`.
        // On Windows, if `executable` is specified, then `argv` can be null. It's unclear if this does anything different compared to just passing 0 arguments.
        [[nodiscard]] Command(TagWindowsOnly, const wchar_t *const *argv, std::optional<MaybeOwningNativeString> executable, Extras extras = DefaultExtras())
        {
            detail_InitCommand_Win(executable, extras, detail::PtrArraySize(argv), [&](std::size_t i) {return argv[i];});
        }

        // Windows special: Command as a non-owning string.
        // Note that the command is clobbered when the process is started, so the pointer is non-const.
        // The updated string is not useful and should be discarded.
        // NOTE: This ignores `extras.cmd_batch_override_registry_win` and `extras.cmd_safety_win`, but we're still passing `extras` for consistency.
        [[nodiscard]] Command(TagWindowsOnly, TagNonOwning, wchar_t *command, std::optional<MaybeOwningNativeString> executable, Extras extras = DefaultExtras())
        {
            (void)extras;
            if (command) // `resulting_command` doesn't mind being assigned `nullptr`, but that's treated as an empty string, and we'd rather have it treated as an absence of a command.
                resulting_command = command;
            resulting_executable = std::move(executable);
        }
        #endif

        #ifdef _WIN32
        // Returns the command string generated from the constructor arguments. Mostly for internal use.
        // This is non-const because on Windows the command is clobbered when ran.
        [[nodiscard]] wchar_t *ResultingCommand_Win()
        {
            return resulting_command ? resulting_command->GetMutPointer() : nullptr;
        }
        #else
        // Returns the command arguments as passed to a constructor. Mostly for internal use.
        [[nodiscard]] const char *const *ResultingCommand_Posix() const
        {
            return resulting_command.GetPointer();
        }
        #endif

        // Returns the executable filename as passed to a constructor. Mostly for internal use.
        [[nodiscard]] const std::optional<MaybeOwningNativeString> &ResultingExecutable() const
        {
            return resulting_executable;
        }

      private:
        #ifdef _WIN32
        // This is optional if `exe_path` is specified.
        // We could drop the `optional` and instead treat the empty string as no command, but meh. Would have to do the same for the executable...
        // This uses non-const `wchar_t` because starting the process clobbers it.
        std::optional<detail::MaybeOwningMaybeMutString<wchar_t>> resulting_command;
        #else
        // The element type is `MaybeOwningNativeString` rather than `std::string` to allow moving in `std::vector<MaybeOwningNativeString>` as is.
        detail::StringPtrArray<MaybeOwningNativeString> resulting_command;
        #endif

        std::optional<MaybeOwningNativeString> resulting_executable;

        [[nodiscard]] static Extras DefaultExtras() {return {};} // Make Clang happy.

        #ifdef _WIN32
        // Here `String` is either `MaybeOwningNativeString` or `MaybeOwningString`.
        // `get_arg(i)` should return something string-like of matching character width.
        // This is a little wrapper for `detail::AssembleCommandLine()`. Maybe we could merge them, but it's currently easier not to.
        // This function is always called from constructors, so it doesn't reset the existing values of the fields.
        template <typename String>
        void detail_InitCommand_Win(std::optional<String> executable, Extras extras, std::size_t num_args, auto &&get_arg)
        {
            bool ok = false;

            auto MakeCommand = [&](auto &out_command)
            {
                ok = detail::AssembleCommandLine(
                    executable,
                    num_args,
                    extras.cmd_batch_override_registry_win,
                    extras.cmd_safety_win == CmdBatchSafety_Win::unsafe_as_is ? 0 : extras.cmd_safety_win == CmdBatchSafety_Win::relaxed ? 1 : 2,
                    out_command,
                    error_string,
                    decltype(get_arg)(get_arg)
                );
            };

            if constexpr (std::is_same_v<String, MaybeOwningNativeString>)
            {
                MakeCommand(resulting_command);
                if (ok)
                    resulting_executable = std::move(executable); // This correctly handles null optionals.
            }
            else
            {
                std::optional<detail::MaybeOwningMaybeMutString<detail::StringToCharType<String>>> out_command; // Have to use a temporary output variable of the specific type.
                MakeCommand(out_command);

                if (ok)
                {
                    if (out_command)
                        resulting_command = out_command->GetStringView();

                    if (executable)
                        resulting_executable = executable->GetStringView();
                }
            }

        }
        #endif
    };

    // A part of parameters of a process. Stores the environment variables.
    class Environment : public StoresErrorMessage
    {
      public:
        // Only this constructor means keeping the environment variables of the parent process.
        [[nodiscard]] constexpr Environment() {}

        using StoresErrorMessage::StoresErrorMessage;

        // Set the environment variables. This overrides all variables. Use `CurrentEnv()` to get the variables of the current process, if you only want to modify some.
        // If this is not called, the default behavior is to use the variables of the current process, reading them right when starting the new process (not when constructing `Params`).
        [[nodiscard]] Environment(EnvMap env_vars)
        {
            // Validate.
            for (const auto &elem : env_vars)
            {
                // `=` in the key.
                // If we wanted to check both `=` and `\0` in one line, we could do `.find_first_of(std::basic_string_view(EM_PROC_NATIVE("="), 2))`, but I'd rather have separate nice errors.
                if (elem.first.native.find_first_of('=') != std::size_t(-1))
                {
                    // See above for why we don't report the variable name.
                    error_string = "Some environment variables had `=` in the names.";
                    return;
                }
                // `\0` in the key.
                if (elem.first.native.find_first_of('\0') != std::size_t(-1))
                {
                    // See above for why we don't report the variable name.
                    error_string = "Some environment variables had null characters in the names.";
                    return;
                }
                // `\0` in the value.
                if (elem.second.native.find_first_of('\0') != std::size_t(-1))
                {
                    // See above for why we don't report the variable name.
                    error_string = "Some environment variables had null characters in the values.";
                    return;
                }
            }

            #ifdef _WIN32

            std::size_t needed_size = 0;
            for (const auto &elem : env_vars)
                needed_size += elem.first.native.size() + elem.second.native.size() + 2; // +1 for `=` and +1 for the separating `\0`.

            std::wstring str;
            str.reserve(needed_size); // This way we get `\0\0` at the end, which is exactly what we want.
            for (const auto &elem : env_vars)
            {
                str += elem.first.native;
                str += '=';
                str += elem.second.native;
                str += '\0'; // Intentional even after the last element. We want `\0\0` after the last element.
            }

            resulting_env = std::move(str);

            #else
            resulting_env = {env_vars.size(), [iter = env_vars.begin()](std::size_t) mutable {return iter->first.native + '=' + iter->second.native;}};
            #endif
        }

        // This is only meaningful on Windows, but defined unconditionally for simplicity (so no `_Win` suffix).
        // It's a bit weird that this enum is specific to `class Environment`, but everything else uses `bool *out_success`, and here it doesn't fit because this class stores its own errors.
        enum class EncodingMode
        {
            relaxed, // Insert placeholder characters on encoding errors.
            error, // Fail on encoding errors.
        };

        // This version copies the strings. There's another non-owning one below.
        // Passing null here means no variables.
        [[nodiscard]] Environment(const char *const *env_vars, EncodingMode encoding = EncodingMode::relaxed)
        {
            #ifdef _WIN32
            if (env_vars)
            {
                bool success = false;
                resulting_env = ConvertString_Win(detail::AssembleEnvironmentFromPtr(env_vars), encoding == EncodingMode::error ? &success : nullptr);
                if (encoding == EncodingMode::error && !success)
                    error_string = "Encoding error.";
            }
            #else
            (void)encoding;

            // Validate that all variables have `=` in them.
            if (env_vars)
            {
                for (auto copy = env_vars; *copy; copy++)
                {
                    if (std::string_view(*copy).find('=') == std::string_view::npos)
                    {
                        error_string = "Missing `=` in environment variable.";
                        return;
                    }
                }
            }

            resulting_env = env_vars;
            #endif
        }

        // This version doesn't copy the strings on POSIX, so make sure they don't dangle.
        // Passing null here means no variables.
        [[nodiscard]] Environment(TagNonOwning, const char *const *env_vars, EncodingMode encoding = EncodingMode::relaxed)
        #ifdef _WIN32
            : Environment(env_vars, encoding)
        {}
        #else
        {
            (void)encoding;

            // No validation here, use the pointer as is.
            resulting_env = {non_owning, env_vars};
        }
        #endif

        #ifdef _WIN32
        // Windows special: Environment from a single string, of the form `A=B \0 C=D \0 E=F \0\0`.
        [[nodiscard]] Environment(TagWindowsOnly, MaybeOwningNativeString env)
        {
            // Trust the string validity.
            resulting_env = std::move(env);
        }
        // Windows special: From pointer array. Copies the contents, never dangles.
        [[nodiscard]] Environment(TagWindowsOnly, const wchar_t *const *env_vars)
        {
            resulting_env = detail::AssembleEnvironmentFromPtr(env_vars);
        }
        #endif


        #ifdef _WIN32
        // Returns the command string generated from the constructor arguments. Mostly for internal use.
        // This is non-const because on Windows the command is clobbered when ran.
        [[nodiscard]] const wchar_t *ResultingEnvironment_Win() const
        {
            return resulting_env ? resulting_env->GetPointer() : nullptr;
        }
        #else
        // Returns the command arguments as passed to a constructor. Mostly for internal use.
        [[nodiscard]] const char *const *ResultingEnvironment_Posix() const
        {
            return resulting_env ? resulting_env->GetPointer() : nullptr;
        }
        #endif

      private:
        #ifdef _WIN32
        std::optional<MaybeOwningNativeString> resulting_env;
        #else
        // Using `MaybeOwningNativeString` here instead of `std::string`, because why not.
        std::optional<detail::StringPtrArray<MaybeOwningNativeString>> resulting_env;
        #endif
    };

    struct TagAllHandles {explicit TagAllHandles() = default;};
    // Inherit all file handles as opposed to only some. The main benefit of this is that it's faster to not filter them.
    // Note that this is dangerous! If you inherit a pipe end that's also used by another subprocess as an IO stream redirection, and then keep it open,
    //   if that other subprocess closes it, the other end of the pipe will not receive EOF until you close it too, which is not a good thing.
    inline constexpr TagAllHandles all_handles{};

    // Used to store handles inherited by the subprocess. See `inherit_handles` below.
    // Wasn't sure if those should be `BasicIoStream::handle_t` or the whole `BasicIoStream`. I think storing whole streams is more convenient for ownership, even though it makes it less clear that on Windows those can be arbitrary handles.
    using InheritedHandlesSet = std::unordered_set<BasicIoStream>;
    using InheritedHandles = std::variant<InheritedHandlesSet, TagAllHandles>;

    // A part of parameters of a process. Stores misc stuff that doesn't fit anywhere else, including IO redirects.
    // This is move-only because of the stored IO streams.
    class MiscParams : public StoresErrorMessage
    {
      public:
        [[nodiscard]] constexpr MiscParams() {}

        using StoresErrorMessage::StoresErrorMessage;

        // This causes us to immediately release the process handle after starting it, so you can't wait for it to terminate and can't get its exit code.
        // Also does a few things to separate the child process from the parent.
        // This also doesn't work with `inherit` IO redirects. Those get implicitly replaced with null streams.
        //
        // In theory, on POSIX we could still wait using the PID, using `kill(pid, 0)`. But that seems unreliable, because the PID could be reused by another process. And this is not very useful in the first place.
        //
        // This prevents the mandatory wait for the process in the destructor. (Without this, the destructor is forced to wait on POSIX, otherwise we'd leak resources, look up so-called "zombie processes".
        //   And on Windows it doesn't seem to be the case, but we replicate the POSIX behavior for consistency.)
        //
        // Also on POSIX this has a special effect of ensuring that this child won't get the terminal of the current process if the current process dies before the child, so it couldn't be Ctrl+C'ed in that case.
        //
        // What this does on POSIX is called "double forking" of "daemonizing" the new process, see this for more details: https://stackoverflow.com/q/881388/2752075
        bool detach = false;

        // If specified, replaced the working directory. Otherwise it's inherited from the parent process.
        std::optional<MaybeOwningNativeString> working_directory;


        // What to do with `stdin`. The default behavior is to inherit it from the parent process.
        // Pass `{}` or `nullptr` to make it null.
        // Pass a string to open a file with that path and attach it.
        // Pass an `InputStream` to attach a custom stream. This usually should be used with `MakePipe()`. While you could pass the result of `OpenInputFile()` as a stream too,
        //   that's less optimal than passing the path here as a string.
        InputStreamAttachment stdin_stream = inherit;

        // What to do with `stdout`. The default behavior is to inherit it from the parent process.
        // Pass `{}` or `nullptr` to make it null.
        // Pass a string to open a file with that path and attach it. You can also pass `OutputFileDesc` which stores a file path with some extra settings.
        // Pass an `OutputStream` to attach a custom stream. This usually should be used with `MakePipe()`. While you could pass the result of `OpenOutputFile()` as a stream too,
        //   that's less optimal than passing the path here as a string.
        // Pass `merge_output` to redirect it to `stderr` (rarely useful), then `stderr_stream` controls both streams.
        OutputStreamAttachment stdout_stream = inherit;

        // What to do with `stderr`. The default behavior is to inherit it from the parent process.
        // Pass `{}` or `nullptr` to make it null.
        // Pass a string to open a file with that path and attach it. You can also pass `OutputFileDesc` which stores a file path with some extra settings.
        // Pass an `OutputStream` to attach a custom stream. This usually should be used with `MakePipe()`. While you could pass the result of `OpenOutputFile()` as a stream too,
        //   that's less optimal than passing the path here as a string.
        // Pass `merge_output` to redirect it to `stdout`, then `stderr_stream` controls both streams. This is useful if you want to read both streams combined.
        OutputStreamAttachment stderr_stream = inherit;


        // Which handles should the subprocess inherit.
        // On Windows, those are arbitrary `HANDLE`s. On POSIX, those are file descriptors.
        // The default behavior is to inherit nothing. Insert handles into the set to inherit them, or pass `all_handles` to inherit all of them (which is faster than filtering the handles,
        //   but also dangerous, see the comment of `all_handles` for details).
        // The IO redirects (`*_stream`s above) must not be listed here, we inherit them implicitly.
        // This list can only include handles that are inheritable (`bInheritHandle == true` on Windows, no `O_CLOEXEC` on POSIX). Non-inheritable handles are never inherited,
        //   and inheritable handles are inherited only if allowed by this parameter.
        InheritedHandles inherit_handles;
    };

    // The combined process parameters.
    // Normally you want to use this, but you can also create the individual parameter classes separately, if you want to reuse some of them between several processes.
    // This is move-only because of `MiscParams`.
    struct Params : MiscParams
    {
        Command command;
        Environment env;
    };

    // A baked form of `MiscParams`. This is move-only.
    class BakedMiscParams : public StoresErrorMessage
    {
      public:
        [[nodiscard]] constexpr BakedMiscParams() {}

        // Not movable, because we statically allocate some buffers on Windows (see `State` below for details).
        // If you ever decide to make it movable, don't forget to also move the `StoresErrorMessage` base.
        BakedMiscParams(const BakedMiscParams &) = delete;
        BakedMiscParams &operator=(const BakedMiscParams &) = delete;

        ~BakedMiscParams()
        {
            #ifdef _WIN32
            if (auto attrs = ResultingProcessAttributeList_Win())
                DeleteProcThreadAttributeList(attrs);
            #else
            if (state.spawn_fa_alive)
                posix_spawn_file_actions_destroy(&state.spawn_fa);
            #endif
        }


        using StoresErrorMessage::StoresErrorMessage;

        [[nodiscard]] BakedMiscParams(MiscParams &&params)
            : BakedMiscParams() // Call destructor on throw.
        {
            #ifndef _WIN32
            // Construct spawn file actions.
            // Returns true on error, then this constructor should exit too.
            // Constructs the `posix_spawn_file_actions_t` on the first call. Repeated calls do nothing.
            auto ConstructFileActionsIfNeeded = [&]() -> bool
            {
                if (state.spawn_fa_alive)
                    return false; // Already constructed.

                if (int error = posix_spawn_file_actions_init(&state.spawn_fa))
                {
                    // No useful messages for us to emit here, so just write the number.
                    // The manual doesn't mention this setting `errno`, so we use the return value instead. At least for `posix_spawn`, glibc sets the errno anyway, even though the manual doesn't say so, but for this function I can't check, because it never fails in glibc.
                    error_string = "`posix_spawn_file_actions_init()` failed: " + std::to_string(error);
                    return true;
                }
                state.spawn_fa_alive = true;
                return false;
            };
            #endif

            { // IO handles.
                // Complain if both `stdout` and `stderr` are set to `merge_output`.
                if (std::holds_alternative<TagMergeOutput>(params.stdout_stream) && std::holds_alternative<TagMergeOutput>(params.stderr_stream))
                {
                    error_string = "Both `stdout` and `stderr` are set to `merge_output`. At most one output stream can be in this mode.";
                    return;
                }

                // Complain if any if the inherited handles are invalid.
                if (auto opt = std::get_if<InheritedHandlesSet>(&params.inherit_handles))
                {
                    for (const auto &elem : *opt)
                    {
                        if (elem.HasError())
                        {
                            error_string = "One of the inherited handles is invalid: " + elem.ErrorMessage();
                            return;
                        }

                        if (!elem)
                        {
                            // Apparently this is particualrly important to check.
                            // `https://github.com/rprichard/win32-console-docs/blob/master/README.md#proc_thread_attribute_handle_list` says that passing a `NULL` handle makes the whole list look empty, interesting.
                            // Not sure what passing `INVALID_HANDLE_VALUE` would do, the docs say it's not allowed. Doesn't really matter.
                            error_string = "One of the inherited handles is null.";
                            return;
                        }
                    }
                }

                #ifdef _WIN32

                // Stdin.
                if (std::visit(detail::Overload{
                    [&](InputStream &&elem)
                    {
                        if (elem.HasError())
                        {
                            error_string = "Bad `stdin`: " + elem.ErrorMessage();
                            return true;
                        }

                        // If the user passed null, open a handle to `\\.\NUL` instead. This is what SDL does.
                        // We could open it once and store it, but that's mildly suspicious and probably isn't worth it.
                        if (!elem)
                        {
                            state.stdin_stream = OpenInputFile(R"(\\.\NUL)", Inheritable::as_stdio_stream);
                            if (state.stdin_stream.HasError())
                            {
                                error_string = "Failed to open a `\\\\.\\NUL` handle for `stdin`: " + state.stdin_stream.ErrorMessage();
                                return true;
                            }
                        }

                        state.stdin_stream = std::move(elem);
                        return false;
                    },
                    [&](TagInherit)
                    {
                        HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
                        if (handle == INVALID_HANDLE_VALUE)
                        {
                            error_string = "`GetStdHandle(STD_INPUT_HANDLE)` failed: " + detail::WinApiErrorToString(GetLastError());
                            return true;
                        }

                        // This handle can still be null. In that case we just pass that as is. (Even though this constructor converts null to `INVALID_HANDLE_VALUE`, we convert that back when passing it.)
                        state.stdin_stream = {non_owning, handle};
                        return false;
                    },
                    [&](MaybeOwningNativeString &&elem)
                    {
                        state.stdin_stream = OpenInputFile(std::move(elem), Inheritable::as_stdio_stream);
                        if (state.stdin_stream.HasError())
                        {
                            error_string = "Failed to open `stdin` file: " + state.stdin_stream.ErrorMessage();
                            return true;
                        }

                        return false;
                    },
                }, std::move(params.stdin_stream)))
                {
                    return;
                }

                auto HandleOutputStream = [&](bool is_stderr) -> bool
                {
                    OutputStream &target_field = is_stderr ? state.stderr_stream : state.stdout_stream;
                    const char *const stream_name = is_stderr ? "stderr" : "stdout";

                    return std::visit(detail::Overload{
                        [&](OutputStream &&elem)
                        {
                            if (elem.HasError())
                            {
                                error_string = std::string("Bad `") + stream_name + "`: " + elem.ErrorMessage();
                                return true;
                            }

                            // If the user passed null, open a handle to `\\.\NUL` instead. This is what SDL does.
                            // We could open it once and store it, but that's mildly suspicious and probably isn't worth it.
                            if (!elem)
                            {
                                // No particular reason to pass `no_append`, just to hopefully do less work.
                                target_field = OpenOutputFile(R"(\\.\NUL)", Inheritable::as_stdio_stream, ExistingFile::keep, false, OutputFileFlags::no_append);
                                if (target_field.HasError())
                                {
                                    error_string = std::string("Failed to open a `\\\\.\\NUL` handle for `") + stream_name + "`: " + target_field.ErrorMessage();
                                    return true;
                                }
                            }

                            target_field = std::move(elem);
                            return false;
                        },
                        [&](TagInherit)
                        {
                            HANDLE handle = GetStdHandle(is_stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
                            if (handle == INVALID_HANDLE_VALUE)
                            {
                                error_string = std::string("`GetStdHandle(") + (is_stderr ? "STD_ERROR_HANDLE" : "STD_OUTPUT_HANDLE") + ")` failed: " + detail::WinApiErrorToString(GetLastError());
                                return true;
                            }

                            // This handle can still be null. In that case we just pass that as is. (Even though this constructor converts null to `INVALID_HANDLE_VALUE`, we convert that back when passing it.)
                            target_field = {non_owning, handle};
                            return false;
                        },
                        [&](TagMergeOutput)
                        {
                            // We could just pass the same handle to both streams, but Claude has a good point that the started program might close one of the two handles and don't expect the other one to get closed at the same time. Didn't test this though, but SDL does the same.
                            CloneStream(is_stderr ? state.stdout_stream : state.stderr_stream, target_field, Inheritable::as_stdio_stream);
                            if (target_field.HasError())
                            {
                                error_string = "Failed to merge the output streams: " + target_field.ErrorMessage();
                                return true;
                            }

                            return false;
                        },
                        [&]<typename T>(T &&name_or_desc) requires std::is_same_v<T, MaybeOwningNativeString> || std::is_same_v<T, OutputFileDesc>
                        {
                            target_field = OpenOutputFile(name_or_desc, Inheritable::as_stdio_stream);
                            if (target_field.HasError())
                            {
                                error_string = std::string("Failed to open `") + (is_stderr ? "stderr" : "stdout") + "` file: " + target_field.ErrorMessage();
                                return true;
                            }

                            return false;
                        },
                    }, std::move(is_stderr ? params.stderr_stream : params.stdout_stream));
                };

                if (std::holds_alternative<TagMergeOutput>(params.stdout_stream))
                {
                    // This specific case requires reverse stream initialization order, since we'll be duplicating the `stderr` handle into `stdout`.

                    if (HandleOutputStream(true))
                        return;
                    if (HandleOutputStream(false))
                        return;
                }
                else
                {
                    if (HandleOutputStream(false))
                        return;
                    if (HandleOutputStream(true))
                        return;
                }

                // Handle inherited handles.
                if (std::visit(detail::Overload{
                    [&](InheritedHandlesSet &input_handle_set)
                    {
                        // Prepare the handle set:

                        state.inherited_handle_set = std::move(input_handle_set);

                        std::size_t num_handles = state.inherited_handle_set.size() + bool(state.stdin_stream) + bool(state.stdout_stream) + bool(state.stderr_stream);

                        // Do we actually have any handles to inherit?
                        if (num_handles == 0)
                            return false;

                        // Allocate the array.
                        HANDLE *handle_arr = nullptr;
                        if (num_handles <= num_preallocated_inherited_handles)
                        {
                            handle_arr = state.inherited_handles_storage_array;
                        }
                        else
                        {
                            state.inherited_handles_storage_ptr = std::make_unique<HANDLE[]>(num_handles);
                            handle_arr = state.inherited_handles_storage_ptr.get();
                        }

                        // Fill the array.
                        std::size_t handle_arr_pos = 0;

                        if (state.stdin_stream)
                            handle_arr[handle_arr_pos++] = state.stdin_stream.Handle();
                        if (state.stdout_stream)
                            handle_arr[handle_arr_pos++] = state.stdout_stream.Handle();
                        if (state.stderr_stream)
                            handle_arr[handle_arr_pos++] = state.stderr_stream.Handle();

                        for (const auto &elem : state.inherited_handle_set)
                            handle_arr[handle_arr_pos++] = elem.Handle(); // We already checked `HasError()` for those earlier, and also tested them for being null.

                        assert(handle_arr_pos == num_handles);


                        // Make the attribute list:

                        // How many bytes do we need to allocate for the attribute list.
                        // This depends on the max number of attributes we're planning to store inside, which in our case is one (the list of inherited handles).
                        static const std::size_t attribute_list_byte_size = []{
                            std::size_t ret = 0;
                            // The docs say this always returns an error when querying the size, by design. So I'm not checking the return value.
                            InitializeProcThreadAttributeList(nullptr, attrs_capacity, 0, &ret);
                            assert(ret > 0);
                            return ret;
                        }();
                        if (attribute_list_byte_size == 0)
                        {
                            error_string = "`InitializeProcThreadAttributeList()` failed when determining the needed capacity.";
                            return true;
                        }

                        // We can't call `ResultingProcessAttributeList_Win()` to initialize this, since `state.attribute_list_alive` is still false.
                        LPPROC_THREAD_ATTRIBUTE_LIST attr_list = nullptr;

                        // If on this system `attribute_list_byte_size` is higher than expected, heap-allocate the thing, and also assert.
                        if (attribute_list_byte_size > expected_attribute_list_byte_size)
                        {
                            assert(false && "`InitializeProcThreadAttributeList()` returns a larger value than expected on your Windows version, go bump `expected_attribute_list_byte_size` in the library and report this to me. This is not a bug.");
                            state.attribute_list_storage_ptr = std::make_unique<unsigned char[]>(attribute_list_byte_size);
                            attr_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(state.attribute_list_storage_ptr.get());
                        }
                        else
                        {
                            // Use the embedded storage.
                            attr_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(state.attribute_list_storage_array);
                        }

                        // I don't think this value is actually going to get modified, but it's easier to make a mutable copy than to do `const_cast` and then having to worry about it.
                        std::size_t attribute_list_byte_size_copy = attribute_list_byte_size;
                        if (!InitializeProcThreadAttributeList(attr_list, attrs_capacity, 0, &attribute_list_byte_size_copy))
                        {
                            error_string = "`InitializeProcThreadAttributeList()` failed: " + detail::WinApiErrorToString(GetLastError());
                            return true;
                        }
                        state.attribute_list_alive = true;


                        // Pass the array to the attribute list. That only stores a pointer to it, so we could fill it even after passing, doesn't matter.

                        if (!UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handle_arr, num_handles * sizeof(HANDLE), nullptr, nullptr))
                        {
                            error_string = "`UpdateProcThreadAttribute()` failed when storing the inherited handle list: " + detail::WinApiErrorToString(GetLastError());
                            return true;
                        }

                        return false;
                    },
                    [&](TagAllHandles)
                    {
                        return false;
                    },
                }, params.inherit_handles))
                {
                    return;
                }

                #else

                // Returns true on error, then this constructor should exit too.
                // `target_handle` is the desired handle in the new process, one of `STD{IN,OUT,ERR}_FILENO`.
                // `source_handle` is the existing handle in the current process.
                auto AddDup2 = [&](int target_handle, int source_handle) -> bool
                {
                    if (ConstructFileActionsIfNeeded())
                        return true;

                    if (int error = posix_spawn_file_actions_adddup2(&state.spawn_fa, source_handle, target_handle))
                    {
                        // Not adding the stream name to the error message. This shouldn't normally fail, so it's not particularly important.
                        error_string = std::string("`posix_spawn_file_actions_adddup2()` failed: ") + std::strerror(error);
                        return true;
                    }

                    return false;
                };
                // Returns true on error, then this constructor should exit too.
                // `target_handle` is the desired handle in the new process, one of `STD{IN,OUT,ERR}_FILENO`.
                auto AddOpen = [&](int target_handle, const char *filename, int flags, mode_t new_file_permissions) -> bool
                {
                    if (ConstructFileActionsIfNeeded())
                        return true;

                    if (int error = posix_spawn_file_actions_addopen(&state.spawn_fa, target_handle, filename, flags, new_file_permissions))
                    {
                        // Not adding the stream name to the error message. This shouldn't normally fail, so it's not particularly important.
                        error_string = std::string("`posix_spawn_file_actions_adddup2()` failed: ") + std::strerror(error);
                        return true;
                    }

                    return false;
                };
                // Returns true on error, then this constructor should exit too.
                // `target_handle` is the handle we're closing.
                auto AddClose = [&](int target_handle) -> bool
                {
                    if (ConstructFileActionsIfNeeded())
                        return true;

                    if (int error = posix_spawn_file_actions_addclose(&state.spawn_fa, target_handle))
                    {
                        // Not adding the handle to the error message. This shouldn't normally fail, so it's not particularly important.
                        error_string = std::string("`posix_spawn_file_actions_addclose()` failed: ") + std::strerror(error);
                        return true;
                    }

                    return false;
                };

                // Stdin.
                if (std::visit(detail::Overload{
                    [&](InputStream &&elem)
                    {
                        // The order matters here. A null stream with a error inside should trigger the error message, instead of silently opening `/dev/null`.
                        if (elem.HasError())
                        {
                            error_string = "Bad `stdin`: " + elem.ErrorMessage();
                            return true;
                        }

                        if (!elem)
                            return AddOpen(STDIN_FILENO, "/dev/null", O_RDONLY, 0);

                        state.stdin_stream = std::move(elem);
                        return AddDup2(STDIN_FILENO, state.stdin_stream.Handle());
                    },
                    [&](TagInherit)
                    {
                        return false;
                    },
                    [&](MaybeOwningNativeString &&elem)
                    {
                        return AddOpen(STDIN_FILENO, elem.GetPointer(), O_RDONLY, 0);
                    },
                }, std::move(params.stdin_stream)))
                {
                    return;
                }

                // Stdout.
                auto HandleOutputStream = [&](bool is_stderr) -> bool
                {
                    const int target_handle = is_stderr ? STDERR_FILENO : STDOUT_FILENO;

                    return std::visit(detail::Overload{
                        [&](OutputStream &&elem)
                        {
                            // The order matters here. A null stream with a error inside should trigger the error message, instead of silently opening `/dev/null`.
                            if (elem.HasError())
                            {
                                error_string = (is_stderr ? "Bad `stderr`: " : "Bad `stdout`: ") + elem.ErrorMessage();
                                return true;
                            }

                            if (!elem && !elem.HasError())
                                return AddOpen(target_handle, "/dev/null", O_WRONLY, 0);

                            auto &target_field = (is_stderr ? state.stderr_stream : state.stdout_stream);
                            target_field = std::move(elem);

                            return AddDup2(target_handle, target_field.Handle());
                        },
                        [&](TagInherit)
                        {
                            return false;
                        },
                        [&](TagMergeOutput)
                        {
                            return AddDup2(target_handle, is_stderr ? STDOUT_FILENO : STDERR_FILENO);
                        },
                        [&](MaybeOwningNativeString &&elem)
                        {
                            OutputFileDesc desc;
                            return AddOpen(target_handle, elem.GetPointer(), desc.ToOpenFlags_Posix(), desc.ToOpenPermissions_Posix());
                        },
                        [&](OutputFileDesc &&desc)
                        {
                            return AddOpen(target_handle, desc.filename.GetPointer(), desc.ToOpenFlags_Posix(), desc.ToOpenPermissions_Posix());
                        },
                    }, std::move(is_stderr ? params.stderr_stream : params.stdout_stream));
                };

                // Stdout and stderr.
                if (std::holds_alternative<TagMergeOutput>(params.stdout_stream))
                {
                    // This specific case requires reverse stream initialization order. Otherwise `stdout` gets redirect to the old value of `stderr`, not the new one.

                    if (HandleOutputStream(true))
                        return;
                    if (HandleOutputStream(false))
                        return;
                }
                else
                {
                    if (HandleOutputStream(false))
                        return;
                    if (HandleOutputStream(true))
                        return;
                }


                // Handle inherited handles.
                // Do this after the standard streams, otherwise we could close a handle that we want to `dup2()` into one.
                if (std::visit(detail::Overload{
                    [&](InheritedHandlesSet &input_handle_set)
                    {
                        state.inherited_handle_set = std::move(input_handle_set);

                        // How should we close the unwanted handles? Hmm.

                        // Option 1: `posix_spawn_file_actions_addclosefrom_np()`.
                        // We can only use this if we inherit nothing (not counting IO redirects).
                        // Interestingly, in the current version of glibc it seems to use a plain loop instead of `/proc/self/fd`. Oh well.
                        #if EM_PROC_HAVE_POSIX_SPAWN_FILE_ACTIONS_ADDCLOSEFROM_NP
                        if (state.inherited_handle_set.empty())
                        {
                            ConstructFileActionsIfNeeded();
                            if (int error = posix_spawn_file_actions_addclosefrom_np(&state.spawn_fa, 3)) // 0,1,2 are the standard streams, and this closes anything greater or equal to the second argument.
                            {
                                error_string = std::string("`posix_spawn_file_actions_addclosefrom_np()` failed: ") + std::strerror(error);
                                return true;
                            }

                            return false; // This is enough, don't need to fall back to other approaches.
                        }
                        #endif

                        // Like `AddClose()`, but doesn't close handles listed in `state.inherited_handle_set`, or handles of the standard IO streams.
                        // Also does nothing if the handle has `O_CLOEXEC` already enabled (so will be closed automatically).
                        // If `maybe_invalid_fd == true`, then silently ignore the error if `target_handle` is a non-existent handle. We need this when looping over all possible handles.
                        auto AddCloseIfNeeded = [&](int target_handle, bool maybe_invalid_fd) -> bool
                        {
                            if (target_handle < 3) // 0,1,2 are standard streams.
                                return false; // Skip.

                            if (state.inherited_handle_set.contains({non_owning, target_handle}))
                                return false; // Included in the set, skip.

                            int flags = fcntl(target_handle, F_GETFD);
                            if (flags == -1)
                            {
                                int errno_copy = errno;
                                if (!(maybe_invalid_fd && errno_copy == EBADF)) // Interestingly, the correct error code is `EBADF`. `EBADFD` is something else.
                                {
                                    error_string = std::string("`fcntl(F_GETFD)` failed: ") + std::strerror(errno_copy);
                                    return true;
                                }
                            }

                            if (flags & FD_CLOEXEC)
                                return false; // Don't need to close this one.

                            return AddClose(target_handle);
                        };

                        // Option 2: `/proc/self/fd`.
                        #if EM_PROC_HAVE_PROC_SELF_FD
                        {
                            struct DirGuard
                            {
                                BakedMiscParams &self;
                                DIR *dir = nullptr;

                                DirGuard(BakedMiscParams &self)
                                    : self(self)
                                {
                                    dir = opendir("/proc/self/fd");

                                    // Just ignore any errors here. Fall back to another approach.

                                    // But assert if the error is something unusual.
                                    assert(dir || errno == ENOENT);
                                }

                                void Close(bool graceful)
                                {
                                    if (dir)
                                    {
                                        if (closedir(dir) < 0 && graceful)
                                            self.error_string = std::string("`closedir()` failed: ") + std::strerror(errno);

                                        // Must reset this to null, since we're doing this twice.
                                        dir = nullptr;
                                    }
                                }

                                ~DirGuard()
                                {
                                    Close(false);
                                }
                            };
                            DirGuard dir{*this};
                            // Don't need to check `HasError()` after creating `dir`, it doesn't write any errors.

                            if (dir.dir)
                            {
                                while (true)
                                {
                                    errno = 0; // Must explicitly zero `errno` to distinguish errors from running out of files.

                                    dirent *entry = readdir(dir.dir);
                                    if (!entry)
                                    {
                                        int errno_copy = errno;
                                        if (errno_copy == 0)
                                            break; // No more files in the directory.

                                        // Failed to access the next entry...
                                        error_string = std::string("`readdir(\"/proc/self/fd\")` failed: ") + std::strerror(errno_copy);
                                        return true;
                                    }

                                    if (entry->d_type == DT_DIR)
                                        continue; // Probably `.` or `..`, ignore.

                                    // Parse the number.
                                    int fd = 0;
                                    for (const char *cur = entry->d_name; *cur; cur++)
                                    {
                                        char ch = *cur;
                                        if (ch < '0' || ch > '9')
                                        {
                                            // Non-digit in name, hmm.
                                            error_string = std::string("Strage filename `") + entry->d_name + "` found in `/proc/self/fd`.";
                                            return true;
                                        }

                                        fd = fd * 10 + (ch - '0');
                                    }

                                    // Add an action to close this FD.
                                    if (AddCloseIfNeeded(fd, false))
                                        return true; // Failed to add the action.
                                }

                                // Gracefully close the directory:
                                dir.Close(true);
                                if (HasError())
                                    return true; // Failed to close directory.

                                return false; // This is enough, don't need to fall back to other approaches.
                            }
                        }
                        #endif

                        { // Option 3: Loop over the descriptors.
                            long max_num_fds = sysconf(_SC_OPEN_MAX); // I wonder why this returns `long`.
                            if (max_num_fds < 0)
                            {
                                error_string = std::string("`sysconf(_SC_OPEN_MAX)` failed: ") + std::strerror(errno);
                                return true;
                            }

                            // 0,1,2 are the standard streams. Custom ones start from 3.
                            for (int fd = 3; fd < int(max_num_fds); fd++)
                            {
                                if (AddCloseIfNeeded(fd, true))
                                    return true;
                            }
                        }

                        return false;
                    },
                    [&](TagAllHandles)
                    {
                        return false;
                    },
                }, params.inherit_handles))
                {
                    return;
                }
                #endif
            }

            // Working directory.
            #ifdef _WIN32
            state.working_directory = std::move(params.working_directory);
            #else
            // `_np` suffix means "non-portable" and marks experimental functions.
            // Modern glibc has a version of it without the suffix, and so does MacOS. MacOS marks the `_np` version as deprecated.
            // Android NDK doesn't have a non-`_np` version though (at v29, which is what I'm looking at).
            // So I'm using the `_np` version just in case.
            if (params.working_directory)
            {
                ConstructFileActionsIfNeeded();
                if (int error = posix_spawn_file_actions_addchdir_np(&state.spawn_fa, params.working_directory->GetPointer()))
                {
                    error_string = "`posix_spawn_file_actions_addchdir()` failed: " + std::to_string(error);
                }
            }
            #endif


            // Lastly, reset the consumed input params, just in case.
            params = {};
        }


        [[nodiscard]] bool ShouldDetach() const
        {
            return state.detach;
        }

        #ifdef _WIN32
        [[nodiscard]] const std::optional<MaybeOwningNativeString> &ResultingWorkingDir_Win() const
        {
            return state.working_directory;
        }

        // The attribute list for the process, or null if none.
        [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST ResultingProcessAttributeList_Win()
        {
            if (state.attribute_list_alive)
                return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(state.attribute_list_storage_ptr ? state.attribute_list_storage_ptr.get() : state.attribute_list_storage_array);
            else
                return nullptr;
        }

        [[nodiscard]] BasicIoStream::handle_t ResultingStdinHandle_Win() const
        {
            // By default those would return `INVALID_HANDLE_VALUE` when null, which isn't what we want, because the subprocess would apparently receive the same invalid handle.
            // While passing `NULL` may sometimes cause the child to allocate its own console. (All that is Claude is to be trusted.)
            // It shouldn't happen often, though, as when the user requests a null stream, we pass an opened handle to `\\.\NUL` instead. An actual `NULL` would only happen if we try to inherit the parent stream and it's null.
            return state.stdin_stream ? state.stdin_stream.Handle() : nullptr;
        }

        [[nodiscard]] BasicIoStream::handle_t ResultingStdoutHandle_Win() const
        {
            return state.stdout_stream ? state.stdout_stream.Handle() : nullptr;
        }

        [[nodiscard]] BasicIoStream::handle_t ResultingStderrHandle_Win() const
        {
            return state.stderr_stream ? state.stderr_stream.Handle() : nullptr;
        }
        #else
        [[nodiscard]] const posix_spawn_file_actions_t *ResultingFileActions_Posix() const
        {
            // `spawn_fa_alive == false` is not an error here. It can mean that we didn't need any custom file actions.
            return state.spawn_fa_alive ? &state.spawn_fa : nullptr;
        }
        #endif

      private:

        #ifdef _WIN32
        // How many attributes should the attribute list support. Currently only one, the list of handles.
        static constexpr DWORD attrs_capacity = 1;

        // The size that I observed `InitializeProcThreadAttributeList()` to report, for the current `attrs_capacity`.
        // We use this to avoid heap allocations. If this happens to be wrong, we assert and fall back to a heap allocation.
        static constexpr std::size_t expected_attribute_list_byte_size = 48;

        // How many handles we'll typically inherit, so we can allocate a heap buffer for those. Typically those are `stdin`/`stdout`/`stderr`.
        // Unlike `expected_attribute_list_byte_size`, which is expected to always be correct, it's fine for this one to be too small depending on the provided params.
        static constexpr std::size_t num_preallocated_inherited_handles = 3;
        #endif


        struct State
        {
            bool detach = false;

            #ifdef _WIN32
            std::optional<MaybeOwningNativeString> working_directory;

            // Those two are storage for `AttributeList()`. Use that, don't read them directly.
            unsigned char attribute_list_storage_array[expected_attribute_list_byte_size]{};
            std::unique_ptr<unsigned char[]> attribute_list_storage_ptr;
            bool attribute_list_alive = false;

            // Those two are for the list of inherited handles, stored inside of `AttributeList()`.
            // The first one right there is why `BakedMiscParams` is not movable. A pointer to this is baked into `AttributeList()` (unless we need a larger array, in which case it gets heap-allocated in `inherited_handles_storage_ptr`).
            HANDLE inherited_handles_storage_array[num_preallocated_inherited_handles]{};
            std::unique_ptr<HANDLE[]> inherited_handles_storage_ptr;

            #else
            bool spawn_fa_alive = false;
            posix_spawn_file_actions_t spawn_fa{};
            #endif

            // Those keep IO redirect handles alive until the process starts.
            // On POSIX, the handle values are baked into `spawn_fa`, so we don't need to read them from here.
            // On Windows, we read them from here (and they are also included in the list of inherited handles in the attribute list).
            InputStream stdin_stream;
            OutputStream stdout_stream;
            OutputStream stderr_stream;

            // This is used to keep extra inherited handles alive.
            InheritedHandlesSet inherited_handle_set;
        };
        State state;
    };

    // A single subprocess.
    class Process : public StoresErrorMessage
    {
      public:
        [[nodiscard]] constexpr Process() {}

        using StoresErrorMessage::StoresErrorMessage;

        // The high-level constructor taking the parameter struct. The params aren't copyable.
        [[nodiscard]] Process(Params &&params)
            : Process(std::move(params.command), params.env, BakedMiscParams(std::move(params)))
        {}

        // The low-level constructor from separate parameter classes.
        // This has two versions: taking `const Command &` and `Command &&`. On Windows, the command is consumed by starting the process,
        //   so there if you don't move it, it has to be copied. (Copying the whole `Command` is not entirely optimal,
        //   since the executable name is not consumed but we copy it anyway, but I don't see a good solution to this. Who cares anyway.)

        #ifdef _WIN32
        [[nodiscard]] Process(const Command &command, const Environment &env, const BakedMiscParams &misc)
            : Process(Command(command), env, misc)
        {}

        [[nodiscard]] Process(Command &&command, const Environment &env, const BakedMiscParams &misc)
        #else
        [[nodiscard]] Process(Command &&command, const Environment &env, const BakedMiscParams &misc)
            : Process(command, env, misc)
        {}

        [[nodiscard]] Process(const Command &command, const Environment &env, const BakedMiscParams &misc)
        #endif
        {
            // Mark as background process before doing anything else, so that this information is not lost on error.
            if (misc.ShouldDetach())
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Detached{});

            // Then check for errors in parameters.
            if (command.HasError())
            {
                error_string = "Bad command: " + command.ErrorMessage();
                return;
            }
            if (env.HasError())
            {
                error_string = "Bad environment: " + env.ErrorMessage();
                return;
            }
            if (misc.HasError())
            {
                error_string = "Bad parameters: " + misc.ErrorMessage();
                return;
            }


            #ifdef _WIN32
            wchar_t *command_ptr = command.ResultingCommand_Win();

            const wchar_t *executable_ptr = nullptr;
            if (const auto &opt = command.ResultingExecutable())
                executable_ptr = opt->GetPointer();

            if (!command_ptr && !executable_ptr)
            {
                // WinAPI needs at least one.
                error_string = "No command and no executable path specified for process.";
                return;
            }

            const wchar_t *working_dir_ptr = nullptr;
            if (const auto &opt = misc.ResultingWorkingDir_Win())
                working_dir_ptr = opt->GetPointer();

            STARTUPINFOEXW startup_info{};

            // The attribute list is a mutable pointer, but I don't think starting the process can actually modify it, so here I `const_cast`.
            // Since `BakedMiscParams` can use a small buffer optimization for it, this function itself can't be `const`.
            if (auto attrs = const_cast<BakedMiscParams &>(misc).ResultingProcessAttributeList_Win())
            {
                startup_info.StartupInfo.cb = sizeof(startup_info);
                startup_info.lpAttributeList = attrs;
            }
            else
            {
                // I'm not sure if I'm allowed to pass a null `lpAttributeList` or not. It's easier to just set `cb` to the size of the non-`EX` part, the `STARTUPINFOW`. That will prevent the attribute list pointer from being passed.
                startup_info.StartupInfo.cb = sizeof(startup_info.StartupInfo);
            }

            // IO streams.
            // Here we could avoid passing `STARTF_USESTDHANDLES`, which would enable some defaults, but I might as well do that myself when determining the handles when baking the parameters.
            // I don't think there's any benefit to special-casing not passing those here when all the streams match the default values.
            startup_info.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
            startup_info.StartupInfo.hStdInput = misc.ResultingStdinHandle_Win();
            startup_info.StartupInfo.hStdOutput = misc.ResultingStdoutHandle_Win();
            startup_info.StartupInfo.hStdError = misc.ResultingStderrHandle_Win();

            struct ProcInfoGuard
            {
                PROCESS_INFORMATION value{};

                ~ProcInfoGuard()
                {
                    // Not sure if the validity checks are needed. The manual doesn't say anything about allowing invalid handles, so probably needed.
                    // Both null and `INVALID_HANDLE_VALUE` are invalid values.
                    if (value.hProcess && value.hProcess != INVALID_HANDLE_VALUE)
                        CloseHandle(value.hProcess);
                    if (value.hThread && value.hThread != INVALID_HANDLE_VALUE)
                        CloseHandle(value.hThread);
                }
            };
            ProcInfoGuard proc_info;

            // Here if `env_ptr` is not specified, WinAPI copies the environment of this process, which is exactly what we want.
            bool ok = CreateProcessW(
                executable_ptr,
                command_ptr,
                nullptr, // Process attributes.
                nullptr, // Thread attributes.
                true, // Inherit handles.
                CREATE_UNICODE_ENVIRONMENT |
                    // Randomly stumbled upon this flag, seems helpful.
                    (CREATE_NEW_PROCESS_GROUP * misc.ShouldDetach()),
                // It's mildly sus that `env_ptr` needs a `const_cast`. The function parameter is of type `void *`. Unlike for the command line, the documentation (at https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
                //   doesn't say that the environment is clobbered, so I think the `const_cast` is fine.
                const_cast<wchar_t *>(env.ResultingEnvironment_Win()),
                working_dir_ptr,
                &startup_info.StartupInfo,
                &proc_info.value
            );
            if (!ok)
            {
                error_string = "Failed to start process: " + detail::WinApiErrorToString(GetLastError()); // This specific error message doesn't mention the function name, since it happens often and is considered user-facing.
                return;
            }

            // Get the pid.
            state.pid = proc_info.value.dwProcessId;


            // Lastly, for non-background processes, preserve the handle.
            if (!misc.ShouldDetach())
                state.process_handle = std::exchange(proc_info.value.hProcess, INVALID_HANDLE_VALUE);

            #else
            // Note the `const_cast` here. It's needed because the POSIX API takes `char *const *` instead of `const char *const *`, because the C pointer conversion rules are more strict than the C++ ones,
            //   and they figured it would be more convenient. They don't actually modify those strings.
            // Similarly for `env_ptr` below.
            char **command_ptr = const_cast<char **>(command.ResultingCommand_Posix());
            if (!command_ptr)
            {
                // We check it here, because we sometimes use `command_ptr[0]` below and `command_ptr` better not be null.
                error_string = "Null `argv` specified for process.";
                return;
            }
            if (!command_ptr[0])
            {
                // We check it here, because we sometimes use `command_ptr[0]` below and the array better not be empty.
                error_string = "Empty `argv` specified for process.";
                return;
            }

            const detail::PosixSpawnAttr &spawn_attr_holder = detail::CommonPosixSpawnAttr();
            if (spawn_attr_holder.HasError())
            {
                error_string = "Bad `posix_spawnattr_t`: " + spawn_attr_holder.ErrorMessage();
                return;
            }
            // Calling it here rather than after `vfork()`, just in case.
            const posix_spawnattr_t *spawn_attr = &spawn_attr_holder.Underlying();

            // This can be null.
            const posix_spawn_file_actions_t *spawn_fa_opt = misc.ResultingFileActions_Posix();


            // See `command_ptr` above for why we `const_cast` here and why it's safe.
            char *const *env_ptr = const_cast<char *const *>(env.ResultingEnvironment_Posix());
            if (!env_ptr)
                env_ptr = detail::GetEnviron(); // User didn't override the environment, get the current one.

            const char *executable_ptr = nullptr;
            if (const auto &opt = command.ResultingExecutable())
                executable_ptr = opt->GetPointer();
            if (!executable_ptr)
                executable_ptr = command_ptr[0]; // Unlike on Windows, the executable name can't be null, or the child process segfaults on start.

            if (misc.ShouldDetach())
            {
                // This is similar to what SDL does. Except they don't block signals.

                // First, temporarily disable signals so their handlers don't run in the forked process. Would be especially weird with `vfork()`.
                struct SignalGuard
                {
                    bool error = false;
                    Process *self;

                    sigset_t old_sigmask{};
                    sigset_t new_sigmask{};

                    SignalGuard(Process &new_self)
                        : self(&new_self)
                    {
                        // Set the new mask to all ones.
                        if (sigfillset(&new_sigmask))
                        {
                            self->error_string = std::string("`sigfillset()` failed: ") + std::strerror(errno);
                            error = true;
                            return;
                        }

                        // Disable all signals. (Enabled bits in the mask mean disabled signals.)

                        // In theory, this is a thread-safe version of `sigprocmask()`. In practice on glibc they seem to do the exact same thing, so we could use either one.
                        // `pthread_sigmask()` looks more correct to me, but if it causes portability issues, we could replace it with `sigprocmask()`.
                        // It seems this works fine even without `-pthread`, on glibc at least.
                        // NOTE: Their API is slightly different. `pthread_sigmask()` returns the error code on failure, while `sigprocmask()` returns -1 on failure and writes to `errno`.
                        if (int sigmask_status = pthread_sigmask(SIG_SETMASK, &new_sigmask, &old_sigmask); sigmask_status < 0)
                        {
                            self->error_string = std::string("`pthread_sigmask()` failed to disable signals before forking: ") + std::strerror(sigmask_status);
                            error = true;
                            return;
                        }
                    }

                    SignalGuard(const SignalGuard &) = delete;
                    SignalGuard &operator=(const SignalGuard &) = delete;

                    void Finish() noexcept
                    {
                        if (!self)
                            return;

                        if (int sigmask_status = pthread_sigmask(SIG_SETMASK, &old_sigmask, nullptr); sigmask_status < 0)
                        {
                            self->error_string = std::string("`pthread_sigmask()` failed to restore signals after forking: ") + std::strerror(sigmask_status);
                            error = true;
                            return;
                        }

                        self = nullptr;
                    }

                    ~SignalGuard()
                    {
                        Finish();
                    }
                };
                SignalGuard signal_guard(*this);
                if (signal_guard.error)
                    return;

                #ifdef __APPLE__ // SDL says:  Apple has vfork marked as deprecated and (as of macOS 10.12) is almost identical to calling fork() anyhow.
                const pid_t pid = fork();
                const char *fork_error_prefix = "`fork()` failed: ";
                #else
                // `vfork` makes us share memory with the parent (unless implemented as `fork`), which means we must be extra careful to not touch anything. See manual: https://linux.die.net/man/3/vfork
                const pid_t pid = vfork();
                const char *fork_error_prefix = "`vfork()` failed: ";
                #endif
                switch (pid)
                {
                  case -1:
                    // Forking failed.
                    error_string = std::string(fork_error_prefix) + std::strerror(errno);
                    return;

                  case 0:
                    // Forking successful, we're in the new process.

                    // SDL says this detaches us from the original terminal. This creates a new "session" and makes this process its owner: https://linux.die.net/man/2/setsid
                    setsid();

                    // If we `vfork()`ed, it's theoretically unsafe to touch any memory of the parent process, and we touch `state.pid` here.
                    // But the worst that can happen (if `vfork` is implemented as `fork`) is that `state.pid` doesn't propagate to the parent and remains zero there.

                    // Note the use of `_exit()` as opposed to `std::exit()`.
                    // `vfork` manual says we must use this specific function.

                    // It's technically undefined `posix_spawnp()` in `vfork()` (it's not in the list of allowed functions), but SDL does it anyway, and it seems to be fine in practice.

                    // See the other call to `posix_spawnp()` below for more helpful comments.
                    _exit(posix_spawnp(&state.pid, executable_ptr, spawn_fa_opt, spawn_attr, command_ptr, env_ptr));

                  default:
                    // Firstly, allow signals again.
                    // Don't check `signal_guard.error` here though! I don't want to leak the process handle because of it.
                    signal_guard.Finish();

                    // Check the exit code of the direct child process. Also must call it to clean it up, otherwise it remains as a zombie.
                    // Note `pid` here, not `state.pid`.
                    // `waitpid` can wait for some other things than process termination, but it seems all of that is opt-in via the flags parameter, and by default it only waits for termination.
                    int status = -1;
                    if (waitpid(pid, &status, 0) < 0)
                    {
                        error_string = std::string("`waitpid()` failed: ") + std::strerror(errno);
                        return;
                    }

                    // Analyze the exit status of the child.
                    if (WIFEXITED(status)) // Did the process exit normally? Regardless of the exit code.
                    {
                        if (int exit_code = WEXITSTATUS(status))
                        {
                            // We use the exit code to propagate the error code from `posix_spawnp` above.
                            error_string = std::string("Failed to start process: ") + std::strerror(exit_code); // This specific error message doesn't mention the function name, since it happens often and is considered user-facing.
                            return;
                        }
                    }
                    else
                    {
                        error_string = "Forked process exited abnormally. Status integer: " + std::to_string(status);
                        return;
                    }

                    // Lastly, check `signal_guard` for errors.
                    // Do this after cleaning up the child process handle.
                    if (signal_guard.error)
                        return;

                    break;
                }
            }
            else
            {
                // Not a background process, just call `posix_spawnp()` normally.

                // The manual doesn't mention `posix_spawnp` setting `errno`. It still does at least in glibc, but it's more correct to use the return value.

                // Note the `const_cast` here and on `env_ptr` above. It's needed because the POSIX API takes `char *const *` instead of `const char *const *`, because the C pointer conversion rules are more strict than the C++ ones,
                //   and they figured it would be more convenient. They don't actually modify those strings.

                // Note that here the second parameter can't be null. If we don't have a custom executable name, we have to pass `argv[0]` ourselves, or the child will segfault.

                if (int error = posix_spawnp(&state.pid, executable_ptr, spawn_fa_opt, spawn_attr, command_ptr, env_ptr))
                {
                    error_string = std::string("Failed to start process: ") + std::strerror(error); // This specific error message doesn't mention the function name, since it happens often and is considered user-facing.
                    return;
                }

                state.owns_pid = true;
            }

            assert(state.pid);
            #endif
        }

        // This is move-only.
        [[nodiscard]] Process(Process &&other) noexcept
            : StoresErrorMessage(std::move(other)),
            state(std::move(other.state))
        {
            other.state = {};
        }

        Process &operator=(Process other) noexcept
        {
            std::swap(static_cast<StoresErrorMessage &>(*this), static_cast<StoresErrorMessage &>(other));
            std::swap(state, other.state);
            return *this;
        }

        // The default behavior is to wait for the process (if `IsBackground() == false`).
        // We have to wait to clean up the process, otherwise it remains as a "zombie", because we never consumed its exit status.
        ~Process()
        {
            // Not checking `HasError()`, it shouldn't stop us from calling `waitpid()` to clean up the process.

            // We can either check `operator bool` or `OwnsProcess()` here (the latter being more strict). At least one of them is needed,
            //   because `CheckOrWait()` asserts on that. But it does nothing if `bool(*this) == true && !OwnsProcess()` anyway, so it doesn't matter which one we check.

            if (OwnsProcess())
                CheckOrWait(true);
        }

        // Returns true if this instance either owns a process (for non-detached processes), or was created by successfully starting a detached process.
        [[nodiscard]] explicit operator bool() const {return state.pid != 0;}

        // This is zero for null processes.
        [[nodiscard]] Proc::Pid Pid() const {return state.pid;}

        // Returns true if this instance owns a process handle. This is a subset of `operator bool`.
        // This is only true for non-background processes, which we didn't observe to exit yet. This means the destructor will have to do something to clean up the process handle/pid that we own.
        [[nodiscard]] bool OwnsProcess() const
        {
            #ifdef _WIN32
            return state.process_handle != INVALID_HANDLE_VALUE;
            #else
            return state.owns_pid;
            #endif
        }

        // Returns true if this is a background process. See `Params::Detached()` for more details.
        [[nodiscard]] bool IsBackground() const
        {
            return state.exit_reason && std::holds_alternative<Proc::ExitReason::Detached>(state.exit_reason->var);
        }

        // Update the process state. Check `ExitReason()` and `HasError()` after this.
        void UpdateState()
        {
            CheckOrWait(false);
        }

        // Wait until the process exits.
        // Note! This can deadlock if you have pipes open to this process, because you need to be manually poking those pipes.
        void BlockUntilExit()
        {
            CheckOrWait(true);
        }

        // Returns the exit reason of the process, or false if it's not known to be exited.
        [[nodiscard]] const std::optional<Proc::ExitReason> &ExitReason() const
        {
            return state.exit_reason;
        }

      private:
        void CheckOrWait(bool wait)
        {
            // Intentionally don't check `HasError()`. If something random has failed, we should still be able to `waitpid()` the process to clean it up.

            // Complain about null instances.
            if (!*this)
            {
                // Assert instead of writing to `state.error`, this makes more sense to me.
                assert(false && "Attempt to wait for a null instance.");
                return;
            }

            // Do nothing if we don't own a PID. This rejects background processes, and also repeated waits.
            if (!OwnsProcess())
                return;

            #ifdef _WIN32
            auto wait_result = WaitForSingleObject(state.process_handle, wait ? INFINITE : 0);
            // If wait errored...
            if (wait_result == WAIT_FAILED)
            {
                // Mark the process as exited, I guess.
                // So that nothing gets blocked on the user side, waiting for it to exit.
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Error{});
                return;
            }
            // If wait says the process is still running...
            if (wait_result != WAIT_OBJECT_0)
            {
                assert(wait); // Should only be possible if `wait == true`.
                return;
            }

            // At this point we know the process has exited.

            // First, get the exit code.
            DWORD exit_code = DWORD(-1);
            // Don't check this for errors immediately, close the handle first.
            bool exit_code_ok = GetExitCodeProcess(state.process_handle, &exit_code) != 0;
            DWORD exit_code_error = exit_code_ok ? 0 : GetLastError();

            // Release the process handle, for consistency with POSIX. The destructor also relies on this function doing it.
            CloseHandle(state.process_handle);
            state.process_handle = INVALID_HANDLE_VALUE;

            // Now check that `exit_code` was obtained successfully.
            if (!exit_code_ok)
            {
                error_string = "`GetExitCodeProcess()` failed: " + detail::WinApiErrorToString(exit_code_error);
                state.exit_reason = Proc::ExitReason(ExitReason::Error{}); // The process is considered exited.
                return;
            }

            // Set the exit reason.
            static_assert(sizeof(int) == sizeof(DWORD)); // I don't feel like making `ExitReason::Code` store `DWORD` on Windows. And the negative values for things like `0xC0000005` are more recongizable to me.
            state.exit_reason = Proc::ExitReason(ExitReason::Code{int(exit_code)});

            #else
            int status = 0;
            int wait_result = waitpid(state.pid, &status, wait ? 0 : WNOHANG);
            // If wait errored...
            // It returns `-1` on error.
            if (wait_result < 0)
            {
                error_string = std::string("`waitpid()` failed: ") + std::strerror(errno);
                // Mark the process as exited, I guess.
                // So that nothing gets blocked on the user side, waiting for it to exit.
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Error{});
                return;
            }
            // If wait says the process is still running...
            if (wait_result == 0)
            {
                assert(wait); // Should only be possible if `wait == true`.
                return;
            }

            // At this point we know the process has exited.

            // We can no longer wait on this PID. We preserve it for posterity though.
            state.owns_pid = false;

            // But why did it exit?
            if (WIFEXITED(status))
            {
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Code{WEXITSTATUS(status)});
                return;
            }
            if (WIFSIGNALED(status))
            {
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Signal_Posix{
                    WTERMSIG(status)
                    #if EM_PROC_CAN_DETECT_CORE_DUMPS
                    // I know that trailing commas are ignored. Want to do it this way.
                    , bool(WCOREDUMP(status))
                    #endif
                });
                return;
            }

            // Some unknown reason.
            state.exit_reason = Proc::ExitReason(Proc::ExitReason::Other_Posix{status});
            #endif
        }

        struct State
        {
            Proc::Pid pid = 0;

            #ifdef _WIN32
            // There's some weirdness with what counts as a valid handle. `INVALID_HANDLE_VALUE` is basically `(HANDLE)-1`, but `nullptr` also seems to be invalid. Using `INVALID_HANDLE_VALUE` seems better to me.
            HANDLE process_handle = INVALID_HANDLE_VALUE;
            #else
            // Do we need to `waitpid()` on the PID to clean it up?
            bool owns_pid = false;
            #endif

            std::optional<Proc::ExitReason> exit_reason;
        };
        State state;
    };
}


// Implement `std::hash` functions declared earlier:

inline std::size_t std::hash<em::Proc::BasicIoStream>::operator()(const em::Proc::BasicIoStream &stream) const
{
    return std::hash<em::Proc::BasicIoStream::handle_t>{}(stream.Handle());
}
