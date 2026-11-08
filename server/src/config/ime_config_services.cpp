// 网络代理：模型下载、更新检查等出站请求共用。
#include "config/ime_config_internal.h"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>

using namespace ime_config_detail;

std::string NormalizeNetworkProxyServer(const std::string &value)
{
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    const auto begin = std::find_if(value.begin(), value.end(), not_space);
    const auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    std::string text = begin < end ? std::string(begin, end) : std::string();
    std::string lowered = text;
    for (char &ch : lowered)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (lowered.rfind("http://", 0) == 0)
        text.erase(0, std::string("http://").size());
    else if (lowered.find("://") != std::string::npos)
        return {};
    while (!text.empty() && text.back() == '/')
        text.pop_back();

    const size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == text.size())
        return {};
    const std::string host = text.substr(0, colon);
    const std::string port = text.substr(colon + 1);
    if (port.size() > 5 || !std::all_of(port.begin(), port.end(), [](unsigned char ch) { return std::isdigit(ch); }))
        return {};
    const int port_number = std::stoi(port);
    if (port_number < 1 || port_number > 65535)
        return {};
    // 方括号 IPv6 字面量或主机名/IPv4；不收用户名密码（WinHTTP 的命名代理不认 user:pass@）。
    const bool bracketed = host.size() > 2 && host.front() == '[' && host.back() == ']' &&
                           std::all_of(host.begin() + 1, host.end() - 1,
                                       [](unsigned char ch) { return std::isxdigit(ch) || ch == ':' || ch == '.'; });
    const bool plain = std::all_of(host.begin(), host.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_';
    });
    if (!bracketed && !plain)
        return {};
    return host + ":" + std::to_string(port_number);
}

NetworkProxyConfig GetConfiguredNetworkProxy()
{
    std::lock_guard<std::mutex> lock(g_network_proxy_mutex);
    return g_network_proxy;
}

bool SetConfiguredNetworkString(const std::string &key, const std::string &value)
{
    std::string stored;
    if (key == "proxy_mode")
    {
        if (value != "system" && value != "none" && value != "custom")
            return false;
        stored = value;
    }
    else if (key == "proxy_server")
    {
        // 留空是合法的「未填写」；非空但解析不出 host:port 的一律拒绝，不把坏值写进配置。
        const bool blank = value.find_first_not_of(" \t") == std::string::npos;
        stored = blank ? std::string() : NormalizeNetworkProxyServer(value);
        if (!blank && stored.empty())
            return false;
    }
    else
    {
        return false;
    }
    if (!WriteConfiguredValue("network", key, EscapeTomlBasicString(stored)))
        return false;
    std::lock_guard<std::mutex> lock(g_network_proxy_mutex);
    (key == "proxy_mode" ? g_network_proxy.mode : g_network_proxy.server) = stored;
    return true;
}
