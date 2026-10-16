#include <em/process.hpp>
#include <iostream>

int main(int, char **argv)
{
    em::Proc::Params params;
    params.command = argv + 1;
    // params.stdout_stream = em::Proc::OpenOutputFile("1.txt");
    // params.stderr_stream = em::Proc::merge_output;
    params.stdout_stream = em::Proc::merge_output;
    params.stderr_stream = em::Proc::OpenOutputFile("1.txt");

    em::Proc::Process proc = std::move(params);
    if (proc.HasError())
    {
        std::cout << "Failed: " << proc.ErrorMessage() << '\n';
        return 1;
    }

    proc.BlockUntilExit();
    std::cout << proc.ExitReason().value().ToString() << '\n';
}
