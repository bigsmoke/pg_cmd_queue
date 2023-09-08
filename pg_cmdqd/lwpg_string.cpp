#include "lwpg_string.h"

#include <regex>

std::string lwpg::double_quote(const std::string &unquoted)
{
    static std::regex re("\"|\\\\");

    return std::string("\"") + std::regex_replace(unquoted, re, "\\$&") + "\"";
}

std::string lwpg::composite_value(std::initializer_list<std::string> field_values)
{
    std::string composite_value = "(";

    int i = 0;
    for (const std::string &field_value : field_values)
    {
        if (i++ > 0)
            composite_value += ",";
        composite_value += lwpg::double_quote(field_value);
    }

    composite_value += ")";

    return composite_value;
}

std::string lwpg::composite_value(std::initializer_list<std::optional<std::string>> field_values)
{
    std::string composite_value = "(";

    int i = 0;
    for (const std::optional<std::string> &field_value : field_values)
    {
        if (i++ > 0)
            composite_value += ",";
        composite_value += field_value ? lwpg::double_quote(field_value.value()) : "NULL";
    }

    composite_value += ")";

    return composite_value;
}
