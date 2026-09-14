#include <em/process.hpp>

#include <stdexcept>
#include <string>

// Tests the command line escaping function.
// `executable` and `command` are inputs, `expected_executable` and `expected_command` are the expected outputs, and `expected_error` is the expected output error.
// `passing_mode_mask` consists of:
// * 0b1000  - CmdBatchMode::normal
// * 0b0100  - CmdBatchMode::relaxed
// * 0b0010  - CmdBatchMode::keep_registry_settings
// * 0b0001  - CmdBatchMode::unsafe
// The modes that are enabled should succeed (with `expected_executable` and `expected_command`), and the other modes should fail (with `expected_error`).
// If all 4 bits in `passing_mode_mask` are set or all are unset, you shouldn't specify the test values for the opposite result.
// All 4 modes are tested by default. You can skip testing some of them by passing the next 4 bits: `0bXXXXyyyy`. If any of `XXXX` are set, only those modes are tested.
//   Not specifying any `XXXX` defaults to `1111`.
void CheckEscaping(
    std::optional<std::string> executable,
    std::vector<std::string> command,
    int passing_mode_mask,
    std::optional<std::string> expected_executable,
    std::optional<std::string> expected_command,
    std::string expected_error = "",
    int line = __builtin_LINE()
)
{
    // Converts any of: `std::string`, `std::wstring`, `NativeString` to `std::string`.
    // Returns `std::string` either by value or by const ref.
    static constexpr auto StrToNarrowStr = []<typename T>(const T &str) -> decltype(auto)
    {
        if constexpr (std::is_same_v<T, em::Proc::NativeString>)
            return str.get();
        else if constexpr (std::is_same_v<T, std::wstring>)
            return em::Proc::ConvertString_Win(std::basic_string_view(str));
        else
            return str;
    };
    // Same but for optional strings.
    static constexpr auto OptStrToNarrowStr = []<typename T>(const std::optional<T> &opt) -> std::string
    {
        if (opt)
        {
            std::string ret;
            ret += '`';
            ret += StrToNarrowStr(*opt);
            ret += '`';
            return ret;
        }
        else
        {
            return "null";
        }
    };

    auto CheckVariant = [&passing_mode_mask, &line, &expected_error]<typename T>(std::string variant, std::optional<T> executable, std::vector<T> command, std::optional<T> expected_executable, std::optional<T> expected_command)
    {
        for (int i = 0; i < 4; i++)
        {
            // Skip this if necessary.
            if (bool(passing_mode_mask & 0b1111'0000) && !bool(passing_mode_mask & (0b1000'0000 >> i)))
                continue;

            auto mode_enum = em::Proc::Params::CmdBatchMode(i);

            auto MakeErrorPrefix = [&]
            {
                const char *mode_str = nullptr;
                switch (mode_enum)
                {
                    case em::Proc::Params::CmdBatchMode::normal:                 mode_str = "normal";                 break;
                    case em::Proc::Params::CmdBatchMode::relaxed:                mode_str = "relaxed";                break;
                    case em::Proc::Params::CmdBatchMode::keep_registry_settings: mode_str = "keep_registry_settings"; break;
                    case em::Proc::Params::CmdBatchMode::unsafe:                 mode_str = "unsafe";                 break;
                }

                return __FILE__ ":" + std::to_string(line) + ": `AssembleCommandLine` (" + variant + ", " + mode_str + ") ";
            };

            std::optional<T> out_executable = executable; // Must copy this, since the function can change it.
            std::optional<T> out_command;
            std::string out_error;
            bool ok = em::Proc::detail::AssembleCommandLine(
                out_executable,
                command.size(),
                mode_enum == em::Proc::Params::CmdBatchMode::normal || mode_enum == em::Proc::Params::CmdBatchMode::relaxed,
                mode_enum == em::Proc::Params::CmdBatchMode::normal || mode_enum == em::Proc::Params::CmdBatchMode::keep_registry_settings,
                out_command,
                out_error,
                [&](std::size_t i) -> const auto & {return command.at(i);}
            );

            bool should_pass = bool(passing_mode_mask & (0b1000 >> i));

            if (!ok && should_pass)
                throw std::runtime_error(MakeErrorPrefix() + "unexpectedly failed with error: `" + StrToNarrowStr(out_error) + "`.");
            if (ok && !should_pass)
                throw std::runtime_error(MakeErrorPrefix() + "unexpectedly passed, the error was supposed to be: `" + StrToNarrowStr(expected_error) + "`.");

            if (ok && !out_error.empty())
                throw std::runtime_error(MakeErrorPrefix() + "correctly reported success, but wrote an error message: `" + StrToNarrowStr(out_error) + "`.");
            if (!ok && out_error.empty())
                throw std::runtime_error(MakeErrorPrefix() + "correctly reported failure, but wrote no error message: `" + StrToNarrowStr(out_error) + "`.");

            if (ok)
            {
                if (out_executable != expected_executable)
                    throw std::runtime_error(MakeErrorPrefix() + "produced an incorrect executable name, got " + OptStrToNarrowStr(out_executable) + " but expected " + OptStrToNarrowStr(expected_executable) + ".");

                if (out_command != expected_command)
                    throw std::runtime_error(MakeErrorPrefix() + "produced an incorrect command, got " + OptStrToNarrowStr(out_command) + " but expected " + OptStrToNarrowStr(expected_command) + ".");
            }
            else
            {
                if (out_error != expected_error)
                    throw std::runtime_error(MakeErrorPrefix() + "produced an incorrect error message: got `" + out_error + "` but expected `" + expected_error + "`.");
            }
        }
    };

    CheckVariant("std::string", executable, command, expected_executable, expected_command);

    auto ToWstring = [](const auto &s){return em::Proc::NativeString(s).native;};
    auto ToNstring = [](const auto &s){return em::Proc::NativeString(s);};
    std::vector<std::wstring> command_w;
    std::vector<em::Proc::NativeString> command_n;
    for (const auto &arg : command)
    {
        command_w.push_back(ToWstring(arg));
        command_n.push_back(ToNstring(arg));
    }

    CheckVariant("std::wstring", executable.transform(ToWstring), command_w, expected_executable.transform(ToWstring), expected_command.transform(ToWstring));
    CheckVariant("NativeString", executable.transform(ToNstring), command_n, expected_executable.transform(ToNstring), expected_command.transform(ToNstring));
}

int main()
{
    // Some basic sanity checks.
    CheckEscaping({}, {"foo", "bar", "a b"}, 0b1111, {}, R"("foo" bar "a b")", "");
    CheckEscaping("exe", {"foo", "bar", "a b"}, 0b1111, "exe", R"(foo bar "a b")", "");
    CheckEscaping("exe", {}, 0b1111, "exe", {}, "");


    // 1.1. At least one of the two parameters must be specified.
    CheckEscaping({}, {}, 0b0000, {}, {}, "Must specify either an executable or a command.");


    // 1.2. No null characters.
    CheckEscaping(std::string("foo\0bar", 7), {}, 0b0000, {}, {}, "Null character in the executable name.");
    CheckEscaping({}, {std::string("foo\0bar", 7)}, 0b0000, {}, {}, "Null character in the command.");
    CheckEscaping(std::string("foo\0bar", 7), {"foo"}, 0b0000, {}, {}, "Null character in the executable name.");
    CheckEscaping({"foo"}, {std::string("foo\0bar", 7)}, 0b0000, {}, {}, "Null character in the command.");
    CheckEscaping({}, {"a", std::string("foo\0bar", 7), "b"}, 0b0000, {}, {}, "Null character in the command.");
    CheckEscaping({"foo"}, {"a", std::string("foo\0bar", 7), "b"}, 0b0000, {}, {}, "Null character in the command.");
    CheckEscaping(std::string("foo\0bar", 7), {"a", std::string("foo\0bar", 7), "b"}, 0b0000, {}, {}, "Null character in the executable name."); // Here both are null, it's arbitrary which one is reported.


    // 2. Has nothing to test.


    // 3. Error if the executable name (falling back to `argv[0]`) ends with a space or dot.
    CheckEscaping("foo ", {}, 0b0000, {}, {}, "The program name can't end with a space or a dot.");
    CheckEscaping("foo.", {}, 0b0000, {}, {}, "The program name can't end with a space or a dot.");
    CheckEscaping({}, {"foo "}, 0b0000, {}, {}, "The program name can't end with a space or a dot.");
    CheckEscaping({}, {"foo."}, 0b0000, {}, {}, "The program name can't end with a space or a dot.");
    CheckEscaping({"blah"}, {"foo "}, 0b1111, "blah", {R"("foo ")"}); // This is allowed.
    CheckEscaping({"blah"}, {"foo."}, 0b1111, "blah", {R"(foo.)"}); // ^
    CheckEscaping({}, {"command", "foo "}, 0b1111, {}, {R"("command" "foo ")"}); // Arguments can end with spaces and dots just fine.
    CheckEscaping({}, {"command", "foo."}, 0b1111, {}, {R"("command" foo.)"}); // ^
    CheckEscaping({"bleh"}, {"command", "foo "}, 0b1111, "bleh", {R"(command "foo ")"}); // ^
    CheckEscaping({"bleh"}, {"command", "foo."}, 0b1111, "bleh", {R"(command foo.)"}); // ^


    // 4. and 5. Check what counts or doesn't count as cmd/batch, by checking if batch-specific illegal characters are rejected.
    CheckEscaping({}, {"command", "foo\nbar"}, 0b1111, {}, "\"command\" foo\nbar");
    CheckEscaping({}, {"command.bat", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping("command", {"blah", "foo\nbar"}, 0b1111, "command", "blah foo\nbar");
    CheckEscaping("command.bat", {"blah", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break."); // This is a batch file.
    CheckEscaping("command", {"blah.bat", "foo\nbar"}, 0b1111, "command", "blah.bat foo\nbar"); // This isn't.
    CheckEscaping({}, {"blah.batb", "foo\nbar"}, 0b1111, {}, "\"blah.batb\" foo\nbar"); // This isn't too.
    CheckEscaping({}, {"cmd", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break."); // This is a CMD invocation.
    CheckEscaping("cmd", {"blah", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break."); // This is a CMD invocation.
    CheckEscaping("command", {"cmd", "foo\nbar"}, 0b1111, "command", "cmd foo\nbar"); // This isn't.
    CheckEscaping({}, {"cmd.exe", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break."); // This is a CMD invocation.
    CheckEscaping("cmd.exe", {"blah", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break."); // This is a CMD invocation.
    CheckEscaping("command", {"cmd.exe", "foo\nbar"}, 0b1111, "command", "cmd.exe foo\nbar"); // This isn't.

    CheckEscaping({}, {"command.bat", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.baT", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.bAt", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.bAT", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.Bat", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.BaT", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.BAt", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.BAT", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.cmd", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.cmD", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.cMd", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.cMD", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.Cmd", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.CmD", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.CMd", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.CMD", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");

    CheckEscaping({}, {"CMD", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"cMd", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"CMD.EXE", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"CmD.eXe", "foo\nbar"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");

    // We currently don't recognize this as a CMD invocation. We could, but IMO there's no point in bothering.
    CheckEscaping({}, {R"(C:\Windows\System32\cmd.exe)", "foo\nbar"}, 0b1111, {}, "\"C:\\Windows\\System32\\cmd.exe\" foo\nbar");

    // This is of course not a CMD invocation.
    CheckEscaping({}, {"foo\\cmd.exe", "foo\nbar"}, 0b1111, {}, "\"foo\\cmd.exe\" foo\nbar");
    CheckEscaping({}, {"blah.exe", "foo\nbar"}, 0b1111, {}, "\"blah.exe\" foo\nbar");
    CheckEscaping({}, {"blahbat", "foo\nbar"}, 0b1111, {}, "\"blahbat\" foo\nbar");
    CheckEscaping({}, {"blahcmd", "foo\nbar"}, 0b1111, {}, "\"blahcmd\" foo\nbar");
    CheckEscaping({}, {"a.bat.b", "foo\nbar"}, 0b1111, {}, "\"a.bat.b\" foo\nbar");
    CheckEscaping({}, {"a.cmd.b", "foo\nbar"}, 0b1111, {}, "\"a.cmd.b\" foo\nbar");
    CheckEscaping({}, {"cmd.exe.a", "foo\nbar"}, 0b1111, {}, "\"cmd.exe.a\" foo\nbar");


    // 6. Which characters get rejected in batch arguments?

    // Not batch, so everything is allowed.
    CheckEscaping({}, {"blah", "foo\n\r%!bar"}, 0b1111, {}, "\"blah\" foo\n\r%!bar");

    // Ok in non-batch executable name too.
    CheckEscaping({}, {"foo\n\r%!bar"}, 0b1111, {}, "\"foo\n\r%!bar\"");
    CheckEscaping("blah", {"foo\n\r%!bar"}, 0b1111, "blah", "foo\n\r%!bar");
    CheckEscaping("foo\n\r%!bar", {}, 0b1111, "foo\n\r%!bar", {});

    // Reject in the batch name: (`argv[0]`)
    CheckEscaping({}, {"foo\nbar.bat"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"foo\rbar.bat"}, 0b0000, {}, {}, "Bad character in cmd/batch command: carriage return.");
    CheckEscaping({}, {"foo!bar.bat"}, 0b1100'1101, {}, R"("cmd" /d /e:on /v:off /s /c ""foo!bar.bat"")", "Bad character in cmd/batch command: `!`.");
    CheckEscaping({}, {"foo!bar.bat"}, 0b0011'1101, {}, R"("foo!bar.bat")", "Bad character in cmd/batch command: `!`.");
    CheckEscaping({}, {"foo%bar.bat"}, 0b1100'0101, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%%cd:~,%bar.bat"")", "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"foo%bar.bat"}, 0b0011'0101, {}, R"("foo%bar.bat")", "Bad character in cmd/batch command: `%`.");

    // Reject in the batch name: (executable)
    CheckEscaping("foo\nbar.bat", {}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping("foo\rbar.bat", {}, 0b0000, {}, {}, "Bad character in cmd/batch command: carriage return.");
    CheckEscaping("foo!bar.bat", {}, 0b1100'1101, {}, R"("cmd" /d /e:on /v:off /s /c ""foo!bar.bat"")", "Bad character in cmd/batch command: `!`.");
    CheckEscaping("foo!bar.bat", {}, 0b0011'1101, R"(foo!bar.bat)", {}, "Bad character in cmd/batch command: `!`.");
    CheckEscaping("foo%bar.bat", {}, 0b1100'0101, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%%cd:~,%bar.bat"")", "Bad character in cmd/batch command: `%`.");
    CheckEscaping("foo%bar.bat", {}, 0b0011'0101, R"(foo%bar.bat)", {}, "Bad character in cmd/batch command: `%`.");

    // Reject in `argv[0]` even if the executable is specified (and is batch).
    CheckEscaping("blah.bat", {"foo\nbar.bat"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping("blah.bat", {"foo\rbar.bat"}, 0b0000, {}, {}, "Bad character in cmd/batch command: carriage return.");
    CheckEscaping("blah.bat", {"foo!bar.bat"}, 0b1100'1101, {}, R"("cmd" /d /e:on /v:off /s /c ""foo!bar.bat"")", "Bad character in cmd/batch command: `!`."); // Note double quoting!
    CheckEscaping("blah.bat", {"foo!bar.bat"}, 0b0011'1101, "blah.bat", R"(""foo!bar.bat"")", "Bad character in cmd/batch command: `!`."); // ^
    CheckEscaping("blah.bat", {"foo%bar.bat"}, 0b1100'0101, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%%cd:~,%bar.bat"")", "Bad character in cmd/batch command: `%`."); // ^
    CheckEscaping("blah.bat", {"foo%bar.bat"}, 0b0011'0101, "blah.bat", R"(""foo%bar.bat"")", "Bad character in cmd/batch command: `%`."); // ^

    // Reject in arguments, without executable.
    CheckEscaping({}, {"bleh.bat", "foo\nbar.bat"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"bleh.bat", "foo\rbar.bat"}, 0b0000, {}, {}, "Bad character in cmd/batch command: carriage return.");
    CheckEscaping({}, {"bleh.bat", "foo!bar.bat"}, 0b1100'1101, {}, R"("cmd" /d /e:on /v:off /s /c "bleh.bat "foo!bar.bat"")", "Bad character in cmd/batch command: `!`."); // Note double quoting!
    CheckEscaping({}, {"bleh.bat", "foo!bar.bat"}, 0b0011'1101, {}, R"("bleh.bat" "foo!bar.bat")", "Bad character in cmd/batch command: `!`."); // ^
    CheckEscaping({}, {"bleh.bat", "foo%bar.bat"}, 0b1100'0101, {}, R"("cmd" /d /e:on /v:off /s /c "bleh.bat "foo%%cd:~,%bar.bat"")", "Bad character in cmd/batch command: `%`."); // ^
    CheckEscaping({}, {"bleh.bat", "foo%bar.bat"}, 0b0011'0101, {}, R"("bleh.bat" "foo%bar.bat")", "Bad character in cmd/batch command: `%`."); // ^

    // Reject in arguments, with executable.
    CheckEscaping("blah.bat", {"bleh", "foo\nbar.bat"}, 0b0000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping("blah.bat", {"bleh", "foo\rbar.bat"}, 0b0000, {}, {}, "Bad character in cmd/batch command: carriage return.");
    CheckEscaping("blah.bat", {"bleh", "foo!bar.bat"}, 0b1100'1101, {}, R"("cmd" /d /e:on /v:off /s /c "bleh "foo!bar.bat"")", "Bad character in cmd/batch command: `!`."); // Note double quoting!
    CheckEscaping("blah.bat", {"bleh", "foo!bar.bat"}, 0b0011'1101, "blah.bat", R"("bleh "foo!bar.bat"")", "Bad character in cmd/batch command: `!`."); // ^
    CheckEscaping("blah.bat", {"bleh", "foo%bar.bat"}, 0b1100'0101, {}, R"("cmd" /d /e:on /v:off /s /c "bleh "foo%%cd:~,%bar.bat"")", "Bad character in cmd/batch command: `%`."); // ^
    CheckEscaping("blah.bat", {"bleh", "foo%bar.bat"}, 0b0011'0101, "blah.bat", R"("bleh "foo%bar.bat"")", "Bad character in cmd/batch command: `%`."); // ^
}
