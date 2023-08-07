#include "lwpg_hstore.h"

std::unordered_map<std::string, std::string> lwpg::hstore_to_unordered_map(std::string input)
{
    std::unordered_map<std::string, std::string> result;

    std::string key, value;
    bool in_quotes = false;
    std::string::size_type key_start, key_end, val_start, val_end = 0;

    for (std::string::size_type i = 0; i < input.size(); ++i)
    {
        if (input[i] == '"' && (i == 0 || input[i-1] != '\\')) {
            in_quotes = not in_quotes;
        }
        else if ((not in_quotes) && input[i] == '=' && input[i+1] == '>') {
            ++i;
        }
    }

    return result;
}
