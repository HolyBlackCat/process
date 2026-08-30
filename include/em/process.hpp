#pragma once

#include <cassert>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#  include <type_traits>
#  pragma push_macro("NOMINMAX")
#  pragma push_macro("WIN32_LEAN_AND_MEAN")
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  pragma pop_macro("NOMINMAX")
#  pragma pop_macro("WIN32_LEAN_AND_MEAN")
#else
#  include <cstring> // For `std::strerror()`.
#  include <spawn.h>
#  include <sys/types.h> // For `pid_t`.
#  include <sys/wait.h> // For `waitpid()`.
#  include <unistd.h>
#  if __APPLE__
#    include <crt_externs.h> // For `_NSGetEnviron()`.
#  endif
#endif

// Changes here relative to SDL:
// * Better error reporting from starting background processes. See: https://github.com/libsdl-org/SDL/issues/16188
// * Use return values instead of `errno` in a few places. But it seems in glibc those functions do set errno, even though it's not documented in the manual, so I'm not sure this ever matters.
// * Don't bother with android-specific code to obtain extra env variables from the application manifest, whatever that is.
// * Refuse to use `kill(pid, 0)` to wait for background processes. Since PIDs can be recycled, this seems unreliable.
// * Added "have core dump" check when a process stops due to a signal.
// * On Windows, the `CREATE_NO_WINDOW` flag that disables console allocation is not implied by process background-ness. It doesn't seem terribly useful, and we expose the flags directly on Windows.

// Differences to reproc:
// * We don't try to use sockets as pipes as reproc does on Windows. Seems hacky, and they have several suspicious bug reports that look like they could be caused by those, that they didn't respond to.
//   They did that to support polling (so checking which pipes are ready without actually reading/writing to them), but we can use IO completion ports instead. Those seem to force you to queue an operation,
//     instead of just reporting the pipe status (like polling does on Linux), so the API has to be designed around it.

#ifdef _WIN32
// Usage: `EM_PROC_NATIVE("blah")`.
// Makes a string literal wide on Windows, and returns it as is on other platforms.
#define EM_PROC_NATIVE(x) L"" x ""
#else
// Usage: `EM_PROC_NATIVE("blah")`.
// Makes a string literal wide on Windows, and returns it as is on other platforms.
#define EM_PROC_NATIVE(x) "" x ""
#endif

namespace em::Proc
{
    #ifdef _WIN32
    using NativeChar = wchar_t;
    #else
    using NativeChar = char;
    #endif

    // This is always some integer type.
    #ifdef _WIN32
    using Pid = DWORD;
    #else
    using Pid = pid_t;
    #endif

    #ifdef _WIN32
    // This is primarily for internal use, and exposed as a courtesy. Prefer `class NativeString` defined below.
    // Converts between `std::string` and `std::wstring` in both directions.
    // By default, replaces invalid characters in the input with placeholders.
    // If `success` is specified, instead returns an empty string if the input has invalid characters, and writes false to `success`. On success, writes true.
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
            assert(success && "`MultiByteToWideChar` failed when calculating the buffer size.");

            if (success)
                *success = false;
            return ret;
        }

        ret.resize(std::size_t(expected_size)); // `expected_size` only includes space for null-terminator if the input size included it, so not in our case.

        int actual_size = Convert(ret.data(), expected_size);
        if (actual_size == 0)
        {
            // If `!success`, this shouldn't be possible. Then assert.
            assert(success && "`MultiByteToWideChar` failed when encoding.");

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
    #endif

    // Stores `std::wstring` on Windows and `std::string` on other platforms, and provides functions to convert that to/from UTF-8.
    struct NativeString
    {
        using UnderlyingType = std::basic_string<NativeChar>;

        // `std::wstring` on Windows, `std::string` on POSIX.
        UnderlyingType native;


        // Constructs an empty string.
        [[nodiscard]] NativeString() {}

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
        // See the Windows version for what `success` does.
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
            return ConvertString_Win(std::wstring_view(native), success);
        }
        #else
        // Returns the stored string.
        // Never fails on POSIX, so if `success` is specified, always writes true to it. See the Windows comment for how this can fail on Windows.
        [[nodiscard]] const std::string &get(bool *success = nullptr) const
        {
            if (success)
                *success = true;
            return native;
        }
        #endif

        // Compare with itself.
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

        #ifdef _WIN32

        // Combines multiple command-line arguments into one string. Can operate either on wide or on narrow strings, doesn't matter. Always returns a wide string.
        // `executable` is the optional override for the executable name, that's otherwise taken from the first argument. We don't paste it into the result,
        //   but need it to know if this is a batch file or not, SDL escapes those differently.
        // `executable` is not an `std::optional<std::basic_string_view<...>>` for convenience, since we always pass `std::optional<NativeString>` to it.
        // `num_args` is the number of arguments, including the program name.
        // `get_arg(i)` is called for `0 <= i < num_args`, and must return either `NativeString` or something convertible to `std::string_view` or `std::wstring_view`.
        // `get_arg` can be called multiple times for the same index, make sure that works and is fast.
        // If `force_batch_escape` is specified, it controls whether or not we apply additional batch-style escaping to the arguments.
        // By default it's enabled only when `argv[0]` (or `executable` instead, if specified) ends in `.bat` or `.cmd` (case-insensitive),
        //   but passing `force_batch_escape` lets you force enable or disable it.
        [[nodiscard]] NativeString AssembleCommandLine(const std::optional<NativeString> &executable, std::optional<bool> force_batch_escape, std::size_t num_args, auto &&get_arg)
        {
            if (num_args == 0)
                return {}; // Must special-case this because we might call `get_arg(0)` to get the executable name.

            // What does the lambda return.
            using LambdaReturnType = std::remove_cvref_t<decltype(get_arg(std::size_t{}))>;

            // This lambda returns the `i`th argument converted to `std::basic_string_view<...>`.
            auto get_arg_view = [&](std::size_t i)
            {
                if constexpr (std::is_same_v<LambdaReturnType, NativeString>)
                    return std::basic_string_view(get_arg(i).native);
                else
                    return std::basic_string_view(get_arg(i));
            };

            // Now we can determine the character type of the input arguments;
            using Char = decltype(get_arg_view(0))::value_type;

            auto EndsWithExt = []<typename T>(
                std::basic_string_view<T> view,
                std::type_identity_t<T> a,
                std::type_identity_t<T> b,
                std::type_identity_t<T> c,
                std::type_identity_t<T> A,
                std::type_identity_t<T> B,
                std::type_identity_t<T> C
            )
            {
                if (view.size() < 4)
                    return false;
                view = view.substr(view.size() - 4);
                if (view[0] != '.')
                    return false;
                return
                    (view[1] == a || view[1] == A) &&
                    (view[2] == b || view[2] == B) &&
                    (view[3] == c || view[3] == C);
            };

            auto EndsWithBatchExt = [&]<typename T>(std::basic_string_view<T> view)
            {
                return EndsWithExt(view, 'b','a','t', 'B','A','T') || EndsWithExt(view, 'c','m','d', 'C','M','D');
            };

            // Is this a `.bat`/`.cmd` file? We only set this when using the stock escaping mode.
            const bool is_batch_file =
                force_batch_escape ? *force_batch_escape :
                executable ? EndsWithBatchExt(std::basic_string_view(executable->native)) : EndsWithBatchExt(get_arg_view(0));

            // Handles `i`th command line argument.
            // If `Write == false`, returns the necessary string size to encode it. Then the `out` argument must not be specified.
            // If `Write == true`, appends the argument to `out` and returns nothing.
            auto HandleArg = [&]<bool Write>(std::size_t i, std::conditional_t<Write, std::basic_string<Char> &, std::nullptr_t> out = {}) -> std::conditional_t<Write, void, std::size_t>
            {
                // Escaping algorithm from here: https://learn.microsoft.com/en-us/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way

                std::size_t ret = 0;

                auto WriteChar = [&](Char ch)
                {
                    if constexpr (Write)
                        out += ch;
                    else
                        ret++;
                };

                // Argument separator.
                if (i != 0)
                    WriteChar(' ');

                const std::basic_string_view<Char> arg = get_arg_view(i);

                const bool batch_escape_this_arg = i > 0 && is_batch_file;

                // The original article doesn't list here `\r`, but SDL does.
                // But from my experiments, out of those, only ` ` and `\t` are actually necessary here, the rest is courtesy. And since we're doing courtesy, why not add `\r` too.
                static constexpr Char quoted_chars[] = {' ', '\r', '\n', '\t', '\v'};
                const bool quote = arg.empty() || arg.find_first_of(quoted_chars) != std::size_t(-1);

                // Open the quote.
                if (quote)
                {
                    if (batch_escape_this_arg)
                        WriteChar('^');
                    WriteChar('"');
                }

                for (auto it = arg.begin(), end = arg.end(); it != end; it++)
                {
                    const auto ch = *it;

                    if (ch == '\\')
                    {
                        std::size_t num_slashes = 1;
                        while (it + 1 != end && it[1] == '\\')
                        {
                            num_slashes++;
                            ++it;
                        }

                        if (it + 1 != end ? it[1] == '"' : quote)
                        {
                            // If there's a quote after all those slashes, write 2*N+1 slashes. Leave the quote in the buffer to be processed at the next step.

                            for (std::size_t i = 0; i < num_slashes; i++)
                            {
                                WriteChar('\\');
                                WriteChar('\\'); // Again.
                            }
                            WriteChar('\\'); // And another one.
                        }
                        else
                        {
                            // If there's no quote after the slashes, just write the slashes as is.
                            for (std::size_t i = 0; i < num_slashes; i++)
                                WriteChar('\\');
                        }

                        continue;
                    }

                    // For batch, prepend some special characters with `^`.
                    // The list is straight from the article.
                    if (batch_escape_this_arg && (ch == '(' || ch == ')' || ch == '%' || ch == '!' || ch == '^' || ch == '"' || ch == '<' || ch == '>' || ch == '&' || ch == '|'))
                        WriteChar('^');

                    WriteChar(ch);
                }

                // Close the quote.
                if (quote)
                {
                    if (batch_escape_this_arg)
                        WriteChar('^');
                    WriteChar('"');
                }

                if constexpr (!Write)
                    return ret;
            };

            std::size_t needed_size = 0;
            for (std::size_t i = 0; i < num_args; i++)
                needed_size += HandleArg.template operator()<false>(i);

            // Intentionally assemble the result in a possibly non-wide string. Then `return ret;` converts it to a wide string.
            // This way we only have to call WinAPI once, and not per argument.
            std::basic_string<Char> ret;
            ret.reserve(needed_size);
            for (std::size_t i = 0; i < num_args; i++)
                HandleArg.template operator()<true>(i, ret);

            return ret;
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
                ret += *env++;
                ret += '\0'; // Intentional even after the last element. We want `\0\0` at the end.
            }

            return ret;
        }

        [[nodiscard]] inline std::string GetLastWinApiErrorMessage()
        {
            // Get the error code first, since we might clobber it when trying to get the error message in English if it's not available, see below.
            auto last_error = GetLastError();

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
                    last_error,
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

                    std::string ret = ConvertString_Win(ret_wide);
                    std::erase(ret, '\r'); // For a good measure.

                    return ret;
                }
            }

            // If the message wasn't available in any language:
            return std::to_string(last_error) + " (no error message is available)";
        }

        #else

        #ifndef __APPLE__
        extern "C" char **environ;
        #endif

        [[nodiscard]] inline const char *const *GetEnviron()
        {
            #ifdef __APPLE__
            // Use `*_NSGetEnviron()` instead of `environ`.
            // `environ` does kinda work on Macs, but `man environ` on Macs says that it doesn't work in shared libraries and in bundles (whatever those are), but this function works.
            return *_NSGetEnviron();
            #else
            return environ;
            #endif
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
                // Even `putenv` is said to have a special case for the missing `=` on glibc, which causes it to unsert that variable: https://linux.die.net/man/3/putenv
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
                [](const Background &) -> std::string {return "Detached background process.";},
                [](const Code &elem)   -> std::string {return "Exited with code " + std::to_string(elem.code) + ".";},
                #ifndef _WIN32
                [](const Signal_Posix &elem) -> std::string {std::string ret = "Exited due to signal " + std::to_string(elem.signal) + "."; if (elem.core_dumped) ret += " Core dumped."; return ret;},
                [](const Other_Posix &elem)  -> std::string {return "Exited for an unknown reason: " + std::to_string(elem.status) + ".";},
                #endif
                [](const Error &)      -> std::string {return "Library error.";},
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
            bool core_dumped = false; // Do we have a core dump?
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

        // Don't know if exited or not, this is a background process.
        struct Background
        {
            friend auto operator<=>(Background, Background) = default;
        };

        // Something is wrong with our library. No error message here, check `Process::ErrorMessage()` for more details.
        struct Error
        {
            friend auto operator<=>(Error, Error) = default;
        };

        using Var = std::variant<
            // `Background` is listed first because of the dumb default constructibility checks failing for nested classes with member initializers.
            Background,
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

    // Process creation params.
    class Params
    {
      public:
        // Call some setters after this.
        Params() noexcept
        {
            #ifndef _WIN32
            // Those are dirt cheap to initialize, so no separate constructor.

            if (int spawn_res = posix_spawnattr_init(&state.spawn_attr))
            {
                // No useful messages for us to emit here, so just write the number.
                // The manual doesn't mention this setting `errno`, so we use the return value instead. At least for `posix_spawn`, glibc sets the errno anyway, even though the manual doesn't say so, but for this function I can't check, because it never fails in glibc.
                state.error = "`posix_spawnattr_init` failed: " + std::to_string(spawn_res);
                return;
            }
            state.spawn_attr_alive = true;

            if (int spawn_res = posix_spawn_file_actions_init(&state.spawn_fa))
            {
                // No useful messages for us to emit here, so just write the number.
                // The manual doesn't mention this setting `errno`, so we use the return value instead. At least for `posix_spawn`, glibc sets the errno anyway, even though the manual doesn't say so, but for this function I can't check, because it never fails in glibc.
                state.error = "`posix_spawn_file_actions_init` failed: " + std::to_string(spawn_res);
                return;
            }
            state.spawn_fa_alive = true;
            #endif
        }

        // Non-movable for now, this is simpler to implement.
        Params(const Params &) = delete;
        Params &operator=(const Params &) = delete;

        ~Params()
        {
            #ifndef _WIN32
            if (state.spawn_attr_alive)
                posix_spawnattr_destroy(&state.spawn_attr);
            if (state.spawn_fa_alive)
                posix_spawn_file_actions_destroy(&state.spawn_fa);
            #endif
        }

        struct CommandExtras
        {
            // If specified, it replaces `argv[0]` as the program to execute. The original `argv[0]` is then only passed to the program's `main`.
            // On Windows, specifying `executable` ignores `PATH` and `PATHEXT`, and instead either uses the exact path, or searches the current directory.
            std::optional<NativeString> executable;

            // This is only used on Windows. Not `#ifdef`ing it for simplicity. This is ignored by overloads that take a single string, as opposed to argument array, since the string is assumed to be pre-escaped.
            // By convention, arguments of batch files (`.bat` or `.cmd`) need additional escaping. Unlike POSIX shells, the batch interpreter apparently can't escape the arguments by itself,
            //   so either you give it pre-escaped ones, or your batch files break in mysterious ways (and possibly vulnerable ways, if the arguments are user-provided).
            // Our default behavior is to batch-escape if the extension of `argv[0]` (or `executable` instead, if specified) ends with `.bat` or `.cmd` (case-insensitive).
            // This variable overrides that logic.
            // Note that the extension heuristic is unreliable because of `PATHEXT`, which lets you omit extensions. Even if you run e.g. `foo.exe`, it could resolve to `foo.exe.cmd` if that exists, or whatever.
            // So to be safe, you should unset `PATHEXT` before starting processes, or set the `executable` parameter (which ignores `PATHEXT`).
            std::optional<bool> batch_escaping;
        };

        // Set the command to execute, and its arguments.
        Params &Command(std::vector<NativeString> argv, CommandExtras extras = {})
        {
            #ifdef _WIN32
            state.cmd_string = detail::AssembleCommandLine(extras.executable, extras.batch_escaping, argv.size(), [&](std::size_t i) -> const auto & {return argv[i];});
            state.exe_path = std::move(extras.executable);
            #else
            state.cmd_argv_storage = std::move(argv);
            std::size_t argc = state.cmd_argv_storage.size();
            // A direct resize should be faster than reserve?
            state.cmd_argv_ptrs_storage.resize(argc + 1); // +1 for the terminating null pointer.
            for (std::size_t i = 0; i < argc; i++)
                state.cmd_argv_ptrs_storage[i] = state.cmd_argv_storage[i].native.c_str();
            state.cmd_argv_ptrs_storage.back() = nullptr; // Zero explicitly in case the vector wasn't empty before.
            state.cmd_argv = state.cmd_argv_ptrs_storage.data();

            state.exe_path = std::move(extras.executable);
            #endif
            return *this;
        }
        // This version doesn't copy `argv` on POSIX, make sure it doesn't dangle until the process starts.
        // Naming this `Command` means that `Command({})` will call this overload and not the vector one. This isn't a big deal.
        Params &Command(const char *const *argv, CommandExtras extras = {})
        {
            #ifdef _WIN32
            if (argv)
                state.cmd_string = detail::AssembleCommandLine(extras.executable, extras.batch_escaping, detail::PtrArraySize(argv), [&](std::size_t i) {return argv[i];});
            else
                state.cmd_string.reset();
            state.exe_path = std::move(extras.executable);
            #else
            state.cmd_argv_storage.clear();
            state.cmd_argv_ptrs_storage.clear();
            state.cmd_argv = argv;

            state.exe_path = std::move(extras.executable);
            #endif

            return *this;
        }

        #ifdef _WIN32
        // Windows special: Command as a single string. Like `argv`, `command_str` must start with the executable name, which can be overridden with `executable`.
        // On Windows, if `executable` is specified, then `command` can be null. It's unclear if this does anything different compared to just passing 0 arguments.
        Params &CommandString_Win(std::optional<NativeString> command, CommandExtras extras = {})
        {
            state.cmd_string = std::move(command);
            state.exe_path = std::move(extras.executable);
            return *this;
        }

        // Windows special: Command as a wide `argv`. Unlike the narrow `argv` version on POSIX, this is consumed immediately and can't dangle.
        // On Windows, if `executable` is specified, then `argv` can be null. It's unclear if this does anything different compared to just passing 0 arguments.
        Params &Command_Win(const wchar_t *const *argv, CommandExtras extras = {})
        {
            if (argv)
                state.cmd_string = detail::AssembleCommandLine(extras.executable, extras.batch_escaping, detail::PtrArraySize(argv), [&](std::size_t i) {return argv[i];});
            else
                state.cmd_string.reset();
            state.exe_path = std::move(extras.executable);
            return *this;
        }
        #endif


        // Set the environment variables. This overrides all variables. Use `CurrentEnv()` to get the variables of the current process, if you only want to modify some.
        // If this is not called, the default behavior is to use the variables of the current process, reading them right when starting the new process (not when constructing `Params`).
        Params &Env(EnvMap env_vars)
        {
            // Validate.
            for (const auto &elem : env_vars)
            {
                // `=` in the key.
                // If we wanted to check both `=` and `\0` in one line, we could do `.find_first_of(std::basic_string_view(EM_PROC_NATIVE("="), 2))`, but I'd rather have separate nice errors.
                if (elem.first.native.find_first_of('=') != std::size_t(-1))
                {
                    // See above for why we don't report the variable name.
                    state.error = "Some environment variables had `=` in the names.";
                    return *this;
                }
                // `\0` in the key.
                if (elem.first.native.find_first_of('\0') != std::size_t(-1))
                {
                    // See above for why we don't report the variable name.
                    state.error = "Some environment variables had null characters in the names.";
                    return *this;
                }
                // `\0` in the value.
                if (elem.second.native.find_first_of('\0') != std::size_t(-1))
                {
                    // See above for why we don't report the variable name.
                    state.error = "Some environment variables had null characters in the values.";
                    return *this;
                }
            }

            #ifdef _WIN32

            std::size_t needed_size = 0;
            for (const auto &elem : env_vars)
                needed_size += elem.first.native.size() + elem.second.native.size() + 2; // +1 for `=` and +1 for the separating `\0`.

            state.env_string.native.clear();
            state.env_string.native.reserve(needed_size); // This way we get `\0\0` at the end, which is exactly what we want.
            for (const auto &elem : env_vars)
            {
                state.env_string.native += elem.first.native;
                state.env_string.native += '=';
                state.env_string.native += elem.second.native;
                state.env_string.native += '\0'; // Intentional even after the last element. We want `\0\0` after the last element.
            }

            state.env_ptr = state.env_string.native.c_str();

            #else

            std::size_t count = env_vars.size();

            state.env_storage.reserve(count);
            for (const auto &elem : env_vars)
                state.env_storage.push_back(elem.first.native + '=' + elem.second.native);

            // A direct resize should be faster than reserve?
            state.env_ptrs_storage.resize(count + 1); // +1 for the terminating null pointer.
            for (std::size_t i = 0; i < count; i++)
                state.env_ptrs_storage[i] = state.env_storage[i].c_str();
            state.env_ptrs_storage.back() = nullptr; // Zero explicitly in case the vector wasn't empty before.

            state.env_ptr = state.env_ptrs_storage.data();
            #endif

            return *this;
        }
        // This version doesn't copy the strings on POSIX, so make sure they don't dangle.
        // This can't be named `Env()` because then `Env({})` would call this overload, rather than passing an empty map, which is error-prone (unlike the similar `Command()` situation).
        Params &EnvPtr(const char *const *env_vars)
        {
            #ifdef _WIN32
            if (env_vars)
                return EnvString_Win(detail::AssembleEnvironmentFromPtr(env_vars));
            else
                return EnvStringPtr_Win(nullptr); // Special-case this to uncustomize the environment, to mirror the POSIX behavior. Why not.
            #else
            state.env_storage.clear();
            state.env_ptrs_storage.clear();
            state.env_ptr = env_vars;
            return *this;
            #endif
        }

        #ifdef _WIN32
        // Windows special: Environment from a single string, of the form `A=B \0 C=D \0 E=F \0\0`.
        Params &EnvString_Win(NativeString env)
        {
            state.env_string = std::move(env.native);
            state.env_ptr = state.env_string.native.c_str();
            return *this;
        }
        // Windows special: Environment from a single non-owning pointer. Same format as `EnvString_Win()`, but can dangle.
        // There's no narrow version, because that's just `EnvString_Win()`.
        Params &EnvStringPtr_Win(const wchar_t *env)
        {
            state.env_string = {};
            state.env_ptr = env;
            return *this;
        }
        // Windows special: From pointer array. Copies the contents, never dangles.
        Params &EnvPtr_Win(const wchar_t *const *env_vars)
        {
            return EnvString_Win(detail::AssembleEnvironmentFromPtr(env_vars));
        }
        #endif


        // This causes us to immediately release the process handle after starting it, so you can't wait for it to terminate and can't get its exit code.
        //   (In theory, on POSIX we could still wait using the PID, using `kill(pid, 0)`. But that seems unreliable, because the PID could be reused by another process. And not very useful in the first place.)
        //
        // This prevents the mandatory wait for the process in the destructor. (Without this, the destructor is forced to wait on POSIX, otherwise we'd leak resources, look up so-called "zombie processes".
        //   And on Windows it doesn't seem to be the case, but we replicate the POSIX behavior for consistency.)
        //
        // Also on POSIX this has a special effect of ensuring that this child won't get the terminal of the current process if the current process dies before the child, so it couldn't be Ctrl+C'ed in that case.
        //
        // What this does on POSIX is called "double forking" of "daemonizing" the new process, see this for more details: https://stackoverflow.com/q/881388/2752075
        // You can still wait for its completion since we know its PID, but enabling this theoretically makes it not reliable anymore, since something could've reused it, I think?
        Params &Background(bool enable = true)
        {
            state.background = enable;
            return *this;
        }

        #ifdef _WIN32
        // Windows special! Add or remove process creation flags, as documented here: https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags
        Params &AddFlags_Win(DWORD flags)
        {
            state.process_flags |= flags;
            return *this;
        }
        Params &RemoveFlags_Win(DWORD flags)
        {
            state.process_flags &= ~flags;
            return *this;
        }
        #endif

        // Sets the current directory (aka working directory).
        // If not specified, the default behavior is to copy the working directory of the current process (when starting the subprocess, not when creating the `Params` instance, just like we do when mirroring environment).
        Params &CurrentDirectory(std::optional<NativeString> working_dir)
        {
            #ifdef _WIN32
            state.working_dir = std::move(working_dir);
            #else
            if (state.spawn_fa_alive)
            {
                // `_np` suffix means "non-portable" and marks experimental functions.
                // Modern glibc has a version of it without the suffix, and so does MacOS. MacOS marks the `_np` version as deprecated.
                // Android NDK doesn't have a non-`_np` version though (at v29, which is what I'm looking at).
                // So I'm using the `_np` version just in case.
                posix_spawn_file_actions_addchdir_np(&state.spawn_fa, working_dir->native.c_str());
            }
            else
            {
                // Silently ignoring this in release builds. Starting the process will catch this being null anyway.
                // Since initializing `posix_spawn_file_actions_t` apparently can't fail on glibc, this should only happen in practice if it's moved from.
                assert(false && "Operating on a null `posix_spawn_file_actions_t`, is this instance moved from?");
            }
            #endif
            return *this;
        }

        // Returns true on a non-null instance.
        // This becomes false if moved from.
        [[nodiscard]] explicit operator bool() const
        {
            #ifdef _WIN32
            return true;
            #else
            return state.spawn_attr_alive && state.spawn_fa_alive;
            #endif
        }

      private:
        struct State
        {
            // Having those in a struct isn't strictly necessary anymore. Leaving it in case we decide to make this movable later.

            // If this is non-empty, the object is in an error state.
            // When we assign to this, we don't immediately destroy the resources we already created.
            // This is easier to implement, and also lets the user check for errors faster.
            std::string error;


            #ifdef _WIN32
            // This is optional if `exe_path` is specified.
            std::optional<NativeString> cmd_string;
            #else
            const char *const *cmd_argv = nullptr;
            std::vector<NativeString> cmd_argv_storage; // Not `std::vector<std::string>` for simplicity, since the user argument comes in this type.
            std::vector<const char *> cmd_argv_ptrs_storage;
            #endif

            // There's no non-owning version of this for pure convenience. Who needs long paths?
            std::optional<NativeString> exe_path;


            #ifdef _WIN32
            const wchar_t *env_ptr = nullptr; // Non-owning.
            NativeString env_string;
            #else
            const char *const *env_ptr = nullptr; // Non-owning.
            std::vector<std::string> env_storage;
            std::vector<const char *> env_ptrs_storage;
            #endif


            bool background = false;

            #ifdef _WIN32
            DWORD process_flags = CREATE_UNICODE_ENVIRONMENT;
            std::optional<NativeString> working_dir; // On POSIX this is baked into `spawn_attr`.
            #endif

            #ifndef _WIN32
            bool spawn_attr_alive = false;
            posix_spawnattr_t spawn_attr{};
            bool spawn_fa_alive = false;
            posix_spawn_file_actions_t spawn_fa{};
            #endif
        };
        State state;

        friend class Process;
    };

    // A single subprocess.
    class Process
    {
      public:
        Process() {}

        Process(const Params &params)
            : Process() // Run the destructor on throw.
        {
            StartProcess(params);
        }

        Process(Params &&params)
            : Process() // Run the destructor on throw.
        {
            StartProcess(std::move(params));
        }

        // This is move-only.
        Process(Process &&other) noexcept : state(std::move(other.state)) {other.state = {};}
        Process &operator=(Process other) noexcept {std::swap(state, other.state); return *this;}

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

        // Returns true if this instance owns a process, or used to own one.
        [[nodiscard]] explicit operator bool() const {return state.pid != 0;}

        // Returns true if this instance is an error state, due to the underlying API failing.
        [[nodiscard]] bool HasError() const {return !state.error.empty();}
        // Returns the error message. If `HasError() == false`, then always returns an empty string.
        [[nodiscard]] const std::string ErrorMessage() const {return state.error;}

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

        // Returns true if this is a background process. See `Params::Background()` for more details.
        [[nodiscard]] bool IsBackground() const
        {
            return state.exit_reason && std::holds_alternative<Proc::ExitReason::Background>(state.exit_reason->var);
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
        void StartProcess(auto &&params_ref)
        {
            const Params &params = params_ref;

            // Mark as background process before doing anything else, so that this information is not lost on error.
            if (params.state.background)
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Background{});

            if (!params.state.error.empty())
            {
                state.error = "Error in parameters: " + params.state.error;
                return;
            }


            #ifdef _WIN32

            if (!params.state.cmd_string && !params.state.exe_path)
            {
                // WinAPI needs at least one.
                state.error = "No command or executable path specified for process.";
                return;
            }

            // Copy or move `cmd_string` from the parameters. `CreateProcessW()` is documented to clobber it (!!), so unlike on POSIX, we can't just `const_cast` the command line.
            // This is the entire reason we have separate constructors for `const Params &` and `Params &&`.
            auto cmd_string_copy = decltype(params_ref)(params_ref).state.cmd_string;

            STARTUPINFOW startup_info{};
            startup_info.cb = sizeof(startup_info);
            // startup_info.dwFlags |= STARTF_USESTDHANDLES;
            // startup_info.hStdInput = INVALID_HANDLE_VALUE;
            // startup_info.hStdOutput = INVALID_HANDLE_VALUE;
            // startup_info.hStdError = INVALID_HANDLE_VALUE;

            struct ProcInfoGuard
            {
                PROCESS_INFORMATION value{};

                ~ProcInfoGuard()
                {
                    // Not sure if the validity checks are needed. The manual doesn't say anything about allowing invalid handles, so probably needed.
                    if (value.hProcess != INVALID_HANDLE_VALUE)
                        CloseHandle(value.hProcess);
                    if (value.hThread != INVALID_HANDLE_VALUE)
                        CloseHandle(value.hThread);
                }
            };
            ProcInfoGuard proc_info;

            // Here if `env_ptr` is not specified, WinAPI copies the environment of this process, which is exactly what we want.
            bool ok = CreateProcessW(
                params.state.exe_path ? params.state.exe_path->native.c_str() : nullptr,
                cmd_string_copy ? cmd_string_copy->native.data() : nullptr,
                nullptr, // Process attributes.
                nullptr, // Thread attributes.
                true, // Inherit handles.
                params.state.process_flags,
                // It's mildly sus that `env_ptr` needs a `const_cast`. The function parameter is of type `void *`. Unlike for the command line, the documentation (at https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
                //   doesn't say that the environment is clobbered, so I think the `const_cast` is fine.
                const_cast<wchar_t *>(params.state.env_ptr),
                params.state.working_dir ? params.state.working_dir->native.c_str() : nullptr,
                &startup_info,
                &proc_info.value
            );
            if (!ok)
            {
                state.error = "`CreateProcessW` failed: " + detail::GetLastWinApiErrorMessage();
                return;
            }

            // Get the pid.
            state.pid = proc_info.value.dwProcessId;


            // Lastly, for non-background processes, preserve the handle.
            if (!params.state.background)
                state.process_handle = std::exchange(proc_info.value.hProcess, INVALID_HANDLE_VALUE);

            #else
            if (!params.state.cmd_argv)
            {
                // We check it here, because we sometimes use `cmd_argv[0]` below and it better not be null.
                state.error = "Null `argv` specified for process.";
                return;
            }

            if (!params)
            {
                // This can only mean moved-from at this point, sinc we checked `params.state.error` earlier.
                assert(false && "Null params struct, was it moved from?");
                state.error = "Trying to create a process from a null params struct.";
                return;
            }

            // This is almost directly copied from SDL:

            // Note the `const_cast` here and on `argv` below. It's needed because the POSIX API takes `char *const *` instead of `const char *const *`, because the C pointer conversion rules are more strict than the C++ ones,
            //   and they figured it would be more convenient. They don't actually modify those strings.
            char *const *env_ptr = const_cast<char *const *>(params.state.env_ptr ? params.state.env_ptr : detail::GetEnviron());
            char *exe_path = const_cast<char *>(params.state.exe_path ? params.state.exe_path->native.c_str() : params.state.cmd_argv[0]);

            if (params.state.background)
            {
                #ifdef __APPLE__ // SDL says:  Apple has vfork marked as deprecated and (as of macOS 10.12) is almost identical to calling fork() anyhow.
                const pid_t pid = fork();
                const char *forkname = "fork";
                #else
                // `vfork` makes us share memory with the parent (unless implemented as `fork`), which means we must be extra careful to not touch anything. See manual: https://linux.die.net/man/3/vfork
                const pid_t pid = vfork();
                const char *forkname = "vfork";
                #endif
                switch (pid)
                {
                  case -1:
                    // Forking failed.
                    state.error = std::string("`") + forkname + "` failed: " + std::strerror(errno);
                    return;

                  case 0:
                    // Forking successful, we're in the new process.

                    // SDL says this detaches us from the original terminal. This creates a new "session" and makes this process its owner: https://linux.die.net/man/2/setsid
                    setsid();

                    // If we `vfork()`ed, it's theoretically unsafe to touch any memory of the parent process, and we touch `state.pid` here.
                    // But the worst that can happen (if `vfork` is implemented as `fork`) is that `state.pid` doesn't propagate to the parent and remains zero there.

                    // Note the use of `_exit()` as opposed to `std::exit()`.
                    // `vfork` manual says we must use this specific function.

                    // The manual doesn't mention `posix_spawnp` setting `errno`. It still does at least in glibc, but it's more correct to use the return value.

                    // Note the `const_cast` here and on `env_ptr` above. It's needed because the POSIX API takes `char *const *` instead of `const char *const *`, because the C pointer conversion rules are more strict than the C++ ones,
                    //   and they figured it would be more convenient. They don't actually modify those strings.

                    if (int error = posix_spawnp(&state.pid, exe_path, &params.state.spawn_fa, &params.state.spawn_attr, const_cast<char **>(params.state.cmd_argv), env_ptr))
                        _exit(error);
                    else
                        _exit(0);

                  default:
                    // Check the exit code of the direct child process. Also must call it to clean it up, otherwise it remains as a zombie.
                    // Note `pid` here, not `state.pid`.
                    // `waitpid` can wait for some other things than process termination, but it seems all of that is opt-in via the flags parameter, and by default it only waits for termination.
                    int status = -1;
                    if (waitpid(pid, &status, 0) < 0)
                    {
                        state.error = std::string("`waitpid` failed: ") + std::strerror(errno);
                        return;
                    }
                    if (WIFEXITED(status)) // Did the process exit normally? Regardless of the exit code.
                    {
                        if (int exit_code = WEXITSTATUS(status))
                        {
                            // We use the exit code to propagate the error code from `posix_spawnp` above.
                            state.error = std::string("`posix_spawnp` failed: ") + std::strerror(exit_code);
                            return;
                        }
                    }
                    else
                    {
                        state.error = "Forked process exited abnormally. Status integer: " + std::to_string(status);
                    }
                    break;
                }
            }
            else
            {
                // Not a background process, just call `posix_spawnp()` normally.

                // The manual doesn't mention `posix_spawnp` setting `errno`. It still does at least in glibc, but it's more correct to use the return value.

                // Note the `const_cast` here and on `env_ptr` above. It's needed because the POSIX API takes `char *const *` instead of `const char *const *`, because the C pointer conversion rules are more strict than the C++ ones,
                //   and they figured it would be more convenient. They don't actually modify those strings.

                if (int error = posix_spawnp(&state.pid, exe_path, &params.state.spawn_fa, &params.state.spawn_attr, const_cast<char **>(params.state.cmd_argv), env_ptr))
                {
                    state.error = std::string("`posix_spawnp` failed: ") + std::strerror(error);
                    return;
                }

                state.owns_pid = true;
            }

            assert(state.pid);
            #endif
        }

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

            // Release the process handle, for consistency with POSIX. The destructor also relies on this function doing it.
            CloseHandle(state.process_handle);
            state.process_handle = INVALID_HANDLE_VALUE;

            #else
            int status = 0;
            int wait_result = waitpid(state.pid, &status, wait ? 0 : WNOHANG);
            // If wait errored...
            // It returns `-1` on error.
            if (wait_result < 0)
            {
                state.error = std::string("`waitpid` failed: ") + std::strerror(errno);
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
                    WTERMSIG(status),
                    #ifdef WCOREDUMP // Manual says to ifdef this: https://linux.die.net/man/2/waitpid
                    bool(WCOREDUMP(status))
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
            // I considered merging this into `ExitReason`, but since I also merged `Background` into that, I'm worried that we'd lose information if the error message replaced that.
            std::string error;

            Proc::Pid pid = 0;

            #ifdef _WIN32
            // There's some weirdness with what counts as a valid handle. `INVALID_HANDLE_VALUE` is basically `(HANDLE)-1`, but `nullptr` also seems to be invalid.
            // Using `INVALID_HANDLE_VALUE` seems better to me.
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
