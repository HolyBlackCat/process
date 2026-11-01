// A simple test program that reads `stdin` and writes the contents to `stdout` and `stderr`. Used to test IO redirects.
// Takes no arguments.

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1)
        throw std::runtime_error("Expected no arguments.");

    std::ostringstream ss;
    ss << std::cin.rdbuf();
    std::string str = std::move(ss).str();

    std::cout << "stdout[" << str << "]\n" << std::flush;
    std::clog << "stderr[" << str << "]\n" << std::flush;
}
