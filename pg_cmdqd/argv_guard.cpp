#include "argv_guard.h"

#include <string.h>

argv_guard::argv_guard(const std::vector<std::string> &argv)
{
    argc = argv.size();
    c_argv = new char*[argc+1];
    memset(c_argv, 0, (argc+1)*sizeof(char*));

    int i = 0;
    for (const std::string &arg : argv)
    {
        c_argv[i++] = new char[arg.size()+1];
        memset(c_argv[i], 0, (arg.size()+1)*sizeof(char));
        strcpy(c_argv[i], arg.c_str());
    }
}

argv_guard::~argv_guard()
{
    if (argc == -1) return;

    for (int i = 0; i < (argc+1); i++)
        delete[] c_argv[i];
    delete [] c_argv;
}

char * const * argv_guard::get()
{
    return c_argv;
}
