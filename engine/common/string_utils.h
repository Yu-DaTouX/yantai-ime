#pragma once

#include <cstddef>
#include <string>

namespace CommonUtils
{
std::wstring string_to_wstring(const std::string &str);
std::string wstring_to_string(const std::wstring &wstr);

// 按码位计的字符数；count_utf8_chars 走 utf8::distance，count_han_chars 用长度字节自行
// 计数（遇到不完整的 UTF-8 序列也不中断），两者对合法输入结果相同。
std::string::size_type count_utf8_chars(const std::string &text);
std::string::size_type count_han_chars(const std::string &words);
// 第一个 / 最后一个汉字；没有汉字时返回空串。
std::string get_first_han_char(const std::string &words);
std::string get_last_han_char(const std::string &words);
} // namespace CommonUtils
