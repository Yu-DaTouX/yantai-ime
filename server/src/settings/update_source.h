#pragma once

// 自动更新与关于页共用的发布来源。仓库以后改名或搬走时只改 kRepository 一处：
// 更新清单、发布页、安装包资产和仓库里的文件链接都由它拼出来。
//
// 清单地址形如 https://github.com/<owner>/<repo>/releases/latest/download/update.json，
// GitHub 会先 302 到实际资产所在的地址，所以取清单的 WinHTTP 请求必须放开跟随重定向。

#include <string>

namespace update_source
{
// GitHub 上的 <owner>/<repo>，不带协议，也不带结尾斜杠。
inline constexpr char kRepository[] = "Yu-DaTouX/yantai-ime";

// 仓库根地址，例如 https://github.com/<owner>/<repo>。
inline std::string RepositoryUrl()
{
    return std::string("https://github.com/") + kRepository;
}

// 发布列表页。
inline std::string ReleasesPageUrl()
{
    return RepositoryUrl() + "/releases";
}

// 客户端轮询的更新清单。永远指向最新一个正式版 Release 的资产。
inline std::string ManifestUrl()
{
    return RepositoryUrl() + "/releases/latest/download/update.json";
}

// 仓库根地址拿去拼正则时用的形式：URL 里的正则元字符先转义。
inline std::string RegexEscapedRepositoryUrl()
{
    std::string escaped;
    for (const char ch : RepositoryUrl())
    {
        if (ch == '.' || ch == '+' || ch == '*' || ch == '?' || ch == '(' || ch == ')' || ch == '[' || ch == ']' ||
            ch == '{' || ch == '}' || ch == '^' || ch == '$' || ch == '|' || ch == '\\')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}
} // namespace update_source
