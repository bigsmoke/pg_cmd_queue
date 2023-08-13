#include "envp_guard.h"

#include <string.h>

envp_guard::envp_guard(const std::unordered_map<std::string, std::string> &envp)
{
    argc = envp.size();
    c_envp = new char*[argc+1];
    memset(c_envp, 0, (argc+1)*sizeof(char*));

    int i = 0;
    for (const std::pair<const std::string, std::string> &var : envp)
    {
        int env_str_size = var.first.size() + 1 + var.second.size() + 1;
        c_envp[i++] = new char[env_str_size];
        memset(c_envp[i], 0, env_str_size*sizeof(char));
        strncpy(c_envp[i], var.first.c_str(), var.first.size());
        c_envp[i][var.first.size()] = '=';
        strcpy(c_envp[i]+var.first.size()+1, var.second.c_str());
    }
}

envp_guard::~envp_guard()
{
    if (argc == -1) return;

    for (int i = 0; i < (argc+1); i++)
        delete[] c_envp[i];
    delete [] c_envp;
}

char * const * envp_guard::get()
{
    return c_envp;
}
