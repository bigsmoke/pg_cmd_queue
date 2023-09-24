#include <iostream>
#include <stdexcept>
#include <string>

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

const std::string USAGE = R"(
nixtestcmd [--exit-code <exit_code>|--exit-signal <signal_no>] [--close-stdin] [--close-stdout] [--close-stderr]
)";

class CmdLineParseError : std::exception
{
    std::string _message;

public:
    CmdLineParseError(const std::string &msg)
        : _message(msg) {}

    const char *what() const noexcept override
    {
        return _message.data();
    }
};


int main(int argc, char** argv)
{
    try
    {
        for (int i = 1; i < argc; i++)
        {
            std::string arg(argv[i]);
            if (arg == "--exit-code")
            {
                if (i == argc)
                    throw std::runtime_error("Missing argument to --exit-code option");
                int exit_code = std::stoi(argv[++i]);
                exit(exit_code);
            }
            else if (arg == "--exit-signal")
            {
                if (i == argc)
                    throw std::runtime_error("Missing argument to --exit-signal option");
                int exit_signal = std::stoi(argv[++i]);
                raise(exit_signal);
            }
            else if (arg == "--close-stdin")
            {
                close(STDIN_FILENO);
            }
            else if (arg == "--close-stdout")
            {
                close(STDOUT_FILENO);
            }
            else if (arg == "--close-stderr")
            {
                close(STDOUT_FILENO);
            }
            else if (arg == "--stdout-line")
            {
                if (i == argc)
                    throw CmdLineParseError(std::string("Missing argument to: ") + argv[i]);
                std::cout << argv[++i] << std::endl;
            }
            else if (arg == "--stderr-line")
            {
                if (i == argc)
                    throw CmdLineParseError(std::string("Missing argument to: ") + argv[i]);
                std::cerr << argv[++i] << std::endl;
            }
            else if (arg == "--echo-stdin")
            {
                std::cout << std::cin.rdbuf();
            }
            else if (arg == "--echo-env-var")
            {
                if (i == argc)
                    throw CmdLineParseError(std::string("Missing argument to: ") + argv[i]);
                std::cout << getenv(argv[++i]);
            }
            else throw CmdLineParseError(std::string("Unrecognized argument: ") + argv[i]);
        }
    }
    catch (const CmdLineParseError &err)
    {
        std::cerr << err.what();
        exit(254);
    }
}
