#include <em/process.hpp>

#include <stdexcept>
#include <string>

// Tests the command line escaping function.
// `executable` and `command` are inputs, `expected_executable` and `expected_command` are the expected outputs, and `expected_error` is the expected output error.
// The underlying function has 6 different modes of operation that we can test.
// `passing_mode_mask` controls which modes to test and which of them should pass or fail.
// The mask is 12 bits wide.
// The upper 6 bits control which modes to test. If those are all zeroes, they're automatically replaced with all ones.
// The lower 6 bits control which of the tested modes should return success (and match `expected_executable` and `expected_command`), or otherwise return error (matching `expected_error`).
// If an upper bit is not set, the corresponding lower bit is ignored.
// Out of those 6 modes, the upper 3 have `batch_prepend_cmd == true`, and others have it set to false.
// Then the 3 modes have different values of `batch_safety`: 0, 1, or 2. Higher bits have higher values.
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
    static constexpr auto StrToQuotedNarrowStr = []<typename T>(const T &str) -> std::string
    {
        if constexpr (std::is_same_v<T, em::Proc::NativeString>)
            return '`' + str.get() + '`';
        else if constexpr (std::is_same_v<T, std::wstring>)
            return '`' + em::Proc::ConvertString_Win(std::basic_string_view(str)) + '`';
        else
            return '`' + str + '`';
    };
    // Same but for optional strings.
    static constexpr auto OptStrToQuotedNarrowStr = []<typename T>(const std::optional<T> &opt) -> std::string
    {
        if (opt)
            return StrToQuotedNarrowStr(*opt);
        else
            return "null";
    };

    auto CheckVariant = [&passing_mode_mask, &line, &expected_error]<typename T>(std::string variant, std::optional<T> executable, std::vector<T> command, std::optional<T> expected_executable, std::optional<T> expected_command)
    {
        for (int i = 0; i < 6; i++)
        {
            // Skip this if necessary.
            if (bool(passing_mode_mask & 0b111111'000000) && !bool(passing_mode_mask & (0b100000'000000 >> i)))
                continue;

            bool prepend_cmd = i < 3;
            int batch_safety = 2 - i % 3;

            auto MakeErrorPrefix = [&]
            {
                std::string mask;
                for (int j = 0; j < 6; j++)
                    mask += "01"[i == j];

                return __FILE__ ":" + std::to_string(line) + ": `AssembleCommandLine` (" + variant + ", 0b" + mask + " [prepend_cmd=" + (prepend_cmd ? "true" : "false") + ", batch_safety=" + std::to_string(batch_safety) + "]) ";
            };

            std::optional<T> out_executable = executable; // Must copy this, since the function can change it.
            std::optional<T> out_command;
            std::string out_error;
            bool ok = em::Proc::detail::AssembleCommandLine(
                out_executable,
                command.size(),
                prepend_cmd,
                batch_safety,
                out_command,
                out_error,
                [&](std::size_t i) -> const auto & {return command.at(i);}
            );

            bool should_pass = bool(passing_mode_mask & (0b100000 >> i));

            if (!ok && should_pass)
                throw std::runtime_error(MakeErrorPrefix() + "unexpectedly failed with error: " + StrToQuotedNarrowStr(out_error) + ".");
            if (ok && !should_pass)
                throw std::runtime_error(MakeErrorPrefix() + "unexpectedly passed, the error was supposed to be: " + StrToQuotedNarrowStr(expected_error) + ".");

            if (ok && !out_error.empty())
                throw std::runtime_error(MakeErrorPrefix() + "correctly reported success, but wrote an error message: " + StrToQuotedNarrowStr(out_error) + ".");
            if (!ok && out_error.empty())
                throw std::runtime_error(MakeErrorPrefix() + "correctly reported failure, but wrote no error message: " + StrToQuotedNarrowStr(out_error) + ".");

            if (ok)
            {
                if (out_executable != expected_executable)
                    throw std::runtime_error(MakeErrorPrefix() + "produced an incorrect executable name, got " + OptStrToQuotedNarrowStr(out_executable) + " but expected " + OptStrToQuotedNarrowStr(expected_executable) + ".");

                if (out_command != expected_command)
                    throw std::runtime_error(MakeErrorPrefix() + "produced an incorrect command, got " + OptStrToQuotedNarrowStr(out_command) + " but expected " + OptStrToQuotedNarrowStr(expected_command) + ".");
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
    CheckEscaping({}, {"foo", "bar", "a b"}, 0b111111, {}, R"("foo" bar "a b")", "");
    CheckEscaping("exe", {"foo", "bar", "a b"}, 0b111111, "exe", R"(foo bar "a b")", "");
    CheckEscaping("exe", {}, 0b111111, "exe", {}, "");


    // 1.1. At least one of the two parameters must be specified.
    CheckEscaping({}, {}, 0b000000, {}, {}, "Must specify either an executable or a command.");


    // 1.2. No null characters.
    CheckEscaping(std::string("foo\0bar", 7), {}, 0b000000, {}, {}, "Null character in the executable name.");
    CheckEscaping({}, {std::string("foo\0bar", 7)}, 0b000000, {}, {}, "Null character in the command.");
    CheckEscaping(std::string("foo\0bar", 7), {"foo"}, 0b000000, {}, {}, "Null character in the executable name.");
    CheckEscaping({"foo"}, {std::string("foo\0bar", 7)}, 0b000000, {}, {}, "Null character in the command.");
    CheckEscaping({}, {"a", std::string("foo\0bar", 7), "b"}, 0b000000, {}, {}, "Null character in the command.");
    CheckEscaping({"foo"}, {"a", std::string("foo\0bar", 7), "b"}, 0b000000, {}, {}, "Null character in the command.");
    CheckEscaping(std::string("foo\0bar", 7), {"a", std::string("foo\0bar", 7), "b"}, 0b000000, {}, {}, "Null character in the executable name."); // Here both are null, it's arbitrary which one is reported.


    // 2. Has nothing to test.


    // 3. Error if the executable name (falling back to `argv[0]`) ends with a space or dot.
    CheckEscaping("foo ", {}, 0b000000, {}, {}, "The program name can't end with a space or a dot.");
    CheckEscaping("foo.", {}, 0b000000, {}, {}, "The program name can't end with a space or a dot.");
    CheckEscaping({}, {"foo "}, 0b000000, {}, {}, "The program name can't end with a space or a dot.");
    CheckEscaping({}, {"foo."}, 0b000000, {}, {}, "The program name can't end with a space or a dot.");
    CheckEscaping({"blah"}, {"foo "}, 0b111111, "blah", {R"("foo ")"}); // This is allowed.
    CheckEscaping({"blah"}, {"foo."}, 0b111111, "blah", {R"(foo.)"}); // ^
    CheckEscaping({}, {"command", "foo "}, 0b111111, {}, {R"("command" "foo ")"}); // Arguments can end with spaces and dots just fine.
    CheckEscaping({}, {"command", "foo."}, 0b111111, {}, {R"("command" foo.)"}); // ^
    CheckEscaping({"bleh"}, {"command", "foo "}, 0b111111, "bleh", {R"(command "foo ")"}); // ^
    CheckEscaping({"bleh"}, {"command", "foo."}, 0b111111, "bleh", {R"(command foo.)"}); // ^


    // 4. and 5. Check what counts or doesn't count as cmd/batch, by checking if batch-specific illegal characters are rejected.
    CheckEscaping({}, {"command", "foo\nbar"}, 0b111111, {}, "\"command\" foo\nbar");
    CheckEscaping({}, {"command.bat", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping("command", {"blah", "foo\nbar"}, 0b111111, "command", "blah foo\nbar");
    CheckEscaping("command.bat", {"blah", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break."); // This is a batch file.
    CheckEscaping("command", {"blah.bat", "foo\nbar"}, 0b111111, "command", "blah.bat foo\nbar"); // This isn't.
    CheckEscaping({}, {"blah.batb", "foo\nbar"}, 0b111111, {}, "\"blah.batb\" foo\nbar"); // This isn't too.
    CheckEscaping({}, {"cmd", "foo\nbar"}, 0b111111, {}, "\"cmd\" foo\nbar"); // This is a CMD invocation, but `\n` before `/c` is allowed.
    CheckEscaping({}, {"cmd", "/c", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break."); // This is a CMD invocation.
    CheckEscaping("cmd", {"blah", "/c", "foo\nbar"}, 0b111111, "cmd", "blah /c foo\nbar"); // This isn't. (Because the first parameter doesn't respect PATH.)
    CheckEscaping("command", {"cmd", "/c", "foo\nbar"}, 0b111111, "command", "cmd /c foo\nbar"); // This isn't.
    CheckEscaping({}, {"cmd.exe", "/c", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break."); // This is a CMD invocation.
    CheckEscaping("cmd.exe", {"blah", "/c", "foo\nbar"}, 0b111111, "cmd.exe", "blah /c foo\nbar"); // This isn't. (Because the first parameter doesn't respect PATH.)
    CheckEscaping("command", {"cmd.exe", "/c", "foo\nbar"}, 0b111111, "command", "cmd.exe /c foo\nbar"); // This isn't.

    CheckEscaping({}, {"command.bat", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.baT", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.bAt", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.bAT", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.Bat", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.BaT", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.BAt", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.BAT", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.cmd", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.cmD", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.cMd", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.cMD", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.Cmd", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.CmD", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.CMd", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"command.CMD", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");

    CheckEscaping({}, {"CMD", "/c", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"cMd", "/c", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"CMD.EXE", "/c", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({}, {"CmD.eXe", "/c", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");


    // We currently don't recognize this as a CMD invocation. We could, but IMO there's no point in bothering.
    CheckEscaping({}, {R"(C:\Windows\System32\cmd.exe)", "foo\nbar"}, 0b111111, {}, "\"C:\\Windows\\System32\\cmd.exe\" foo\nbar");

    // This is of course not a CMD invocation.
    CheckEscaping({}, {"foo\\cmd.exe", "foo\nbar"}, 0b111111, {}, "\"foo\\cmd.exe\" foo\nbar");
    CheckEscaping({}, {"blah.exe", "foo\nbar"}, 0b111111, {}, "\"blah.exe\" foo\nbar");
    CheckEscaping({}, {"blahbat", "foo\nbar"}, 0b111111, {}, "\"blahbat\" foo\nbar");
    CheckEscaping({}, {"blahcmd", "foo\nbar"}, 0b111111, {}, "\"blahcmd\" foo\nbar");
    CheckEscaping({}, {"a.bat.b", "foo\nbar"}, 0b111111, {}, "\"a.bat.b\" foo\nbar");
    CheckEscaping({}, {"a.cmd.b", "foo\nbar"}, 0b111111, {}, "\"a.cmd.b\" foo\nbar");
    CheckEscaping({}, {"cmd.exe.a", "foo\nbar"}, 0b111111, {}, "\"cmd.exe.a\" foo\nbar");


    // 6. Prepending CMD invocation.

    CheckEscaping({}, {"foo.bat", "blah"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo.bat blah")");
    CheckEscaping({}, {"foo.bat", "blah"}, 0b000111'111111, {}, R"("foo.bat" blah)");
    CheckEscaping({"foo.bat"}, {"huh", "blah"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "huh blah")"); // Note the loss of the original executable name.
    CheckEscaping({"foo.bat"}, {"huh", "blah"}, 0b000111'111111, "foo.bat", R"(""huh" blah")"); // Here the entire command is quoted.

    // Those arguments are not special here.
    CheckEscaping({}, {"foo.bat", "/d", "/e:on", "/v:off", "/s", "/c", "blah"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo.bat /d /e:on /v:off /s /c blah")");
    CheckEscaping({}, {"foo.bat", "/d", "/e:on", "/v:off", "/s", "/c", "blah"}, 0b000111'111111, {}, R"("foo.bat" /d /e:on /v:off /s /c blah)");


    // 7. Validating the batch executable name.

    // Only in the first argument at this step.
    CheckEscaping("foo\nbar.bat", {}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping("foo\rbar.bat", {}, 0b000000, {}, {}, "Bad character in cmd/batch command: carriage return.");
    CheckEscaping("foo%bar.bat", {}, 0b100110'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping("foo%bar.bat", {}, 0b010000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%%cd:~,%bar.bat"")");
    CheckEscaping("foo%bar.bat", {}, 0b001000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%bar.bat"")");
    CheckEscaping("foo%bar.bat", {}, 0b000001'111111, "foo%bar.bat", {});
    CheckEscaping("foo!bar.bat", {}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo!bar.bat"")");
    CheckEscaping("foo!bar.bat", {}, 0b000110'000000, {}, {}, "Bad character in cmd/batch command: `!`.");
    CheckEscaping("foo!bar.bat", {}, 0b000001'111111, "foo!bar.bat", {});


    // 8. Assembling the command:


    // 8.1. Quoting the entire command.
    CheckEscaping("foo.bat", {"foo", "bar"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo bar")"); // Only the remainder of the command is quoted. `foo` intentionally isn't quoted, that doesn't do anything.
    CheckEscaping("foo.bat", {"foo", "bar"}, 0b000111'111111, "foo.bat", R"(""foo" bar")"); // The entire command is quoted. `"foo"` is quoted because we don't have an `\s`, so we have to ensure the outer quotes are removed this way.
    CheckEscaping({}, {"foo.bat", "bar"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo.bat bar")");
    CheckEscaping({}, {"foo.bat", "bar"}, 0b000111'111111, {}, R"("foo.bat" bar)"); // No whole command quoting here.


    // 8.2. Doesn't need testing.


    // 8.3.1. Is tested along with 8.3.2.


    // 8.3.2. Validation of arguments that use CMD handling.

    // Batch `\n`.
    CheckEscaping({}, {"foo\nbar.bat"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");
    CheckEscaping({"foo\nbar.bat"}, {"foo.bat"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo.bat")"); // This is a little weird, but we discard the executable name entirely, so why not.
    CheckEscaping({"foo\nbar.bat"}, {"foo.bat"}, 0b000111'111111, "foo\nbar.bat", R"(""foo.bat"")"); // The entire command is quoted here, and then the first element is quoted again because of the missing `/s`.
    CheckEscaping({"foo.bat"}, {"foo\nbar.bat"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break.");

    // Batch `\r`.
    CheckEscaping({}, {"foo\rbar.bat"}, 0b000000, {}, {}, "Bad character in cmd/batch command: carriage return.");
    CheckEscaping({"foo\rbar.bat"}, {"foo.bat"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo.bat")"); // This is a little weird, but we discard the executable name entirely, so why not.
    CheckEscaping({"foo\rbar.bat"}, {"foo.bat"}, 0b000111'111111, "foo\rbar.bat", R"(""foo.bat"")"); // The entire command is quoted here, and then the first element is quoted again because of the missing `/s`.
    CheckEscaping({"foo.bat"}, {"foo\rbar.bat"}, 0b000000, {}, {}, "Bad character in cmd/batch command: carriage return.");

    // Batch `%`.
    CheckEscaping({}, {"foo%bar.bat"}, 0b100110'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"foo%bar.bat"}, 0b010000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%%cd:~,%bar.bat"")");
    CheckEscaping({}, {"foo%bar.bat"}, 0b001000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%bar.bat"")");
    CheckEscaping({}, {"foo%bar.bat"}, 0b000001'111111, {}, R"("foo%bar.bat")");
    CheckEscaping({"foo%bar.bat"}, {"foo.bat"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo.bat")"); // This is a little weird, but we discard the executable name entirely, so why not.
    CheckEscaping({"foo%bar.bat"}, {"foo.bat"}, 0b000111'111111, "foo%bar.bat", R"(""foo.bat"")"); // The entire command is quoted here, and then the first element is quoted again because of the missing `/s`.
    // This is mostly the same as without the executable name, except that the entire command is also quoted if `cmd` is not prepended.
    CheckEscaping({"foo.bat"}, {"foo%bar.bat"}, 0b100110'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({"foo.bat"}, {"foo%bar.bat"}, 0b010000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%%cd:~,%bar.bat"")");
    CheckEscaping({"foo.bat"}, {"foo%bar.bat"}, 0b001000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%bar.bat"")");
    CheckEscaping({"foo.bat"}, {"foo%bar.bat"}, 0b000001'111111, "foo.bat", R"(""foo%bar.bat"")"); // The entire command is quoted here.

    // Batch `!`.
    CheckEscaping({}, {"foo!bar.bat"}, 0b111000'111111, {}, {R"("cmd" /d /e:on /v:off /s /c ""foo!bar.bat"")"});
    CheckEscaping({}, {"foo!bar.bat"}, 0b000110'000000, {}, {}, "Bad character in cmd/batch command: `!`.");
    CheckEscaping({}, {"foo!bar.bat"}, 0b000001'111111, {}, {R"("foo!bar.bat")"});


    // Cmd `\n`.
    CheckEscaping({}, {"cmd", "foo\nbar"}, 0b111111, {}, "\"cmd\" foo\nbar"); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo\nbar", "/c"}, 0b111000'111111, {}, "\"cmd\" foo\nbar /d /e:on /v:off /s /c \"\""); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo\nbar", "/c"}, 0b000111'111111, {}, "\"cmd\" foo\nbar /s /c \"\""); // ^
    CheckEscaping({}, {"cmd", "foo\nbar", "/c", "blah"}, 0b111000'111111, {}, "\"cmd\" foo\nbar /d /e:on /v:off /s /c \"blah\""); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo\nbar", "/c", "blah"}, 0b000111'111111, {}, "\"cmd\" foo\nbar /s /c \"blah\""); // ^
    CheckEscaping({}, {"cmd", "/c", "foo\nbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: line break."); // And this is finally not ok.

    // Cmd `\r`.
    CheckEscaping({}, {"cmd", "foo\rbar"}, 0b111111, {}, "\"cmd\" foo\rbar"); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo\rbar", "/c"}, 0b111000'111111, {}, "\"cmd\" foo\rbar /d /e:on /v:off /s /c \"\""); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo\rbar", "/c"}, 0b000111'111111, {}, "\"cmd\" foo\rbar /s /c \"\""); // ^
    CheckEscaping({}, {"cmd", "foo\rbar", "/c", "blah"}, 0b111000'111111, {}, "\"cmd\" foo\rbar /d /e:on /v:off /s /c \"blah\""); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo\rbar", "/c", "blah"}, 0b000111'111111, {}, "\"cmd\" foo\rbar /s /c \"blah\""); // ^
    CheckEscaping({}, {"cmd", "/c", "foo\rbar"}, 0b000000, {}, {}, "Bad character in cmd/batch command: carriage return."); // And this is finally not ok.

    // Cmd `%`.
    CheckEscaping({}, {"cmd", "foo%bar"}, 0b111111, {}, "\"cmd\" foo%bar"); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo%bar", "/c"}, 0b111000'111111, {}, "\"cmd\" foo%bar /d /e:on /v:off /s /c \"\""); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo%bar", "/c"}, 0b000111'111111, {}, "\"cmd\" foo%bar /s /c \"\""); // ^
    CheckEscaping({}, {"cmd", "foo%bar", "/c", "blah"}, 0b111000'111111, {}, "\"cmd\" foo%bar /d /e:on /v:off /s /c \"blah\""); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo%bar", "/c", "blah"}, 0b000111'111111, {}, "\"cmd\" foo%bar /s /c \"blah\""); // ^
    CheckEscaping({}, {"cmd", "/c", "foo%bar"}, 0b100110'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"cmd", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/c", "foo%bar"}, 0b001000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo%bar"")");
    CheckEscaping({}, {"cmd", "/c", "foo%bar"}, 0b000001'111111, {}, R"("cmd" /s /c ""foo%bar"")");
    // With custom flags:
    CheckEscaping({}, {"cmd", "a", "/e:on", "b", "/c", "foo%bar"}, 0b100100'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"cmd", "a", "/e:on", "b", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" a /e:on b /d /v:off /s /c ""foo%%cd:~,%bar"")"); // Here `/e:on` replaces the one that would be added implicitly, but otherwise has no effect.
    CheckEscaping({}, {"cmd", "a", "/e:on", "b", "/c", "foo%bar"}, 0b001000'111111, {}, R"("cmd" a /e:on b /d /v:off /s /c ""foo%bar"")"); // Here `/e:on` replaces the one that would be added implicitly, but otherwise has no effect.
    CheckEscaping({}, {"cmd", "a", "/e:on", "b", "/c", "foo%bar"}, 0b000010'111111, {}, R"("cmd" a /e:on b /s /c ""foo%%cd:~,%bar"")"); // `/e:on` allows escaping here.
    CheckEscaping({}, {"cmd", "a", "/e:on", "b", "/c", "foo%bar"}, 0b000001'111111, {}, R"("cmd" a /e:on b /s /c ""foo%bar"")"); // `/e:on` is ignored here.
    CheckEscaping({}, {"cmd", "a", "/e:off", "b", "/c", "foo%bar"}, 0b110110'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"cmd", "a", "/e:off", "b", "/c", "foo%bar"}, 0b001000'111111, {}, R"("cmd" a /e:off b /d /v:off /s /c ""foo%bar"")"); // Here `/e:off` replaces the one that would be added implicitly, but otherwise has no effect.
    CheckEscaping({}, {"cmd", "a", "/e:off", "b", "/c", "foo%bar"}, 0b000001'111111, {}, R"("cmd" a /e:off b /s /c ""foo%bar"")"); // `/e:off` is ignored here.
    // Different spellings of the flag.
    CheckEscaping({}, {"cmd", "/e:on", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /e:on /d /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/E:ON", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /E:ON /d /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/e:On", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /e:On /d /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/e:off", "/c", "foo%bar"}, 0b010000'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"cmd", "/E:OFF", "/c", "foo%bar"}, 0b010000'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"cmd", "/e:oFf", "/c", "foo%bar"}, 0b010000'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"cmd", "/e:onblah", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /e:onblah /d /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/E:ONBLAH", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /E:ONBLAH /d /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/e:OnBlAh", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /e:OnBlAh /d /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/e:offblah", "/c", "foo%bar"}, 0b010000'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"cmd", "/E:OFFBLAH", "/c", "foo%bar"}, 0b010000'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"cmd", "/e:oFfBlAh", "/c", "foo%bar"}, 0b010000'000000, {}, {}, "Bad character in cmd/batch command: `%`.");
    CheckEscaping({}, {"cmd", "/e", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /e /d /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/E", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /E /d /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/eoff", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /eoff /d /v:off /s /c ""foo%%cd:~,%bar"")");
    CheckEscaping({}, {"cmd", "/e::off", "/c", "foo%bar"}, 0b010000'111111, {}, R"("cmd" /e::off /d /v:off /s /c ""foo%%cd:~,%bar"")");

    // Cmd `!`.
    CheckEscaping({}, {"cmd", "foo!bar"}, 0b111111, {}, "\"cmd\" foo!bar"); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo!bar", "/c"}, 0b111000'111111, {}, "\"cmd\" foo!bar /d /e:on /v:off /s /c \"\""); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo!bar", "/c"}, 0b000111'111111, {}, "\"cmd\" foo!bar /s /c \"\""); // ^
    CheckEscaping({}, {"cmd", "foo!bar", "/c", "blah"}, 0b111000'111111, {}, "\"cmd\" foo!bar /d /e:on /v:off /s /c \"blah\""); // Ok before `/c`.
    CheckEscaping({}, {"cmd", "foo!bar", "/c", "blah"}, 0b000111'111111, {}, "\"cmd\" foo!bar /s /c \"blah\""); // ^
    CheckEscaping({}, {"cmd", "/c", "foo!bar"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c ""foo!bar"")");
    CheckEscaping({}, {"cmd", "/c", "foo!bar"}, 0b000110'000000, {}, {}, "Bad character in cmd/batch command: `!`.");
    CheckEscaping({}, {"cmd", "/c", "foo!bar"}, 0b000001'111111, {}, R"("cmd" /s /c ""foo!bar"")");
    // With custom flags:
    CheckEscaping({}, {"cmd", "a", "/v:off", "b", "/c", "foo!bar"}, 0b111000'111111, {}, R"("cmd" a /v:off b /d /e:on /s /c ""foo!bar"")"); // Here `/v:off` replaces the one that would be added implicitly, but otherwise has no effect.
    CheckEscaping({}, {"cmd", "a", "/v:off", "b", "/c", "foo!bar"}, 0b000111'111111, {}, R"("cmd" a /v:off b /s /c ""foo!bar"")"); // `/v:off` allows `!` here (except that in the `unsafe_as_is` mode it would be allowed regardless).
    CheckEscaping({}, {"cmd", "a", "/v:on", "b", "/c", "foo!bar"}, 0b110110'000000, {}, {}, "Bad character in cmd/batch command: `!`.");
    CheckEscaping({}, {"cmd", "a", "/v:on", "b", "/c", "foo!bar"}, 0b001000'111111, {}, R"("cmd" a /v:on b /d /e:on /s /c ""foo!bar"")"); // Here `/v:on` replaces the one that would be added implicitly, but otherwise has no effect.
    CheckEscaping({}, {"cmd", "a", "/v:on", "b", "/c", "foo!bar"}, 0b000001'111111, {}, R"("cmd" a /v:on b /s /c ""foo!bar"")"); // `/v:on` is ignored here.


    // 8.3.3. Has nothing to test.


    // 8.4.4. Adding implicit arguments.

    // Batch.
    CheckEscaping("foo.bat", {}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo.bat")");
    CheckEscaping("foo.bat", {}, 0b000111'111111, "foo.bat", {});
    CheckEscaping("foo.bat", {"bar.bat"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "bar.bat")");
    CheckEscaping("foo.bat", {"bar.bat"}, 0b000111'111111, "foo.bat", R"(""bar.bat"")"); // Here the entire command is quoted, and then the first element is quoted too because of the missing `/s`.
    CheckEscaping("foo.bat", {"bar.bat", "blah"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "bar.bat blah")");
    CheckEscaping("foo.bat", {"bar.bat", "blah"}, 0b000111'111111, "foo.bat", R"(""bar.bat" blah")");
    CheckEscaping({}, {"foo.bat"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo.bat")");
    CheckEscaping({}, {"foo.bat"}, 0b000111'111111, {}, R"("foo.bat")");
    CheckEscaping({}, {"foo.bat", "blah"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo.bat blah")");
    CheckEscaping({}, {"foo.bat", "blah"}, 0b000111'111111, {}, R"("foo.bat" blah)");
    // Those arguments are out of place and don't count.
    CheckEscaping("foo.bat", {"bar.bat", "/d", "/e:on", "/v:off", "/s", "/c"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "bar.bat /d /e:on /v:off /s /c")");
    CheckEscaping("foo.bat", {"bar.bat", "/d", "/e:on", "/v:off", "/s", "/c"}, 0b000111'111111, "foo.bat", R"(""bar.bat" /d /e:on /v:off /s /c")");
    CheckEscaping({}, {"bar.bat", "/d", "/e:on", "/v:off", "/s", "/c"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "bar.bat /d /e:on /v:off /s /c")");
    CheckEscaping({}, {"bar.bat", "/d", "/e:on", "/v:off", "/s", "/c"}, 0b000111'111111, {}, R"("bar.bat" /d /e:on /v:off /s /c)");
    CheckEscaping({}, {"bar.bat", "/d", "/e:on", "/v:off", "/s", "/c", "%"}, 0b000010'000000, {}, {}, "Bad character in cmd/batch command: `%`."); // Those arguments don't allow us to escape `%`.
    CheckEscaping({}, {"bar.bat", "/d", "/e:on", "/v:off", "/s", "/c", "!"}, 0b000010'000000, {}, {}, "Bad character in cmd/batch command: `!`."); // Those arguments don't allow us to use `!`.

    // Cmd.
    CheckEscaping({}, {"cmd"}, 0b111111, {}, R"("cmd")"); // No `/c`, so not appending anything.
    CheckEscaping({}, {"cmd", "blah"}, 0b111111, {}, R"("cmd" blah)"); // No `/c`, so not appending anything.
    CheckEscaping({}, {"cmd", "blah", "/c"}, 0b111000'111111, {}, R"("cmd" blah /d /e:on /v:off /s /c "")"); // The final quotes are arbitrary here, they don't do anything anyway.
    CheckEscaping({}, {"cmd", "blah", "/c"}, 0b000111'111111, {}, R"("cmd" blah /s /c "")"); // Same thing about the final quotes. Also only adding `/s` here, it's the only mandatory flag.
    CheckEscaping({}, {"cmd", "blah", "/c", "foo"}, 0b111000'111111, {}, R"("cmd" blah /d /e:on /v:off /s /c "foo")");
    CheckEscaping({}, {"cmd", "blah", "/c", "foo"}, 0b000111'111111, {}, R"("cmd" blah /s /c "foo")");
    CheckEscaping({}, {"cmd", "blah", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" blah /d /e:on /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "blah", "/c", "foo", "bar"}, 0b000111'111111, {}, R"("cmd" blah /s /c "foo bar")");
    // Now try appending some flags.
    // In forward order:
    CheckEscaping({}, {"cmd", "/d", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /d foo /e:on /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/D", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /D foo /e:on /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "foo", "/c", "foo", "bar"}, 0b000111'111111, {}, R"("cmd" /d foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/D", "foo", "/c", "foo", "bar"}, 0b000111'111111, {}, R"("cmd" /D foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/dBlAh", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /dBlAh foo /e:on /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d:off", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /d:off foo /e:on /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/dBlAh", "foo", "/c", "foo", "bar"}, 0b000111'111111, {}, R"("cmd" /dBlAh foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d:off", "foo", "/c", "foo", "bar"}, 0b000111'111111, {}, R"("cmd" /d:off foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /d /e foo /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/E", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /d /E foo /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "foo", "/c", "foo", "bar"}, 0b000111'111111, {}, R"("cmd" /d /e foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/E", "foo", "/c", "foo", "bar"}, 0b000111'111111, {}, R"("cmd" /d /E foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/eBlAh", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /d /eBlAh foo /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e:off", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /d /e:off foo /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/eBlAh", "foo", "/c", "foo", "bar"}, 0b000111'111111, {}, R"("cmd" /d /eBlAh foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e:off", "foo", "/c", "foo", "bar"}, 0b000111'111111, {}, R"("cmd" /d /e:off foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "/v", "foo", "/c", "foo", "bar"}, 0b111111'111111, {}, R"("cmd" /d /e /v foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "/V", "foo", "/c", "foo", "bar"}, 0b111111'111111, {}, R"("cmd" /d /e /V foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "/vBlAh", "foo", "/c", "foo", "bar"}, 0b111111'111111, {}, R"("cmd" /d /e /vBlAh foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "/v:off", "foo", "/c", "foo", "bar"}, 0b111111'111111, {}, R"("cmd" /d /e /v:off foo /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "/v", "/s", "foo", "/c", "foo", "bar"}, 0b111111'111111, {}, R"("cmd" /d /e /v /s foo /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "/v", "/S", "foo", "/c", "foo", "bar"}, 0b111111'111111, {}, R"("cmd" /d /e /v /S foo /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "/v", "/sBlAh", "foo", "/c", "foo", "bar"}, 0b111111'111111, {}, R"("cmd" /d /e /v /sBlAh foo /c "foo bar")");
    CheckEscaping({}, {"cmd", "/d", "/e", "/v", "/s:off", "foo", "/c", "foo", "bar"}, 0b111111'111111, {}, R"("cmd" /d /e /v /s:off foo /c "foo bar")");
    // In reverse order:
    CheckEscaping({}, {"cmd", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" foo /d /e:on /v:off /s /c "foo bar")");
    CheckEscaping({}, {"cmd", "/s", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /s foo /d /e:on /v:off /c "foo bar")");
    CheckEscaping({}, {"cmd", "/s", "/v", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /s /v foo /d /e:on /c "foo bar")");
    CheckEscaping({}, {"cmd", "/s", "/v", "/e", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /s /v /e foo /d /c "foo bar")");
    CheckEscaping({}, {"cmd", "/s", "/v", "/e", "/d", "foo", "/c", "foo", "bar"}, 0b111000'111111, {}, R"("cmd" /s /v /e /d foo /c "foo bar")");

    // Different variants of `/c` and `/k`.
    CheckEscaping({}, {"cmd", "/c", "foo"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "foo")");
    CheckEscaping({}, {"cmd", "/C", "foo"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /C "foo")");
    CheckEscaping({}, {"cmd", "/k", "foo"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /k "foo")");
    CheckEscaping({}, {"cmd", "/K", "foo"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /K "foo")");

    // Stuff directly after `/c` or `/k`.
    CheckEscaping({}, {"cmd", "/cblah", "foo"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /c "blah foo")");
    CheckEscaping({}, {"cmd", "/Cblah", "foo"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /C "blah foo")");
    CheckEscaping({}, {"cmd", "/kblah", "foo"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /k "blah foo")");
    CheckEscaping({}, {"cmd", "/Kblah", "foo"}, 0b111000'111111, {}, R"("cmd" /d /e:on /v:off /s /K "blah foo")");


    // 8.4.5. Doesn't need to be tested.


    // 8.4.6. Quoting conditions.

    // A lot of those were copied from the blog pos.
    CheckEscaping({}, {"foo.bat", ""}, 0b000111'111111, {}, R"("foo.bat" "")");
    CheckEscaping({}, {"foo.bat", "\""}, 0b000111'111111, {}, R"("foo.bat" """")");
    CheckEscaping({}, {"foo.bat", "\"\""}, 0b000111'111111, {}, R"("foo.bat" """""")");
    CheckEscaping({}, {"foo.bat", " "}, 0b000111'111111, {}, R"("foo.bat" " ")");
    CheckEscaping({}, {"foo.bat", "foo bar"}, 0b000111'111111, {}, R"("foo.bat" "foo bar")");
    CheckEscaping({}, {"foo.bat", "foo "}, 0b000111'111111, {}, R"("foo.bat" "foo ")");
    CheckEscaping({}, {"foo.bat", " foo"}, 0b000111'111111, {}, R"("foo.bat" " foo")");
    CheckEscaping({}, {"foo.bat", "foo\tbar"}, 0b000111'111111, {}, "\"foo.bat\" \"foo\tbar\"");
    CheckEscaping({}, {"foo.bat", "foo\"bar"}, 0b000111'111111, {}, R"("foo.bat" "foo""bar")");
    CheckEscaping({}, {"foo.bat", "foo\" bar"}, 0b000111'111111, {}, R"("foo.bat" "foo"" bar")");
    CheckEscaping({}, {"foo.bat", R"(foo\bar)"}, 0b000111'111111, {}, R"("foo.bat" foo\bar)");
    CheckEscaping({}, {"foo.bat", R"(foo\\bar)"}, 0b000111'111111, {}, R"("foo.bat" foo\\bar)");
    CheckEscaping({}, {"foo.bat", R"(foo\\\bar)"}, 0b000111'111111, {}, R"("foo.bat" foo\\\bar)");
    CheckEscaping({}, {"foo.bat", R"(foo\"bar)"}, 0b000111'111111, {}, R"("foo.bat" "foo\\""bar")");
    CheckEscaping({}, {"foo.bat", R"(foo\\"bar)"}, 0b000111'111111, {}, R"("foo.bat" "foo\\\\""bar")");
    CheckEscaping({}, {"foo.bat", R"(foo\\\"bar)"}, 0b000111'111111, {}, R"("foo.bat" "foo\\\\\\""bar")");
    CheckEscaping({}, {"foo.bat", R"(foobar\)"}, 0b000111'111111, {}, R"("foo.bat" foobar\)");
    CheckEscaping({}, {"foo.bat", R"(foobar\\)"}, 0b000111'111111, {}, R"("foo.bat" foobar\\)");
    CheckEscaping({}, {"foo.bat", R"(foobar\\\)"}, 0b000111'111111, {}, R"("foo.bat" foobar\\\)");
    CheckEscaping({}, {"foo.bat", R"(foo bar\)"}, 0b000111'111111, {}, R"("foo.bat" "foo bar\\")");
    CheckEscaping({}, {"foo.bat", R"(foo bar\\)"}, 0b000111'111111, {}, R"("foo.bat" "foo bar\\\\")");
    CheckEscaping({}, {"foo.bat", R"(foo bar\\\)"}, 0b000111'111111, {}, R"("foo.bat" "foo bar\\\\\\")");

    // Trailing quote that's not specific to this argument doesn't need duplicating `\`s.
    CheckEscaping({}, {"cmd", "/c", R"(foo\)"}, 0b000111'111111, {}, R"("cmd" /s /c "foo\")");
    CheckEscaping({}, {"cmd", "/c", "foo", R"(bar\)"}, 0b000111'111111, {}, R"("cmd" /s /c "foo bar\")");
    // And similarly when quoting the entire command.
    CheckEscaping("foo.bat", {"bar.bat", R"(foo\)"}, 0b000111'111111, "foo.bat", R"(""bar.bat" foo\")");


    // The rest doesn't need any testing, the above testcases should cover everything.
}
