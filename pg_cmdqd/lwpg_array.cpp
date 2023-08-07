#include "lwpg_array.h"

#include <cassert>
#include <vector>

std::vector<std::string> lwpg::array_to_vector(const std::string &input)
{
    std::vector<std::string> result;
    int depth = 0;
    bool in_quotes = false;
    int item_num = 0;
    std::string::size_type start_pos = 0;
    std::string::size_type end_pos = 0;

#ifndef TESTING
    assert(input.at(0) == '{');
#endif

    for (std::string::size_type i = 0; i < input.size(); ++i)
    {
        if (input.at(i) == '{' && not in_quotes) {
            if (++depth == 1) {
                item_num++;
                start_pos = i+1;
            }
        }
        else if (input.at(i) == '}' && not in_quotes) {
            if (--depth == 1 && end_pos == 0) {
                end_pos = i-1;
            }
        }
        else if (input.at(i) == ',' && not in_quotes) {
            end_pos = i-1;
        }
        // We don't have to worry about `i-1 < 0`, because `input.at(i) == '{'`.
        else if (input.at(i) == '"' && input.at(i-1) != '\\') {
            if (in_quotes) {
                end_pos = i-1;
                in_quotes = false;
            }
            else {
                start_pos = i+1;
                in_quotes = true;
            }
        }

        if (start_pos > 0 && end_pos > 0) {
            result.push_back(input.substr(start_pos, end_pos-start_pos));
            start_pos = i+1;
            end_pos = 0;
        }
    }

    return result;
}
