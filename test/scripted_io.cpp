// Performs IO operations as specified in `argv`. Can pass zero or more actions from this list:
// (actions with an argument need the argument to be passed as a separate command line argument)
// * `wait N` - waits this many milliseconds.
// * `use_stdout`, `use_stderr` - select what stream is used for subsequent write operations. Defaults to stdout.
// * `write ...` - writes the specified string, checks that the write was successful.
// * `write_fails` - writes one byte and checks that it fails.
// * `read ...` - reads the amount of characters equal to the size of the specified string, and checks that the read was successful and the result equals to that string.
// * `read_fails` - reads one byte and checks that it fails.

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char **argv)
{
    (void)argc;
    argv++;

    auto ConsumeArg = [&]() -> std::string_view
    {
        if (!*argv)
            throw std::runtime_error("Not enough arguments.");
        return std::string_view(*argv++);
    };

    bool use_stderr = false;

    while (*argv)
    {
        std::string_view action = ConsumeArg();

        if (action == "wait")
        {
            std::string_view delay = ConsumeArg();
            if (delay.empty() || delay.size() > 4 || !std::all_of(delay.begin(), delay.end(), [](char ch){return ch >= '0' && ch <= '9';}))
                throw std::runtime_error("The argument of `wait N` must be a number of milliseconds, 1 to 4 digits.");

            std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(delay.data())));
        }
        else if (action == "use_stdout")
        {
            use_stderr = false;
        }
        else if (action == "use_stderr")
        {
            use_stderr = true;
        }
        else if (action == "write")
        {
            if (!((use_stderr ? std::clog : std::cout) << ConsumeArg() << std::flush))
                throw std::runtime_error("`write` operation failed.");
        }
        else if (action == "write_fails")
        {
            if ((use_stderr ? std::clog : std::cout) << '#' << std::flush)
                throw std::runtime_error("`write_fails` operation didn't fail.");
        }
        else if (action == "read")
        {
            std::string_view expected_str = ConsumeArg();
            std::string actual_str(expected_str.size(), ' ');
            if (!std::cin.read(actual_str.data(), std::streamsize(actual_str.size())))
                throw std::runtime_error("`read` operation failed.");
            if (actual_str != expected_str)
                throw std::runtime_error("`read` operation expected `" + std::string(expected_str) + "` but got `" + actual_str + "`.");
        }
        else if (action == "read_fails")
        {
            char ch;
            if (std::cin.read(&ch, 1))
                throw std::runtime_error(std::string("`read_fails` operation didn't fail, got character `") + ch + "`.");
        }
        else
        {
            throw std::runtime_error("Bad action, got `" + std::string(action) + "` but expected one of: `wait N`, `use_stdout`, `use_stderr`, `write ...`, `write_fails`, `read ...`, `read_fails`.");
        }
    }
}
