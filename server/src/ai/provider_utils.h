#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ProviderUtils
{
std::string NormalizeProviderId(std::string_view provider);
// DeepSeek thinking levels: disabled | low | high | max (the latter three are sent as reasoning_effort).
bool IsValidThinkingLevel(std::string_view level);
// True only for DeepSeek with a level other than disabled; every other case keeps thinking off.
bool UsesDeepSeekThinking(std::string_view provider, std::string_view level);

// Which service actually answers at an endpoint. Vendor-specific request fields (DeepSeek's
// thinking.type, SiliconFlow's enable_thinking) have to key off this rather than off the provider
// label chosen in the settings UI: a slot can hold any OpenAI-compatible endpoint, and sending
// thinking.type to a service that does not accept it makes it reject the whole request - MiniMax
// answers "model requires adaptive thinking" and the suggestion never comes back.
enum class ChatVendor
{
    Other,
    DeepSeek,
    SiliconFlow,
    Groq,
    OpenAI
};

ChatVendor VendorFromChatEndpoint(std::string_view endpoint);

// The vendor that owns the endpoint, or ChatVendor::Other when the label says something the
// endpoint does not. Used to keep DeepSeek/SiliconFlow fields off foreign endpoints.
ChatVendor VendorForRequest(std::string_view provider, std::string_view endpoint);

// Same question as UsesDeepSeekThinking, but resolved from the endpoint that will really be
// called, so a mislabelled slot cannot turn thinking on (or off) for the wrong service.
bool UsesDeepSeekThinking(std::string_view provider, std::string_view endpoint, std::string_view level);
bool IsPlaceholderToken(std::string_view token);
std::string UsableToken(std::string_view token);
} // namespace ProviderUtils
