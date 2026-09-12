#include <em/process.hpp>

#include <stdexcept>
#include <string>

void CheckEscaping(
    std::optional<std::string> executable,
    std::vector<std::string> command,
    em::Proc::Params::CmdBatchMode mode,
    std::optional<std::string> expected_executable,
    std::optional<std::string> expected_command,
    int line = __builtin_LINE()
)
{
    auto CheckVariant = [&mode, &line]<typename T>(std::string variant, std::optional<T> executable, std::vector<T> command, std::optional<T> expected_executable, std::optional<T> expected_command)
    {
        // Make `NativeString` versions of the inputs:
        std::optional<em::Proc::NativeString> executable_wide;
        if (executable)
            executable_wide = *executable;

        std::vector<em::Proc::NativeString> command_wide(command.begin(), command.end());

        // Check the narrow version:
        std::optional<T> out_command;
        std::string out_error;
        bool ok = em::Proc::detail::AssembleCommandLine(
            executable,
            command.size(),
            mode == em::Proc::Params::CmdBatchMode::normal || mode == em::Proc::Params::CmdBatchMode::relaxed,
            mode == em::Proc::Params::CmdBatchMode::normal || mode == em::Proc::Params::CmdBatchMode::keep_registry_settings,
            out_command,
            out_error,
            [&](std::size_t i) -> const auto & {return command.at(i);}
        );

        if (!ok)
            throw std::runtime_error("Line " + std::to_string(line) + ": `AssembleCommandLine` (" + variant + ") reported an unexpected error: `" + out_error + "`.");

        if (!out_error.empty())
            throw std::runtime_error("Line " + std::to_string(line) + ": `AssembleCommandLine` (" + variant + ") returned success, but wrote an error message: `" + out_error + "`.");

        if (executable != expected_executable)
            throw std::runtime_error("Line " + std::to_string(line) + ": `AssembleCommandLine` (" + variant + ") produced an unexpected executable name.");

        if (out_command != expected_command)
            throw std::runtime_error("Line " + std::to_string(line) + ": `AssembleCommandLine` (" + variant + ") produced an unexpected command string.");
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
    const auto mode_a = em::Proc::Params::CmdBatchMode::normal;
    CheckEscaping({}, {R"(foo)"}, mode_a, {}, R"("foo")");
}
