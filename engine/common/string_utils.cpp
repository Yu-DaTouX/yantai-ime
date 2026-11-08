#include "string_utils.h"

#include <cstdint>
#include <iterator>

#include <utf8.h>

namespace CommonUtils
{
namespace
{
bool is_han_code_point(std::uint32_t code_point)
{
    return code_point == 0x3007 || (code_point >= 0x3400 && code_point <= 0x4DBF) ||
           (code_point >= 0x4E00 && code_point <= 0x9FFF) || (code_point >= 0xF900 && code_point <= 0xFAFF) ||
           (code_point >= 0x20000 && code_point <= 0x2FA1F) || (code_point >= 0x30000 && code_point <= 0x323AF);
}

std::string::size_type first_char_size(const std::string &words)
{
    std::size_t cplen = 1;
    if ((words[0] & 0xf8) == 0xf0)
    {
        cplen = 4;
    }
    else if ((words[0] & 0xf0) == 0xe0)
    {
        cplen = 3;
    }
    else if ((words[0] & 0xe0) == 0xc0)
    {
        cplen = 2;
    }
    if (cplen > words.length())
    {
        cplen = 1;
    }
    return cplen;
}
} // namespace

std::wstring string_to_wstring(const std::string &str)
{
    std::u16string utf16result;
    utf8::utf8to16(str.begin(), str.end(), std::back_inserter(utf16result));
    return std::wstring(utf16result.begin(), utf16result.end());
}

std::string wstring_to_string(const std::wstring &wstr)
{
    std::string result;
    utf8::utf16to8(wstr.begin(), wstr.end(), std::back_inserter(result));
    return result;
}

std::string::size_type count_utf8_chars(const std::string &text)
{
    return utf8::distance(text.begin(), text.end());
}

std::string::size_type count_han_chars(const std::string &words)
{
    std::string::size_type index = 0;
    std::string::size_type count = 0;
    while (index < words.size())
    {
        index += first_char_size(words.substr(index, words.size() - index));
        ++count;
    }
    return count;
}

std::string get_first_han_char(const std::string &words)
{
    auto it = words.begin();
    const auto end = words.end();
    while (it != end)
    {
        const auto start = it;
        const auto code_point = utf8::next(it, end);
        if (is_han_code_point(code_point))
        {
            return std::string(start, it);
        }
    }
    return {};
}

std::string get_last_han_char(const std::string &words)
{
    auto it = words.begin();
    const auto end = words.end();
    std::string result;
    while (it != end)
    {
        const auto start = it;
        const auto code_point = utf8::next(it, end);
        if (is_han_code_point(code_point))
        {
            result.assign(start, it);
        }
    }
    return result;
}
} // namespace CommonUtils
