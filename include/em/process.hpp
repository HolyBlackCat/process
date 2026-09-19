#pragma once

#include <cassert>
#include <functional>
#include <initializer_list>
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
    // This is primarily for internal use, and exposed as a courtesy. You can also use `class NativeString` defined below.
    // Converts between `std::string` and `std::wstring` in both directions.
    // If the argument is `std::basic_string<T>` as opposed to `std::basic_string_view<T>`, you can cast it to `std::basic_string_view(...)` via CTAD to avoid having to specify `Char`.
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
        // Fun fact: having custom `==`s below forces us to explicitly default this `==`. Defaulting the `<=>` is no longer enough because of those other `==`s.
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

        #ifdef _WIN32

        template <typename T>
        struct StringToCharType {using type = typename T::value_type;};
        template <>
        struct StringToCharType<NativeString> {using type = NativeChar;};

        // Combines multiple command-line arguments into one string. Can operate either on wide or on narrow strings, doesn't matter.
        // `executable` is the optional override for the executable name, that's otherwise taken from the first argument. We may modify it, and may reset it to null if needed. (Currently only resetting.)
        // `executable` is not an `std::optional<std::basic_string_view<...>>` for convenience, since we always pass an owning optional string to it.
        // `executable` has to store a `NativeString` or a `std::string` or a `std::wstring`.
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
        template <typename String, typename Char = typename StringToCharType<String>::type>
        [[nodiscard]] bool AssembleCommandLine(
            std::optional<String> &executable,
            std::size_t num_args,
            bool batch_prepend_cmd,
            int batch_safety,
            std::optional<String> &out_command,
            std::string &out_error,
            auto &&get_arg)
        {
            // This lambda returns the `i`th argument converted to `std::basic_string_view<...>`.
            auto GetArgView = [&](std::size_t i)
            {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(get_arg(std::size_t{}))>, NativeString>)
                    return std::basic_string_view(get_arg(i).native);
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
                if constexpr (std::is_same_v<String, NativeString>)
                    return std::basic_string_view(executable->native);
                else
                    return std::basic_string_view(*executable);
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
                    if (!flag_v && *flag_v)
                        bad_chars_array[bad_chars_array_pos++] = '!';

                    // If `batch_safety >= 2`, or if `/e` is unknown or false.
                    if (batch_safety >= 2 || (!flag_e && !*flag_e))
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

            // 6. Batch/cmd name validation.
            if (num_args == 0 && ArgContainsBadChars(GetExecutableView()))
                return false;


            // Don't read `exe_name` beyond this point, it might get invalidated.

            out_command = {};

            auto GetWritableOutCommand = [&]() -> auto &
            {
                assert(out_command);
                if constexpr (std::is_same_v<String, NativeString>)
                    return out_command->native;
                else
                    return *out_command;
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
                                out += std::basic_string_view(std::to_array<Char>({' ','/','e',':','o','n'}));
                            if (!flag_v)
                                out += std::basic_string_view(std::to_array<Char>({' ','/','v',':','o','f','f'}));
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
                    if (is_first_arg && (!executable || ShouldQuoteEntireCommand()))
                    {
                        quote = true;
                    }
                    else
                    {
                        static constexpr Char quotable_chars_array[] = {' ','\t','"', /*batch only:*/ '<','>','&','|','(',')','[',']','{','}','^','=',';','%','!','\'','+','`','~'};
                        const std::basic_string_view<Char> quotable_chars = arg_needs_cmd_handling ? quotable_chars_array : std::basic_string_view<Char>(quotable_chars_array, 3);

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

            // 7. Prepend CMD invocation to Batch files.
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
                out_command.emplace();
                if constexpr (std::is_same_v<Char, wchar_t>)
                    out_command = L"\"cmd\" /d /e:on /v:off /s /c \"";
                else
                    out_command =  "\"cmd\" /d /e:on /v:off /s /c \"";

                // If we have no user-provided arguments, write the executable as an argument.
                // I figured it's easier to use `WriteArgument()` here.
                if (num_args == 0)
                    WriteArgument(GetExecutableView());

                // Either way, reset `executable`.
                executable = {};

                is_cmd = true;
                is_batch = false;
            }

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

        #ifdef _WIN32
        // How to handle special symbols in CMD and batch arguments.
        enum class CmdBatchSafety_Win
        {
            // Always error on `%`, because while it's possible to escape, certain batch files still allow it to be misused even when escaped. (E.g. those that run nested `cmd /c ...` instances.)
            // Only allow `!` if `/v:off` is specified, since otherwise `!` can have special behavior that can be unsafe. (`/v:on` would enable this unsafe behavior, and omitting it would take the default from the registry,
            //   which defaults to off, but we don't check the registry and just don't allow `!` in that case).
            // When `CommandExtras::cmd_batch_override_registry == true`, we add our own `/v:off`, making sure `!` is allowed by default.
            safe,
            // For `!`, same behavior as `safe`.
            // For `%`, allow and escape it, but only if `/e:on` is specified, which is necessary for the escape to work. (`e:off` would break our escape mechanism. Omitting it would take the default from the registry,
            //   which defaults to on, but we don't check the registry and just don't allow `%` in that case).
            // If `/e:on` is not specified, will error on `%`.
            // When `CommandExtras::cmd_batch_override_registry == true`, we add our own `/e:on`, making sure `%` is allowed by default.
            relaxed,
            // Allow both `%` and `!` unconditionally, and don't escape them.
            unsafe,

            // Note, the numbering of those doens't match what `detail::AssembleCommandLine()` accepts, but I really want `safe` to have the value 0, to be the default value.
        };
        #endif

        struct CommandExtras
        {
            #ifdef _WIN32
            // When running `cmd ... /c`, add some flags at `...` to ignore certain registry overrides, ensuring sane consistent behavior even if the user has something weird in the registry.
            // Will also prepend `cmd ... /c` when running batch files to prepend the same flags.
            bool cmd_batch_override_registry_win = true;

            // Defined unconditionally for simplicity. Because of that we don't suffix it `_win`.
            CmdBatchSafety_Win cmd_safety_win = CmdBatchSafety_Win::safe;
            #endif
        };

        // Set the command to execute, and its arguments.
        // If `executable` is specified, it replaces `argv[0]` as the program to execute. The original `argv[0]` is then only passed to the program's `main`.
        // On Windows, specifying `executable` ignores `PATH` and disables the implicit `.exe` extension, and instead either uses the exact path,
        //   or searches the current directory. (`argv[0]` also searches in the current directory on Windows.)
        // Also on Windows `executable` allows passing overly long executable names. (Those must be prefixed with `\\?\` and canonicalized to use `\` instead of `/`,
        //   don't use multiple adjacent `\`, don't use `.` or `..` directory names, etc. But they remain case-insensitive.)
        std::optional<NativeString> executable;
        Params &Command(std::vector<NativeString> argv, std::optional<NativeString> executable = {}, CommandExtras extras = DefaultCommandExtras())
        {
            #ifdef _WIN32
            detail_SetCommand_Win(executable, extras, argv.size(), [&](std::size_t i) -> const auto & {return argv[i];});
            #else
            (void)extras;
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
        // Note, the `executable is narrow here to match `argv`. This is to simplify our implementation, since the command line is assembled narrow here,
        //   and we might need to copy `executable` into the command line. I guess we could overload this with a `NativeString` executable, and narrow that
        //   ourselves, but it's probably not worth it. You can do it yourself if you need.
        Params &Command(const char *const *argv, std::optional<std::string> executable = {}, CommandExtras extras = DefaultCommandExtras())
        {
            #ifdef _WIN32
            detail_SetCommand_Win(executable, extras, detail::PtrArraySize(argv), [&](std::size_t i) {return argv[i];});
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
        // This ignores `extras.cmd_batch_mode`, but we're still passing `extras` for consistency.
        Params &CommandString_Win(std::optional<NativeString> command, std::optional<NativeString> executable = {}, CommandExtras extras = DefaultCommandExtras())
        {
            (void)extras;
            state.cmd_string = std::move(command);
            state.exe_path = std::move(executable);
            return *this;
        }

        // Windows special: Command as a wide `argv`. Unlike the narrow `argv` version on POSIX, this is consumed immediately and can't dangle.
        // On Windows, if `executable` is specified, then `argv` can be null. It's unclear if this does anything different compared to just passing 0 arguments.
        Params &Command_Win(const wchar_t *const *argv, std::optional<NativeString> executable, CommandExtras extras = DefaultCommandExtras())
        {
            detail_SetCommand_Win(executable, extras, detail::PtrArraySize(argv), [&](std::size_t i) {return argv[i];});
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

        #ifdef _WIN32
        // If `String` is `NativeString`, then `get_arg(i)` should return that, possibly by reference.
        // Otherwise `String` has to be `std::basic_string<T>`, then `get_arg(i)` should return something convertible to `std::basic_string_view<T>`.
        // This is a little wrapper for `detail::AssembleCommandLine()`. Maybe we could merge them, but it's currently easier not to.
        template <typename String>
        void detail_SetCommand_Win(std::optional<String> executable, CommandExtras extras, std::size_t num_args, auto &&get_arg)
        {
            bool ok = false;

            auto MakeCommand = [&](auto &out_command)
            {
                ok = detail::AssembleCommandLine(
                    executable,
                    num_args,
                    extras.cmd_batch_override_registry_win,
                    extras.cmd_safety_win == CmdBatchSafety_Win::unsafe ? 0 : extras.cmd_safety_win == CmdBatchSafety_Win::relaxed ? 1 : 2,
                    out_command,
                    state.error,
                    decltype(get_arg)(get_arg)
                );
            };

            if constexpr (std::is_same_v<String, NativeString>)
            {
                MakeCommand(state.cmd_string);
            }
            else
            {
                std::optional<String> out_command; // Have to use a temporary output variable of the specific type.
                MakeCommand(out_command);
                state.cmd_string = std::move(out_command); // This correctly handles null optionals.
            }

            // Write the updated executable name.
            if (ok)
                state.exe_path = std::move(executable); // This correctly handles null optionals.
        }
        #endif

        [[nodiscard]] static CommandExtras DefaultCommandExtras() {return {};} // Make Clang happy.

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
