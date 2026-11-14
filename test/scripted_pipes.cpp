// Launches a process with some pipes attached, and performs pipe operations as specified in `argv`.
// The usage is:
//     scripted_pipes [pipe_stdin] [pipe_stdout] [pipe_stderr] -- argv... -- actions...
//
// Where `actions...` are zero or more actions from this list:
// (actions with an argument need the argument to be passed as a separate command line argument)
// * `wait N` - waits this many milliseconds.
// * `use_stdout`, `use_stderr` - select which pipe is used for subsequent read operations (sic). Defaults to stdout.
// * `write data status` - writes `data`, then checks that the status equals `status`,
//     which is a `IoResult` enum, one of: `retry_later`, `end_of_buffer`, `end_of_stream`, `error`.
//     Passing status `ok` makes no sense, since we loop while it's `ok`.
//     If `data` is a string `long`, then instead passes a long string designed to fill a pipe buffer.
// * `read data status` - same, but for reads. This is affected by `use_stdout`/`use_stderr`.
// * `close_write_pipe` - closes the pipe that's attached to `stdin`.
// * `close_read_pipe` - closes the pipe that's attached to `stdout` or `stderr` (depending on `use_stdout`/`use_stderr`)
// * `nonblocking_writes` - makes the pipe that's attached to `stdin` non-blocking.
// * `nonblocking_reads` - makes the pipe that's attached to `stdout` or `stderr` non-blocking (depending on `use_stdout`/`use_stderr`).
// * `expect_crash` - expect the subprocess to not exit successfully. This check is done at the end of the program, rather than immediately.

#include <em/process.hpp>

#include <algorithm>
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

    em::Proc::Params params;
    em::Proc::OutputStream pipe_stdin;
    em::Proc::InputStream pipe_stdout;
    em::Proc::InputStream pipe_stderr;

    // Decide which pipes to enable.
    while (true)
    {
        std::string_view arg = ConsumeArg();

        if (arg == "--")
        {
            if (!pipe_stdin && !pipe_stdout && !pipe_stderr)
                throw std::runtime_error("Need at least one pipe redirect before `--`.");
            break;
        }

        if (arg == "pipe_stdin")
        {
            if (pipe_stdin)
                throw std::runtime_error("Duplicate `pipe_stdin`.");
            em::Proc::MakePipe(pipe_stdin, params.stdin_stream);
            continue;
        }

        if (arg == "pipe_stdout")
        {
            if (pipe_stdout)
                throw std::runtime_error("Duplicate `pipe_stdout`.");
            em::Proc::MakePipe(params.stdout_stream, pipe_stdout);
            continue;
        }

        if (arg == "pipe_stderr")
        {
            if (pipe_stderr)
                throw std::runtime_error("Duplicate `pipe_stderr`.");
            em::Proc::MakePipe(params.stderr_stream, pipe_stderr);
            continue;
        }

        throw std::runtime_error("Expected one of: `pipe_stdin`, `pipe_stdout`, `pipe_stderr`, or `--` to begin the process arguments.");
    }

    { // Get the process argv, between the two `--`s.
        char **proc_argv = argv;

        while (ConsumeArg() != "--") {}

        params.command = {proc_argv, std::size_t(argv - proc_argv - 1)};
    }

    // Start the process.
    em::Proc::Process proc(std::move(params));
    proc.ThrowError();

    // Evaluate the commands.
    bool use_stderr = false;
    while (*argv)
    {
        std::string_view action = ConsumeArg();

        // If `s == "long"`, replaces it with a long string, same string every time.
        // Sync this with `scripted_io.cpp`.
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
            std::string data(ConsumeArg());
            ReplaceLongString(data);
            std::size_t pos = 0;
            em::Proc::IoResult status;
            while ((status = pipe_stdin.WriteUnbuffered(data, pos)) == em::Proc::IoResult::ok) {}

            std::string expected_status(ConsumeArg());
            if (em::Proc::to_string(status) != expected_status)
                throw std::runtime_error("The status of a write operation was `" + std::string(em::Proc::to_string(status)) + "`, but expected `" + expected_status + "`.");
        }
        else if (action == "read")
        {
            std::string expected_data(ConsumeArg());
            ReplaceLongString(expected_data);
            std::string data(expected_data.size(), '\0');
            std::size_t pos = 0;
            em::Proc::IoResult status;
            em::Proc::InputStream &in = use_stderr ? pipe_stderr : pipe_stdout;
            while ((status = in.ReadUnbuffered(data, pos)) == em::Proc::IoResult::ok) {}

            std::string expected_status(ConsumeArg());
            if (em::Proc::to_string(status) != expected_status)
                throw std::runtime_error("The status of a read operation was `" + std::string(em::Proc::to_string(status)) + "`, but expected `" + expected_status + "`." + (status == em::Proc::IoResult::error ? " The error was: `" + in.ErrorMessage() + "`." : ""));

            if (status == em::Proc::IoResult::end_of_buffer && data != expected_data)
                throw std::runtime_error("Read operation produced `" + data + "`, but expected `" + expected_data + "`.");
        }
        else if (action == "close_write_pipe")
        {
            pipe_stdin = {};
        }
        else if (action == "close_read_pipe")
        {
            (use_stderr ? pipe_stderr : pipe_stdout) = {};
        }
        else if (action == "nonblocking_writes")
        {
            pipe_stdin.SetBlocking(false);
        }
        else if (action == "nonblocking_reads")
        {
            (use_stderr ? pipe_stderr : pipe_stdout).SetBlocking(false);
        }
        else
        {
            throw std::runtime_error("Bad command, got `" + std::string(action) + "` but expected one of: `wait N`, `use_stdout`, `use_stderr`, `write data status`, `read data status`, `close_write_pipe`, `close_read_pipe`, `nonblocking_writes`, `nonblocking_reads`.");
        }
    }

    proc.BlockUntilExit();
    proc.ThrowError();
    proc.ExitReason()->ThrowError();
}
