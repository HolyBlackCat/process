#include <em/process.hpp>

#include <stdexcept>

// Usage:
//     redirects {null,inherit,{file,file_stream} __} {null,inherit,merge,{file,file_stream} __}x2
//               stdin                                stdout, stderr

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

    { // stdin
        std::string_view arg = ConsumeArg();

        if (arg == "null")
        {
            params.stdin_stream = {};
        }
        else if (arg == "inherit")
        {
            params.stdin_stream = em::Proc::inherit;
        }
        else if (arg == "file")
        {
            params.stdin_stream = ConsumeArg();
        }
        else if (arg == "file_stream")
        {
            params.stdin_stream = em::Proc::OpenInputFile(ConsumeArg(), em::Proc::Inherit::as_stdio_stream);
        }
        else
        {
            throw std::runtime_error("`stdin` redirect must be one of: `null`, `inherit`, `file __`, `file_stream __`.");
        }
    }

    auto OutputStream = [&](std::string name, em::Proc::OutputStreamAttachment &target)
    {
        std::string_view arg = ConsumeArg();

        if (arg == "null")
        {
            target = {};
        }
        else if (arg == "inherit")
        {
            target = em::Proc::inherit;
        }
        else if (arg == "merge")
        {
            target = em::Proc::merge_output;
        }
        else if (arg == "file")
        {
            target = ConsumeArg();
        }
        else if (arg == "file_stream")
        {
            target = em::Proc::OpenOutputFile(ConsumeArg(), em::Proc::Inherit::as_stdio_stream);
        }
        else if (arg == "file_desc")
        {
            // Not testing all the different parameters here, `file_io` does that.
            target = em::Proc::OpenOutputFile(em::Proc::OutputFileDesc{.filename = ConsumeArg()}, em::Proc::Inherit::as_stdio_stream);
        }
        else
        {
            throw std::runtime_error("`" + name + "` redirect must be one of: `null`, `inherit`, `merge`, `file __`, `file_stream __`.");
        }
    };
    OutputStream("stdout", params.stdout_stream);
    OutputStream("stderr", params.stderr_stream);

    params.command = argv;

    em::Proc::Process process = std::move(params);
    process.ThrowError();
    process.BlockUntilExit();
    process.ThrowError();
    process.ExitReason()->ThrowError();
}
