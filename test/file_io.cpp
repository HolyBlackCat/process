// A small program that tests the file IO functions.

// Usage:
// `file_io read "path" {expect_contents "value",expect_error "error"}
// `file_io read "path"
// `file_io write "path" {simple,{params,params_struct} existing:{keep,overwrite,error} allow_creating:{0,1} {"",no_append}} {value "value" expect_contents "value",except_error "error"}

#include <em/process.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <string>
#include <utility>

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

    em::Proc::InputStream in;
    em::Proc::OutputStream out;

    std::string arg;

    // Open the file.
    arg = ConsumeArg();
    bool is_write = false;
    std::string filename;
    if (arg == "read")
    {
        filename = ConsumeArg();
        in = em::Proc::OpenInputFile(filename, em::Proc::Inheritable::no);
    }
    else if ((is_write = arg == "write"))
    {
        filename = ConsumeArg();

        arg = ConsumeArg();
        if (arg == "simple")
        {
            out = em::Proc::OpenOutputFile(filename, em::Proc::Inheritable::no);
        }
        else if (bool use_params_struct = false; arg == "params" || (use_params_struct = arg == "params_struct"))
        {
            arg = ConsumeArg();
            em::Proc::ExistingFile existing{};
            if (arg == "existing:keep")
                existing = em::Proc::ExistingFile::keep;
            else if (arg == "existing:overwrite")
                existing = em::Proc::ExistingFile::overwrite;
            else if (arg == "existing:error")
                existing = em::Proc::ExistingFile::error;
            else
                throw std::runtime_error("Expected `existing:{keep,overwrite,error}` but got `" + arg + "`.");

            arg = ConsumeArg();
            bool allow_creating = false;
            if (arg == "allow_creating:0")
                allow_creating = false;
            else if (arg == "allow_creating:1")
                allow_creating = true;
            else
                throw std::runtime_error("Expected `allow_creating:{0,1}` but got `" + arg + "`.");

            arg = ConsumeArg();
            em::Proc::OutputFileFlags flags{};
            if (!arg.empty())
            {
                while (true)
                {
                    if (arg.starts_with("no_append"))
                    {
                        flags |= em::Proc::OutputFileFlags::no_append;
                        arg = arg.substr(9);
                    }
                    else
                    {
                        throw std::runtime_error("Failed to parse output file flags from `" + arg + "`");
                    }

                    if (arg.empty())
                        break;

                    if (!arg.starts_with(','))
                        throw std::runtime_error("Expected `,` after a flag but got `" + arg + "`");
                    arg = arg.substr(1);
                }
            }

            if (use_params_struct)
                out = em::Proc::OpenOutputFile(em::Proc::OutputFileDesc{.filename = filename, .existing = existing, .allow_creating = allow_creating, .flags = flags}, em::Proc::Inheritable::no);
            else
                out = em::Proc::OpenOutputFile(filename, em::Proc::Inheritable::no, existing, allow_creating, flags);
        }
        else
        {
            throw std::runtime_error("Expected `simple` or `params` or `params_struct`, but got `" + arg + "`.");
        }
    }
    else
        throw std::runtime_error("Expected `read` or `write`, got `" + arg + "`.");

    // Check the opening status.
    arg = ConsumeArg();
    if (arg == "expect_error")
    {
        em::Proc::BasicIoStream &stream = is_write ? static_cast<em::Proc::BasicIoStream &>(out) : static_cast<em::Proc::BasicIoStream &>(in);
        if (!stream.HasError())
            throw std::runtime_error("Expected a file opening error, but it opened fine.");

        arg = ConsumeArg();
        if (stream.ErrorMessage() != arg)
            throw std::runtime_error("Expected file opening error `" + arg + "` but got `" + stream.ErrorMessage() + "`.");

        if (stream)
            throw std::runtime_error("Expected the file to be null after the error, but it's not null.");
    }
    else if (!is_write && arg == "expect_contents")
    {
        if (in.HasError())
            throw std::runtime_error("Expected to open the file successfully, but got error `" + in.ErrorMessage() + "`.");
        if (!in)
            throw std::runtime_error("Expected to open the file successfully, but it's null, but holds no error message.");

        arg = ConsumeArg();

        std::string value;
        value.resize(arg.size() + 1); // +1 to catch eof properly.

        em::Proc::IoResult io_result = em::Proc::IoResult(-1);
        std::size_t pos = 0;
        while (in.ReadUnbuffered(value, pos, &io_result))
        {
            if (io_result != em::Proc::IoResult::ok)
                throw std::runtime_error("`ReadUnbuffered()` returned true but didn't set the result to `ok`.");
        }
        if (pos != arg.size())
            throw std::runtime_error("`ReadUnbuffered()` read " + std::to_string(pos) + " characters, but expected " + std::to_string(arg.size()) + ".");
        if (io_result != em::Proc::IoResult::end_reached)
            throw std::runtime_error("`ReadUnbuffered()` stopped with a status other than `end_reached`.");
        value.resize(arg.size()); // Trim the dummy last character.
        if (value != arg)
            throw std::runtime_error("Got a wrong result from the file. Expected `" + arg + "` but got `" + value + "`.");
    }
    else if (is_write && arg == "value")
    {
        if (out.HasError())
            throw std::runtime_error("Expected to open the file successfully, but got error `" + out.ErrorMessage() + "`.");
        if (!out)
            throw std::runtime_error("Expected to open the file successfully, but it's null, but holds no error message.");

        arg = ConsumeArg();

        em::Proc::IoResult io_result = em::Proc::IoResult(-1);
        std::size_t pos = 0;
        while (out.WriteUnbuffered(arg, pos, &io_result))
        {
            if (io_result != em::Proc::IoResult::ok)
                throw std::runtime_error("`WriteUnbuffered()` returned true but didn't set the result to `ok`.");
        }

        if (pos != arg.size())
            throw std::runtime_error("`WriteUnbuffered()` wrote " + std::to_string(pos) + " characters, but expected " + std::to_string(arg.size()) + ".");
        out = {}; // Close the file.

        arg = ConsumeArg();
        if (arg != "expect_contents")
            throw std::runtime_error("Expected `expect_contents` but got `" + arg + "`.");

        // Read back the file.
        std::ifstream f(filename);
        if (!f)
            throw std::runtime_error("Failed to open the newly created file at `" + filename + "`.");
        std::ostringstream ss;
        ss << f.rdbuf();
        if (!f)
            throw std::runtime_error("Error while reading contents of the newly opened file.");
        std::string contents = std::move(ss).str();

        arg = ConsumeArg();
        if (arg != contents)
            throw std::runtime_error("File contents didn't match. Expected `" + arg + "` but got `" + contents + "`.");
    }
    else
    {
        throw std::runtime_error("Expected one of: `expect_error`, or `expect_contents` (if `read`), or `value` (if `write`), but got `" + arg + "`.");
    }


    if (*argv)
        std::runtime_error(std::string("Too many arguments, starting from: ") + *argv);
}
