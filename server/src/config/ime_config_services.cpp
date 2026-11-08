// 在线服务配置：翻译服务（腾讯 TMT、自定义翻译、小牛翻译）和它们共用的网络代理。
#include "config/ime_config_internal.h"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <shared_mutex>
#include <string>

using namespace ime_config_detail;

const TencentTmtConfig &GetConfiguredTencentTmt()
{
    return g_tencent_tmt;
}

bool SetConfiguredTencentTmtString(const std::string &key, const std::string &value)
{
    std::string *target = nullptr;
    if (key == "secret_id")
        target = &g_tencent_tmt.secret_id;
    else if (key == "secret_key")
        target = &g_tencent_tmt.secret_key;
    else if (key == "region")
        target = &g_tencent_tmt.region;
    else if (key == "target_language" && IsValidTranslationTargetLanguage(value))
        target = &g_tencent_tmt.target_language;
    if (!target || !WriteConfiguredValue("tencent_tmt", key, EscapeTomlBasicString(value)))
        return false;
    *target = value;
    PublishTranslationConfig();
    return true;
}

const CustomTranslationConfig &GetConfiguredCustomTranslation()
{
    return g_custom_translation;
}

// 旧的 enabled 开关不再单独落盘，改成切换 [translation].provider：打开即选中该服务，关掉当前
// 生效的服务回落到腾讯云，和旧版两个布尔都为 false 时的效果一致。
static bool SetTranslationProviderFromLegacyBool(const std::string &provider, bool enabled)
{
    if (enabled)
        return SetConfiguredTranslationString("provider", provider);
    if (g_translation_provider != provider)
        return true;
    return SetConfiguredTranslationString("provider", "tencent");
}

bool SetConfiguredCustomTranslationBool(const std::string &key, bool value)
{
    return key == "enabled" && SetTranslationProviderFromLegacyBool("custom", value);
}

bool SetConfiguredCustomTranslationString(const std::string &key, const std::string &value)
{
    std::string *target = nullptr;
    if (key == "endpoint")
        target = &g_custom_translation.endpoint;
    else if (key == "api_key")
        target = &g_custom_translation.api_key;
    if (!target || !WriteConfiguredValue("custom_translation", key, EscapeTomlBasicString(value)))
        return false;
    *target = value;
    PublishTranslationConfig();
    return true;
}

const AliyunMtConfig &GetConfiguredAliyunMt()
{
    return g_aliyun_mt;
}

bool SetConfiguredAliyunMtBool(const std::string &key, bool value)
{
    return key == "enabled" && SetTranslationProviderFromLegacyBool("aliyun", value);
}

bool SetConfiguredAliyunMtString(const std::string &key, const std::string &value)
{
    std::string *target = nullptr;
    if (key == "secret_id")
        target = &g_aliyun_mt.secret_id;
    else if (key == "secret_key")
        target = &g_aliyun_mt.secret_key;
    if (!target || !WriteConfiguredValue("aliyun_mt", key, EscapeTomlBasicString(value)))
        return false;
    *target = value;
    PublishTranslationConfig();
    return true;
}

const NiuTransConfig &GetConfiguredNiuTrans()
{
    return g_niutrans;
}

bool SetConfiguredNiuTransBool(const std::string &key, bool value)
{
    return key == "enabled" && SetTranslationProviderFromLegacyBool("niutrans", value);
}

bool SetConfiguredNiuTransString(const std::string &key, const std::string &value)
{
    std::string *target = nullptr;
    if (key == "app_id")
        target = &g_niutrans.app_id;
    else if (key == "apikey")
        target = &g_niutrans.apikey;
    if (!target || !WriteConfiguredValue("niutrans", key, EscapeTomlBasicString(value)))
        return false;
    *target = value;
    PublishTranslationConfig();
    return true;
}

bool IsValidTranslationProvider(const std::string &provider)
{
    return provider == "tencent" || provider == "aliyun" || provider == "niutrans" || provider == "custom";
}

namespace
{
std::mutex g_translation_snapshot_mutex;
std::shared_ptr<const TranslationConfig> g_translation_snapshot = std::make_shared<TranslationConfig>();
} // namespace

void ime_config_detail::PublishTranslationConfig()
{
    auto snapshot = std::make_shared<TranslationConfig>();
    snapshot->provider = g_translation_provider;
    snapshot->tencent = g_tencent_tmt;
    snapshot->aliyun = g_aliyun_mt;
    snapshot->custom = g_custom_translation;
    snapshot->niutrans = g_niutrans;
    std::lock_guard<std::mutex> lock(g_translation_snapshot_mutex);
    g_translation_snapshot = std::move(snapshot);
}

std::shared_ptr<const TranslationConfig> GetConfiguredTranslation()
{
    std::lock_guard<std::mutex> lock(g_translation_snapshot_mutex);
    return g_translation_snapshot;
}

bool SetConfiguredTranslationString(const std::string &key, const std::string &value)
{
    if (key != "provider" || !IsValidTranslationProvider(value))
        return false;
    // 旧布尔与 provider 一次写入；旧版不认识的提供方对应布尔保持 false。
    const bool tencent = value == "tencent";
    const bool aliyun = value == "aliyun";
    const bool niutrans = value == "niutrans";
    const bool custom = value == "custom";
    if (!WriteConfiguredValues({{"translation", key, EscapeTomlBasicString(value)},
                                {"tencent_tmt", "enabled", tencent ? "true" : "false"},
                                {"aliyun_mt", "enabled", aliyun ? "true" : "false"},
                                {"niutrans", "enabled", niutrans ? "true" : "false"},
                                {"custom_translation", "enabled", custom ? "true" : "false"}}))
        return false;
    g_translation_provider = value;
    g_tencent_tmt.enabled = tencent;
    g_aliyun_mt.enabled = aliyun;
    g_niutrans.enabled = niutrans;
    g_custom_translation.enabled = custom;
    PublishTranslationConfig();
    return true;
}

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
