#pragma once

#include "ai/ai_assistant_request.h"
#include "ai/chat_request_params.h"

#include <nlohmann/json.hpp>
#include <string>

namespace ApiCredentialTest::detail
{
// 探测发出去的必须是真实联想请求，而不是另一个「看起来差不多」的请求体。
// 以前这里手写 {"messages":[user "Reply OK"]}，与 BuildRequestBody 各自演化：
// 探测没有 system 提示词、没有 response_format、输出上限 1，而验收却按联想格式卡，
// 于是任何按字面回答 "OK" 的模型都会被判成「回复格式不对」（issue #1265）。
// 复用一个构造器之后，提示词、输出格式策略、token 上限和 thinking 分支都不会再跑偏。
//
// 拼音与上下文用固定样例：探测要的是一次可复现的联想，不是用户的真实输入。
inline AiAssistant::Request BuildChatProbeRequest(const AiAssistantConfig &config)
{
    AiAssistant::Request request;
    request.pinyin_segments = {"shi", "shi"};
    request.context = "方案已经通过，明天开始";
    request.config = config;
    return request;
}

inline nlohmann::json BuildChatProbeRequestBody(const AiAssistantConfig &config)
{
    return AiAssistant::detail::BuildRequestBody(BuildChatProbeRequest(config));
}

// 超时也要跟真实请求一致：推理模型在正式使用时有 30 秒，探测用 15 秒会让它先超时。
inline long ChatProbeTimeoutMs(const AiAssistantConfig &config)
{
    return AiAssistant::detail::RequestTimeoutMs(config);
}

} // namespace ApiCredentialTest::detail
