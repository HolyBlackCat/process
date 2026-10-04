#include <em/process.hpp>
#include <iostream>

int main(int, char **argv)
{
    struct Guard
    {
        ~Guard() {std::cout << "End!\n";}
    };
    Guard guard;

    em::Proc::Params params;
    params.command = argv + 1;

    std::cout << "Begin!\n";
    em::Proc::Process proc = std::move(params);
    if (proc.HasError())
    {
        std::cout << "Failed: " << proc.ErrorMessage() << '\n';
        return 1;
    }
    std::cout << "...\n";
    proc.BlockUntilExit();
    std::cout << proc.ExitReason().value().ToString() << '\n';
}
