#include "lwpg_hstore.h"

std::unordered_map<std::string, std::string> lwpg::hstore_to_unordered_map(std::string input)
{
    std::unordered_map<std::string, std::string> result;

    bool in_quotes = false;
    std::string::size_type key_start, key_end, val_start, val_end;
    key_start = key_end = val_start = val_end = 0;

    for (std::string::size_type i = 0; i < input.size(); ++i)
    {
        if (input[i] == '"' and (i == 0 or input[i-1] != '\\'))
        {
            in_quotes = not in_quotes;
            if (in_quotes)
            {
                if (key_start == 0)
                {
                    key_start = i+1;
                }
                else if (val_start == 0)
                {
                    val_start = i+1;
                }
            }
            else
            {
                if (key_end == 0)
                {
                    key_end = i-1;
                }
                else if (val_end == 0)
                {
                    val_end = i-1;
                    result[input.substr(key_start, key_end-key_start)] = input.substr(val_start, val_end-val_start);
                    key_start = key_end = val_start = val_end = 0;
                }
            }
        }
        else if ((not in_quotes) && input[i] == '=' && input[i+1] == '>')
        {
            ++i;
        }
    }

    return result;
}
