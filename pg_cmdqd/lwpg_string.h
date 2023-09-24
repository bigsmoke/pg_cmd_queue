#ifndef LWPG_STRING_H
#define LWPG_STRING_H

#include <initializer_list>
#include <map>
#include <optional>
#include <string>

namespace lwpg
{

std::string double_quote(const std::string &unquoted);

std::string composite_value(std::initializer_list<std::string> field_values);
std::string composite_value(std::initializer_list<std::optional<std::string>> field_values);

std::map<std::string, std::string> from_composite_value(const std::string &text);

}

#endif // LWPG_STRING_H
