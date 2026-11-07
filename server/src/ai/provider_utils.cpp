#include "provider_utils.h"

#include <map>

namespace ProviderUtils
{
namespace
{
std::string Lower(std::string_view value)
{
    std::string result(value);
    for (char &ch : result)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return result;
}
} // namespace

std::string NormalizeProviderId(std::string_view provider)
{
    return Lower(provider);
}

bool IsValidThinkingLevel(std::string_view level)
{
    return level == "disabled" || level == "low" || level == "high" || level == "max";
}

bool UsesDeepSeekThinking(std::string_view provider, std::string_view level)
{
    return NormalizeProviderId(provider) == "deepseek" && IsValidThinkingLevel(level) && level != "disabled";
}

ChatVendor VendorFromChatEndpoint(std::string_view endpoint)
{
    // scheme://[user[:pass]@]host[:port]/path - only the host identifies the vendor.
    const size_t scheme = endpoint.find("://");
    if (scheme == std::string_view::npos)
        return ChatVendor::Other;
    std::string_view rest = endpoint.substr(scheme + 3);
    const size_t path = rest.find('/');
    if (path != std::string_view::npos)
        rest = rest.substr(0, path);
    const size_t at = rest.rfind('@');
    if (at != std::string_view::npos)
        rest = rest.substr(at + 1);
    const size_t colon = rest.find(':');
    if (colon != std::string_view::npos)
        rest = rest.substr(0, colon);
    const std::string host = Lower(rest);
    const auto owned_by = [&host](std::string_view domain) {
        return host == domain ||
               (host.size() > domain.size() && host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
                host[host.size() - domain.size() - 1] == '.');
    };
    if (owned_by("deepseek.com"))
        return ChatVendor::DeepSeek;
    if (owned_by("siliconflow.cn"))
        return ChatVendor::SiliconFlow;
    if (owned_by("groq.com"))
        return ChatVendor::Groq;
    if (owned_by("openai.com"))
        return ChatVendor::OpenAI;
    return ChatVendor::Other;
}

ChatVendor VendorForRequest(std::string_view provider, std::string_view endpoint)
{
    const ChatVendor actual = VendorFromChatEndpoint(endpoint);
    if (actual != ChatVendor::Other)
        return actual;
    // An unrecognised host is still an answer: it means "some other OpenAI-compatible service",
    // and vendor fields must stay off. Only a missing endpoint leaves nothing to judge by, so
    // that is the single case where the label is allowed to speak.
    const bool has_endpoint = endpoint.find("://") != std::string_view::npos;
    if (has_endpoint)
        return ChatVendor::Other;
    const std::string id = NormalizeProviderId(provider);
    if (id == "deepseek")
        return ChatVendor::DeepSeek;
    if (id == "siliconflow")
        return ChatVendor::SiliconFlow;
    if (id == "groq")
        return ChatVendor::Groq;
    if (id == "openai")
        return ChatVendor::OpenAI;
    return ChatVendor::Other;
}

bool UsesDeepSeekThinking(std::string_view provider, std::string_view endpoint, std::string_view level)
{
    return VendorForRequest(provider, endpoint) == ChatVendor::DeepSeek && IsValidThinkingLevel(level) &&
           level != "disabled";
}

bool IsPlaceholderToken(std::string_view token)
{
    if (token.empty())
        return true;
    // Empty values and shipped placeholders are never usable credentials.
    if (token.front() == '<' && token.back() == '>')
        return true;
    return token.find("FAKESECRET_") == 0;
}

std::string UsableToken(std::string_view token)
{
    return IsPlaceholderToken(token) ? std::string() : std::string(token);
}

} // namespace ProviderUtils
