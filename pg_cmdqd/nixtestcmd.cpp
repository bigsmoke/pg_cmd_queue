#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

const std::string USAGE = R"(
nixtestcmd
    {
        --stderr-line |
        --stdout-line |
        --echo-stdin |
        --echo-env-var <var_name> |
        --sleep-ms <msec> |
        --close-stdin |
        --close-stdout |
        --close-stderr |
    }
    [--exit-code <exit_code> | --exit-signal <signal_no>]
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
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL) | O_NONBLOCK);
    fcntl(STDERR_FILENO, F_SETFL, fcntl(STDERR_FILENO, F_GETFL) | O_NONBLOCK);

    try
    {
        for (int i = 1; i < argc; i++)
        {
            std::string arg(argv[i]);
            if (arg == "--help" or arg == "-h")
            {
                std::cout << USAGE;
                exit(0);
            }
            if (arg == "--exit-code")
            {
                if (i == argc)
                    throw std::runtime_error("Missing argument to --exit-code option");
                //int exit_code = std::stoi(argv[++i]);
                int exit_code = 0;
                exit(exit_code);
            }
            else if (arg == "--exit-signal")
            {
                if (i == argc - 1)
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
                if (i == argc - 1)
                    throw CmdLineParseError(std::string("Missing argument to: ") + argv[i]);
                std::cout << argv[++i] << std::endl;
            }
            else if (arg == "--stderr-line")
            {
                if (i == argc - 1)
                    throw CmdLineParseError(std::string("Missing argument to: ") + argv[i]);
                std::cerr << argv[++i] << std::endl;
            }
            else if (arg == "--echo-stdin")
            {
                std::cout << std::cin.rdbuf() << std::flush;
            }
            else if (arg == "--echo-env-var")
            {
                if (i == argc - 1)
                    throw CmdLineParseError(std::string("Missing argument to: ") + argv[i]);
                if (getenv(argv[++i]) == nullptr)
                {
                    std::cerr << "Environment variable `" << argv[i] << "` not set." << std::endl;
                    exit(253);
                }
                std::cout << getenv(argv[i]) << std::endl;
            }
            else if (arg == "--sleep-ms")
            {
                if (i == argc - 1)
                    throw CmdLineParseError(std::string("Missing argument to: ") + argv[i]);
                int msec = std::stoi(argv[++i]);
                std::this_thread::sleep_for(std::chrono::milliseconds(msec));
            }
            else throw CmdLineParseError(std::string("Unrecognized argument: '") + argv[i] + "'");
        }
    }
    catch (const CmdLineParseError &err)
    {
        std::cerr << err.what();
        std::cerr << USAGE;
        exit(254);
    }
}
