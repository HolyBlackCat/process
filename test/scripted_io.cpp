// Performs IO operations as specified in `argv`. Can pass zero or more actions from this list:
// (actions with an argument need the argument to be passed as a separate command line argument)
// * `wait N` - waits this many milliseconds.
// * `use_stdout`, `use_stderr` - select what stream is used for subsequent write operations. Defaults to stdout.
// * `write ...` - writes the specified string, checks that the write was successful.
//     If the string is literally `long`, then instead uses a specific long string that's larger than a pipe buffer.
// * `write_fails` - writes one byte and checks that it fails.
// * `read ...` - reads the amount of characters equal to the size of the specified string, and checks that the read was successful and the result equals to that string.
//     If the expected string is literally `long`, then instead uses a specific long string that's larger than a pipe buffer.
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

    // If `s == "long"`, replaces it with a long string, same string every time.
    // Sync this with `scripted_pipes.cpp`.
    auto ReplaceLongString = [](std::string &s)
    {
        if (s == "long")
        {
            for (std::size_t i = 0; i < 15000; i++)
            {
                if (i > 0)
                    s += ' ';
                s += std::to_string(i);
            }
        }
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
            std::string str(ConsumeArg());
            ReplaceLongString(str);
            if (!((use_stderr ? std::clog : std::cout) << str << std::flush))
                throw std::runtime_error("`write` operation failed.");
        }
        else if (action == "write_fails")
        {
            if ((use_stderr ? std::clog : std::cout) << '#' << std::flush)
                throw std::runtime_error("`write_fails` operation didn't fail.");
        }
        else if (action == "read")
        {
            std::string expected_str(ConsumeArg());
            ReplaceLongString(expected_str);
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
