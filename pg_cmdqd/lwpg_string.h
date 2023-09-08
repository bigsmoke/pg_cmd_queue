#ifndef LWPG_STRING_H
#define LWPG_STRING_H

#include <initializer_list>
#include <optional>
#include <string>

namespace lwpg
{

std::string double_quote(const std::string &unquoted);

std::string composite_value(std::initializer_list<std::string> field_values);
std::string composite_value(std::initializer_list<std::optional<std::string>> field_values);

}

#endif // LWPG_STRING_H
