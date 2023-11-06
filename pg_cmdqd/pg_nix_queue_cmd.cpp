#include <iostream>
#include <streambuf>
#include <string>
#include <tuple>
#include <vector>

#include <signal.h>
#include <unistd.h>

#include "nixqueuecmd.h"

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

void usage(const char* program_name, std::ostream &stream = std::cout)
{
    stream << R"(Run a nix_queue_cmd as if through the pg_cmdqd and output or execute the
UPDATE statement as it would be performed by the daemon.

Usage:
    )" << program_name << R"( [ options ] <queue_cmd_class> <cmd_id> [ <cmd_subid> ] [ -- <cmd_arg>... ]
    )" << program_name << R"( --help

Options:
    --output-update
    --exec-update
)";
}

int main(const int argc, const char *argv[])
{
    bool output_update_statement = false;
    bool exec_update_statement = false;
    std::string queue_cmd_class;
    std::string cmd_id;
    std::optional<std::string> cmd_subid;
    int cmd_meta_fields_identified = 0;
    std::vector<std::string> cmd_argv;

    try
    {
        for (int i = 1; i < argc; i++)
        {
            std::string arg(argv[i]);

            if (arg == "--help")
            {
                usage(argv[0]);
                exit(0);
            }
            else if (arg == "--output-update")
            {
                output_update_statement = true;
            }
            else if (arg == "--exec-update")
            {
                exec_update_statement = true;
            }
            else if (arg.substr(0, 2) != "--")
            {
                if (++cmd_meta_fields_identified == 1)
                    queue_cmd_class = argv[i];
                else if (cmd_meta_fields_identified == 2)
                    cmd_id = argv[i];
                else if (cmd_meta_fields_identified == 3)
                    cmd_subid = argv[i];
                else
                    throw CmdLineParseError("Expected only 3 cmd meta fields; got " + std::to_string(cmd_meta_fields_identified));
            }
            else if (arg == "--")
            {
                while (++i < argc)
                    cmd_argv.emplace_back(argv[i]);
            }
            else
            {
                throw CmdLineParseError("Unrecognized option \x1b[1m" + arg + "\x1b[22m.");
            }
        }

        if (cmd_meta_fields_identified < 2)
            throw CmdLineParseError("Got only " + std::to_string(cmd_meta_fields_identified) + " out of 2 or 3 fields required to uniquely identify a command in a command queue.");

        if (cmd_argv.size() < 1)
            throw CmdLineParseError("You need to specify the cmd_argv after the \x1b[1m--\x1b[22m option—at the very least the executable name of the command.");
    }
    catch (const CmdLineParseError &err)
    {
        std::cerr << "\x1b[31m" << err.what() << "\x1b[0m" << std::endl;
        usage(argv[0], std::cerr);
        exit(222);
    }

    std::string cmd_stdin = "";
    if (not isatty(STDIN_FILENO))
        cmd_stdin = std::string((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());

    NixQueueCmd nix_queue_cmd(queue_cmd_class, cmd_id, cmd_subid, cmd_argv, {}, cmd_stdin);

    std::shared_ptr<PG::conn> null_conn(nullptr);
    nix_queue_cmd.meta.stamp_start_time();
    nix_queue_cmd.run_cmd(null_conn, 0);
    nix_queue_cmd.meta.stamp_end_time();

    std::shared_ptr<PG::conn> conn = PQ::connectdb("");

    if (output_update_statement)
        std::cout << nix_queue_cmd.update_stmt(conn);

    if (nix_queue_cmd.cmd_exit_code.has_value())
        exit(nix_queue_cmd.cmd_exit_code.value());
    raise(nix_queue_cmd.cmd_term_sig.value());
}
