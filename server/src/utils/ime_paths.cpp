#include "common_utils.h"

#include <aclapi.h>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sddl.h>
#include <shlobj.h>
#include <vector>

namespace
{
constexpr wchar_t kAppName[] = L"metasequoiaime";

bool PathHasEmptyComponent(const std::wstring &path)
{
    if (path.empty())
    {
        return true;
    }
    size_t index = 0;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        index = 2;
    }
    else if (path[0] == L'\\')
    {
        return true;
    }
    for (; index + 1 < path.size(); ++index)
    {
        if (path[index] == L'\\' && path[index + 1] == L'\\')
        {
            return true;
        }
    }
    return false;
}

bool IsUsableAbsolutePath(const std::wstring &path)
{
    if (PathHasEmptyComponent(path))
    {
        return false;
    }
    if (path.size() >= 2 && path[1] == L':')
    {
        return true;
    }
    return path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\';
}

std::wstring QueryEnvironmentW(const wchar_t *name)
{
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
    {
        return {};
    }
    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0 || written >= needed)
    {
        return {};
    }
    value.resize(written);
    return value;
}

// The installer records the data directory the user picked during setup. KEY_WOW64_64KEY is
// mandatory: a 32-bit reader would otherwise be redirected to Wow6432Node, where the 64-bit
// setup never wrote anything.
std::wstring QueryInstalledDataDir()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Metasequoia\\MetasequoiaIME", 0,
                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
    {
        return {};
    }

    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, L"DataDir", nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_SZ ||
        bytes < sizeof(wchar_t))
    {
        RegCloseKey(key);
        return {};
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    const LSTATUS status =
        RegQueryValueExW(key, L"DataDir", nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
    {
        return {};
    }
    // REG_SZ is not required to carry a terminator, and the stored length may or may not include
    // one; cut at the first NUL that is actually there.
    value.resize(std::wcslen(value.c_str()));
    return value;
}

std::wstring QueryKnownFolder(REFKNOWNFOLDERID folder_id)
{
    PWSTR known_path = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder_id, KF_FLAG_DEFAULT, nullptr, &known_path)) || !known_path)
    {
        return {};
    }
    std::wstring result(known_path);
    CoTaskMemFree(known_path);
    return result;
}

void ClearReadOnlyAttribute(const std::wstring &path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_READONLY) == 0)
    {
        return;
    }
    SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
}

bool GrantsFullAccess(ACCESS_MASK mask)
{
    return (mask & GENERIC_ALL) != 0 || (mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS;
}

// The SID of the user this process runs as. Caller frees with LocalFree. A data directory created by
// the elevated installer is owned by Administrators, so "owner rights" is not enough here: the ACL has
// to name the user itself, or a medium-integrity Server/TSF could lose access after the first start.
PSID QueryCurrentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return nullptr;
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    PSID result = nullptr;
    if (size != 0)
    {
        std::vector<BYTE> buffer(size);
        if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
        {
            const auto *user = reinterpret_cast<const TOKEN_USER *>(buffer.data());
            const DWORD sid_size = GetLengthSid(user->User.Sid);
            result = LocalAlloc(LPTR, sid_size);
            if (result && !CopySid(sid_size, result, user->User.Sid))
            {
                LocalFree(result);
                result = nullptr;
            }
        }
    }
    CloseHandle(token);
    return result;
}

// Whether the DACL already gives this SID full access. For a directory the ACE must also inherit, so
// files created later inherit the same restricted access.
bool HasFullAccessAce(PACL dacl, PSID sid, bool require_inheritable)
{
    ACL_SIZE_INFORMATION info{};
    if (!dacl || !GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation))
    {
        return false;
    }
    bool effective = false;
    bool inheritable = false;
    for (DWORD index = 0; index < info.AceCount; ++index)
    {
        void *raw = nullptr;
        if (!GetAce(dacl, index, &raw))
        {
            continue;
        }
        const auto *header = static_cast<const ACE_HEADER *>(raw);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
        {
            continue;
        }
        const auto *ace = static_cast<const ACCESS_ALLOWED_ACE *>(raw);
        if (!EqualSid(const_cast<DWORD *>(&ace->SidStart), sid) || !GrantsFullAccess(ace->Mask))
        {
            continue;
        }
        if ((header->AceFlags & INHERIT_ONLY_ACE) == 0)
        {
            effective = true;
        }
        if ((header->AceFlags & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) ==
            (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE))
        {
            inheritable = true;
        }
    }
    return effective && (inheritable || !require_inheritable);
}

bool HasAceForSid(PACL dacl, PSID sid)
{
    ACL_SIZE_INFORMATION info{};
    if (!dacl || !GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation))
    {
        return false;
    }
    for (DWORD index = 0; index < info.AceCount; ++index)
    {
        void *raw = nullptr;
        if (!GetAce(dacl, index, &raw))
        {
            continue;
        }
        const auto *header = static_cast<const ACE_HEADER *>(raw);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE && header->AceType != ACCESS_DENIED_ACE_TYPE)
        {
            continue;
        }
        const auto *ace = static_cast<const ACCESS_ALLOWED_ACE *>(raw);
        if (EqualSid(const_cast<DWORD *>(&ace->SidStart), sid))
        {
            return true;
        }
    }
    return false;
}

bool HasMediumNoWriteUpLabel(const std::wstring &path, PSID medium_sid)
{
    PACL sacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, nullptr, nullptr, nullptr,
                              &sacl, &descriptor) != ERROR_SUCCESS)
    {
        return false;
    }
    bool found = false;
    ACL_SIZE_INFORMATION info{};
    if (sacl && GetAclInformation(sacl, &info, sizeof(info), AclSizeInformation))
    {
        for (DWORD index = 0; index < info.AceCount && !found; ++index)
        {
            void *raw = nullptr;
            if (!GetAce(sacl, index, &raw) ||
                static_cast<const ACE_HEADER *>(raw)->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
            {
                continue;
            }
            const auto *ace = static_cast<const SYSTEM_MANDATORY_LABEL_ACE *>(raw);
            found = EqualSid(const_cast<DWORD *>(&ace->SidStart), medium_sid) &&
                    (ace->Mask & SYSTEM_MANDATORY_LABEL_NO_WRITE_UP) != 0;
        }
    }
    LocalFree(descriptor);
    return found;
}

// Restrict a data directory or one of its files to this user plus SYSTEM/Administrators. The DACL is
// protected, so the inherited BUILTIN\Users grant the elevated installer puts on the data directory no
// longer applies: another local account cannot read the user dictionary, statistics, clipboard history
// or config.toml containing tokens. The STILL-inheritable ACEs let dictionaries and later files under
// the directory keep working for this user's medium-integrity Server, TSF and engine processes.
bool SetOwnerOnlyDacl(const std::wstring &path)
{
    const bool directory = std::filesystem::is_directory(path);
    PSID user_sid = QueryCurrentUserSid();
    if (!user_sid)
    {
        return false;
    }

    PSID users_group = nullptr;
    ConvertStringSidToSidW(L"S-1-5-32-545", &users_group);

    bool already_private = false;
    PACL old_dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_dacl,
                              nullptr, &descriptor) == ERROR_SUCCESS)
    {
        already_private =
            HasFullAccessAce(old_dacl, user_sid, directory) && !(users_group && HasAceForSid(old_dacl, users_group));
        LocalFree(descriptor);
    }

    bool changed = false;
    if (!already_private)
    {
        PSID system_sid = nullptr;
        PSID admins_sid = nullptr;
        ConvertStringSidToSidW(L"S-1-5-18", &system_sid);
        ConvertStringSidToSidW(L"S-1-5-32-544", &admins_sid);

        const DWORD inheritance = directory ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
        EXPLICIT_ACCESSW entries[3]{};
        DWORD count = 0;
        const auto add = [&](PSID sid, TRUSTEE_TYPE type) {
            if (!sid)
            {
                return;
            }
            EXPLICIT_ACCESSW &access = entries[count++];
            access.grfAccessPermissions = GENERIC_ALL;
            access.grfAccessMode = GRANT_ACCESS;
            access.grfInheritance = inheritance;
            access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            access.Trustee.TrusteeType = type;
            access.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        };
        add(user_sid, TRUSTEE_IS_USER);
        add(system_sid, TRUSTEE_IS_WELL_KNOWN_GROUP);
        add(admins_sid, TRUSTEE_IS_WELL_KNOWN_GROUP);

        PACL new_dacl = nullptr;
        if (count > 0 && SetEntriesInAclW(count, entries, nullptr, &new_dacl) == ERROR_SUCCESS)
        {
            changed = SetNamedSecurityInfoW(const_cast<wchar_t *>(path.c_str()), SE_FILE_OBJECT,
                                            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                            nullptr, new_dacl, nullptr) == ERROR_SUCCESS;
            LocalFree(new_dacl);
        }
        if (system_sid)
        {
            LocalFree(system_sid);
        }
        if (admins_sid)
        {
            LocalFree(admins_sid);
        }
    }

    if (users_group)
    {
        LocalFree(users_group);
    }
    LocalFree(user_sid);
    return changed || already_private;
}

void ApplyOwnerModifyAndMediumIntegrity(const std::wstring &path)
{
    SetOwnerOnlyDacl(path);

    PSID medium_sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-16-8192", &medium_sid))
    {
        return;
    }
    if (HasMediumNoWriteUpLabel(path, medium_sid))
    {
        LocalFree(medium_sid);
        return;
    }
    const DWORD sacl_size = sizeof(ACL) + GetLengthSid(medium_sid) + sizeof(SYSTEM_MANDATORY_LABEL_ACE) + 32;
    std::vector<BYTE> sacl_buffer(sacl_size);
    auto *sacl = reinterpret_cast<PACL>(sacl_buffer.data());
    if (InitializeAcl(sacl, sacl_size, ACL_REVISION) &&
        AddMandatoryAce(sacl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, medium_sid))
    {
        SetNamedSecurityInfoW(const_cast<wchar_t *>(path.c_str()), SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, nullptr,
                              nullptr, nullptr, sacl);
    }
    LocalFree(medium_sid);
}

void EnsureMediumIntegrityWritableDirectory(const std::wstring &path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    ClearReadOnlyAttribute(path);
    ApplyOwnerModifyAndMediumIntegrity(path);
}

void EnsureMediumIntegrityWritableExistingPath(const std::wstring &path)
{
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }
    ClearReadOnlyAttribute(path);
    ApplyOwnerModifyAndMediumIntegrity(path);
}
} // namespace

namespace CommonUtils
{
std::wstring get_local_appdata_path_w()
{
    const std::wstring from_env = QueryEnvironmentW(L"LOCALAPPDATA");
    if (IsUsableAbsolutePath(from_env))
    {
        return from_env;
    }

    const std::wstring known = QueryKnownFolder(FOLDERID_LocalAppData);
    if (IsUsableAbsolutePath(known))
    {
        return known;
    }
    return {};
}

std::wstring get_ime_data_path_w()
{
    // Resolution order, shared with the engine (engine/core/data_path.h) and the TSF DLL:
    // explicit environment override, then the location chosen during setup, then the default
    // under LocalAppData. Resolved on every call, not cached: tests relocate the profile between
    // cases inside one process.
    const std::wstring from_env = QueryEnvironmentW(L"METASEQUOIA_IME_DATA_DIR");
    if (IsUsableAbsolutePath(from_env))
    {
        return from_env;
    }

    const std::wstring installed = QueryInstalledDataDir();
    if (IsUsableAbsolutePath(installed))
    {
        return installed;
    }

    return get_local_appdata_path_w() + L"\\" + kAppName;
}

std::wstring get_ime_config_dir_w()
{
    // Configuration normally lives in the data directory. The separate override exists for tests
    // that must redirect config writes without moving the dictionaries with them: the engine
    // resolves its own data directory (engine/core/data_path.h), so METASEQUOIA_IME_DATA_DIR
    // would take the dictionaries along. Nothing in the product sets this.
    const std::wstring from_env = QueryEnvironmentW(L"METASEQUOIA_IME_CONFIG_DIR");
    if (IsUsableAbsolutePath(from_env))
    {
        return from_env;
    }
    return get_ime_data_path_w();
}

void ensure_ime_data_writable()
{
    // 个人词库、统计、剪贴板历史和带 Token 的 config.toml 只属于当前用户。安装器以提升权限运行，
    // 给数据目录授过 BUILTIN\Users 的可写权限；这里换成「当前用户 + SYSTEM + Administrators」的
    // 保护型 DACL，并让子目录/文件继承，其他本地账户就不能再读到这些数据。
    static const wchar_t *const kPrivateFiles[] = {
        L"msime_user.db",    L"msime_user.db-wal",   L"msime_user.db-shm",      L"user_dict.dat",
        L"english.db",       L"english.db-wal",      L"english.db-shm",         L"stats.db",
        L"stats.db-wal",     L"stats.db-shm",        L"clipboard_history.json", L"config.toml",
        L"config.base.toml", L"config.default.toml", L"config.toml.tmp"};

    const std::wstring dir = get_ime_config_dir_w();
    if (IsUsableAbsolutePath(dir))
    {
        EnsureMediumIntegrityWritableDirectory(dir);
        for (const wchar_t *name : kPrivateFiles)
        {
            EnsureMediumIntegrityWritableExistingPath((std::filesystem::path(dir) / name).wstring());
        }
    }

    const std::wstring data_dir = get_ime_data_path_w();
    if (IsUsableAbsolutePath(data_dir) && data_dir != dir)
    {
        EnsureMediumIntegrityWritableDirectory(data_dir);
        for (const wchar_t *name : kPrivateFiles)
        {
            EnsureMediumIntegrityWritableExistingPath((std::filesystem::path(data_dir) / name).wstring());
        }
    }
}

void protect_user_data_file(const std::wstring &path)
{
    if (!IsUsableAbsolutePath(path))
    {
        return;
    }
    // 新建的文件先继承目录 ACL；这里再补一道显式保护，目录 ACL 被改写或文件在旧版本目录里创建时
    // 也不会留下 Users 可读的窗口。调用方在写完个人数据后调用，例如剪贴板历史。
    SetOwnerOnlyDacl(path);
}
} // namespace CommonUtils
