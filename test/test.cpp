#include <em/process.hpp>
#include <iostream>

int main(int, char **argv)
{
    em::Proc::OutputStream out;

    em::Proc::Params params;
    params.command = argv + 1;
    em::Proc::MakePipe(out, params.stdin_stream, em::Proc::Inherit::always);

    em::Proc::Process proc = std::move(params);
    proc.ThrowError();

    std::string data = "Hello!";
    std::size_t pos = 0;
    em::Proc::IoResult r;
    while ((r = out.WriteUnbuffered(data, pos)) == em::Proc::IoResult::ok) {}
    std::cout << "Wrote " << pos << " bytes: " << to_string(r) << '\n';
    out = {};
    proc.BlockUntilExit();
    proc.ThrowError();
    proc.ExitReason()->ThrowError();
}
