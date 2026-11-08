#pragma once

#include <optional>
#include <string>
#include <string_view>

// config.toml 里的凭证以 DPAPI（当前用户范围）密文落盘，只有同一个 Windows 账户能解开。
namespace CredentialProtection
{
bool IsProtected(std::string_view stored);

// 空值、出厂占位符（<...>）和测试用的 FAKESECRET_ 前缀都不是可用的凭证。
inline bool IsPlaceholderCredential(std::string_view value)
{
    if (value.empty())
        return true;
    if (value.front() == '<' && value.back() == '>')
        return true;
    return value.find("FAKESECRET_") == 0;
}

// 返回带 "dpapi:" 前缀的密文；DPAPI 调用失败时返回 nullopt。
std::optional<std::string> Protect(const std::string &plaintext);

// 未加密的值（旧版本写下的、用户手工填的）原样返回；密文解不开（配置被拷到别的账户或机器）返回 nullopt。
std::optional<std::string> Unprotect(const std::string &stored);
} // namespace CredentialProtection
