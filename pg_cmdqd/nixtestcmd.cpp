#include <stdexcept>
#include <string>

const std::string USAGE = R"(
nixtestcmd [--exit-code <exit_code>|--exit-signal <signal_no>] [--close-stdin] [--close-stdout] [--close-stderr]
)";

int main(int argc, char** argv)
{
    bool exit_normally = true;
    int exit_code = -1;
    int exit_signal = -1;
    bool close_stdin = false;
    bool close_stdout = false;
    bool close_stderr = false;

    for (int i = 0; i < argc; i++)
    {
        std::string arg(argv[i]);
        if (arg == "--exit-code")
        {
            if (i == argc)
                throw std::runtime_error("Missing argument to --exit-code option");
            exit_code = std::stoi(argv[++i]);
            exit_normally = true;
        }
        else if (arg == "--exit-signal")
        {
            if (i == argc)
                throw std::runtime_error("Missing argument to --exit-signal option");
            exit_signal = std::stoi(argv[++i]);
            exit_normally = false;
        }
        else if (arg == "--close-stdin")
        {
            close_stdin = true;
        }
        else if (arg == "--close-stdout")
        {
            close_stdout = true;
        }
        else if (arg == "--close-stderr")
        {
            close_stderr = true;
        }
    }

    if (exit_code >= 0 and exit_signal >= 0)
    {
        throw std::runtime_error("--exit-code and --exit-signal cannot be used together.");
    }
}
