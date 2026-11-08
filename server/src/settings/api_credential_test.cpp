#include "api_credential_test.h"

#include "cloud/aliyun_mt.h"
#include "cloud/custom_translation.h"
#include "cloud/niutrans_translation.h"
#include "cloud/tencent_tmt.h"
#include "cloud/translation_gloss.h"

#include <string>
#include <vector>

namespace ApiCredentialTest
{
namespace
{
std::string Value(const ApiCredentialTest::Request &request, const char *key)
{
    const auto found = request.config.find(key);
    return found == request.config.end() ? std::string{} : CloudTranslation::TrimSecret(found->second);
}

ApiCredentialTest::Result TestTranslation(const ApiCredentialTest::Request &request)
{
    std::vector<std::string> translated;
    if (request.service == "translation.tencent")
    {
        const TencentTmt::Credentials credentials{Value(request, "secretId"), Value(request, "secretKey"),
                                                  "ap-guangzhou"};
        if (!CloudTranslation::IsUsableSecret(credentials.secret_id) ||
            !CloudTranslation::IsUsableSecret(credentials.secret_key))
            return {false, "请先填写有效的 SecretId 和 SecretKey。"};
        translated = TencentTmt::TextTranslateBatch(credentials, {"测试"}, "zh", "en");
    }
    else if (request.service == "translation.aliyun")
    {
        const AliyunMt::Credentials credentials{Value(request, "accessKeyId"), Value(request, "accessKeySecret")};
        if (!AliyunMt::IsUsableCredentials(credentials))
            return {false, "请先填写有效的 AccessKey ID 和 AccessKey Secret。"};
        translated = AliyunMt::TextTranslateBatch(credentials, {"测试"}, "zh", "en");
    }
    else if (request.service == "translation.niutrans")
    {
        const NiuTransTranslation::Config config{Value(request, "appId"), Value(request, "apiKey")};
        if (!NiuTransTranslation::IsUsableConfig(config))
            return {false, "请先填写有效的 APP ID 和 API Key。"};
        translated = NiuTransTranslation::TextTranslateBatch(config, {"测试"}, "zh", "en");
    }
    else
    {
        const CustomTranslation::Config config{Value(request, "endpoint"), Value(request, "apiKey")};
        if (!CustomTranslation::IsSupportedEndpoint(config.endpoint))
            return {false, "请先填写有效的翻译接口地址。"};
        translated = CustomTranslation::TextTranslateBatch(config, {"测试"}, "ZH", "EN");
    }
    if (!translated.empty() && !translated.front().empty())
        return {true, "连接成功，翻译服务配置有效。"};
    return {false, "测试失败：服务未返回有效译文，请检查凭据、接口地址和网络。"};
}
} // namespace

Result Run(const Request &request)
{
    if (request.service.rfind("translation.", 0) == 0)
        return TestTranslation(request);
    return {false, "不支持的配置测试类型。"};
}
} // namespace ApiCredentialTest
