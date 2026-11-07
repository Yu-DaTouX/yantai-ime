# Privacy

Metasequoia IME for Windows converts keystrokes, composition text and candidates locally, using the shared C++ engine and the SQLite dictionaries installed under `%LOCALAPPDATA%\metasequoiaime\`. Local conversion never contacts the network. This document describes the optional online features that do, exactly what each one sends, and how to turn each one off.

Every claim below is checkable against this repository. Where a default matters, the file and key that set it are named.

## Network features

**Cloud candidates are enabled by default, and the installer asks before the first install.** `general.cloud_candidates` is `true` in the shipped `installer/default_config/config.default.toml`, and `server/src/config/ime_config.cpp` also defaults to `true` when the key is absent. A first install shows a wizard page naming this feature and its endpoint, with the box checked; clearing it writes `cloud_candidates = false` into the config that install creates. Upgrades skip that page, because by then the config belongs to the user. While a composition is active, the typed spelling is sent over HTTPS to Google's input-tools service (`https://inputtools.google.com/request`) to fetch one extra candidate. Google receives that spelling, the IP address and standard request metadata. No committed text, dictionary content, learned frequencies, settings or account data are sent. This is the one online feature that needs no credential, so it is the one that is actually live on a fresh install. Turn it off in 设置 → 输入 → 云候选, or set `cloud_candidates = false` under `[general]` in `%LOCALAPPDATA%\metasequoiaime\config.toml`.

**AI candidate suggestions require a token you supply.** `[ai_assistant] enabled` ships as `false`. When enabled and configured with a real token, the segmented spelling and the surrounding composition context are sent to the provider's Chat Completions endpoint — DeepSeek, OpenAI, SiliconFlow or Groq, per `[ai_assistant] provider`. That provider's own privacy terms then apply. Set `enabled = false` to disable it outright.

**Jev is an optional candidate-selection provider.** Selecting `jev` under `[ai_assistant]` uses TypeSafe's Choice endpoint (`https://api.typesafe.ai/v1/systemone`) instead of Chat Completions. Each request sends the segmented pinyin, recent committed context from the current focus session (up to 1024 UTF-8 bytes before credential-like text filtering), and up to 32 matching local candidates, including matching user-dictionary entries. The service selects one existing candidate or abstains; it does not generate new candidate text. Requests are disabled in fields reported as sensitive by the host. Ordinary composition context and candidate text are still sent to TypeSafe, whose privacy terms apply. A custom endpoint receives the same data instead of TypeSafe, and the token may be left empty for endpoints that do not require one; the official endpoint is never contacted without a token. Turn off AI candidate suggestions to stop these requests.

**Candidate translation requires credentials you supply.** `[translation] provider` ships empty and every provider ships with placeholder or empty credentials, so nothing is sent until you enter real keys in 设置 → 输入 — the same placeholder check that guards the other providers rejects `<YOUR_...>` values. (When `provider` is left empty, the provider is derived from the legacy per-provider `enabled` flags, so a configuration that only fills in one provider's keys also sends requests.) Once configured, candidate text that the local dictionaries could not gloss goes to the endpoint of the provider you chose: Tencent Cloud (`https://tmt.tencentcloudapi.com`), Alibaba Cloud Machine Translation (`https://mt.aliyuncs.com`), NiuTrans (`https://api.niutrans.com`), your own DeepLX-compatible endpoint, or the Chat Completions endpoint of your AI assistant provider. Translations are display metadata and never change the text that is committed.

**Voice input requires a token you supply.** `[voice_input] voice_input` ships as `true` with a placeholder ASR token, so the hotkeys do nothing over the network until a real token is entered. Once configured, recorded audio is uploaded to the configured endpoint — Doubao (`wss://openspeech.bytedance.com/...`), OpenAI, SiliconFlow or Groq. If text polishing is enabled, the returned transcript is additionally sent to the configured Chat Completions endpoint.

**The update check runs only when you press the button.** 设置 → 关于 → 检查更新 fetches `https://msime.app/update.json` and compares the version. There is no background or scheduled update check, and the request carries no identifier beyond the IP address and a cache-busting timestamp.

## No telemetry

There is no analytics, telemetry, crash reporting, or usage measurement that leaves this machine. No third-party SDK for those purposes is linked, and there is no endpoint that receives usage data. This is verifiable: searching the `server/`, `windows/`, `ui/` and `installer/` trees for `telemetry`, `analytics`, `sentry`, `matomo`, `posthog`, `amplitude`, `mixpanel`, `crashpad` and `breakpad` returns nothing.

## Local data

Settings live in `%LOCALAPPDATA%\metasequoiaime\config.toml`. Learned word frequencies and user-created words are stored in `%LOCALAPPDATA%\metasequoiaime\msime_user.db`. Diagnostic logs are written to `%LOCALAPPDATA%\metasequoiaime\log\` and record process lifecycle and error conditions, not typed content. Clipboard history is disabled by default (`clipboard_history = false`); when enabled it records copied text locally and is cleared as soon as it is switched off.

**API tokens are stored in plain text in `config.toml`.** This differs from the other platforms — macOS and iOS use the Keychain, and Linux uses the desktop Secret Service — and it means anything able to read that file can read the tokens. Use provider tokens scoped to this purpose, and treat the file as a secret when backing up or syncing the profile directory.

All of these files sit in the current user's profile and are protected only by that account's permissions.

## Uninstall

Uninstalling deletes `%LOCALAPPDATA%\metasequoiaime` and `%PROGRAMDATA%\metasequoiaime` in full, including the user dictionary, settings and any installed skins. Back up `msime_user.db` first if you want to keep learned words. An in-place upgrade behaves differently: it preserves `msime_user.db`, `config.toml`, `config.base.toml`, `custom_translations.txt`, and the `skins`, `shuangpin`, `models` and `helpcodes\custom` directories, and replaces everything else.

## General

Metasequoia IME does not sell or share personal data, and installing or using it does not create an account. If a future release adds another network-backed feature, this notice must be updated before that feature ships.

Security or privacy concerns should be reported privately as described in the organisation [SECURITY.md](https://github.com/metasequoiaime/.github/blob/main/SECURITY.md).
