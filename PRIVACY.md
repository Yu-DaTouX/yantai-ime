# Privacy

Metasequoia IME for Windows converts keystrokes, composition text and candidates locally, using the shared C++ engine and the SQLite dictionaries installed under `%LOCALAPPDATA%\metasequoiaime\`. Local conversion never contacts the network. A fresh install sends nothing off the device: every online feature below is off by default and has to be switched on explicitly. This document describes those optional features, exactly what each one sends, and how to turn each one off again.

Every claim below is checkable against this repository. Where a default matters, the file and key that set it are named.

## Network features

**Voice input is off by default.** `[voice_input] voice_input` ships as `false`, so the hotkeys do nothing and no audio is captured or uploaded until you switch it on in 设置 → 语音输入 and enter a real ASR token; the shipped token slots are placeholders and are rejected as empty. Once configured, recorded audio is uploaded to the configured endpoint — Doubao (`wss://openspeech.bytedance.com/...`), OpenAI, SiliconFlow or Groq. If text polishing is enabled, the returned transcript is additionally sent to the configured Chat Completions endpoint.

**Model packages are downloaded only when you ask for them.** The optional Trigram (`sc.lm`), neural sentence and collocation (`.gram`) models are not contacted or refreshed in the background. 设置 → 输入 downloads a package only when you press 下载 (or imports a file you picked), and an already installed collocation model is checked against its upstream only when you press 检查更新 on that row. Those requests are plain HTTPS GET/HEAD requests that carry no typed text; see `server/src/settings/model_download.cpp`, `server/src/settings/lattice_model.cpp`, `server/src/settings/neural_model.cpp` and `server/src/settings/collocation_model.cpp`.

**The update check runs only when you press the button.** 设置 → 关于 → 检查更新 fetches `https://msime.app/update.json` and compares the version. `appearance.auto_download_update` ships `false`, and while it is off the background poller makes no request at all. With it on, the request carries no identifier beyond the IP address and a cache-busting timestamp.

## No telemetry

There is no analytics, telemetry, crash reporting, or usage measurement that leaves this machine. No third-party SDK for those purposes is linked, and there is no endpoint that receives usage data. This is verifiable: searching the `server/`, `windows/`, `ui/` and `installer/` trees for `telemetry`, `analytics`, `sentry`, `matomo`, `posthog`, `amplitude`, `mixpanel`, `crashpad` and `breakpad` returns nothing.

## Local data

Settings live in `%LOCALAPPDATA%\metasequoiaime\config.toml`. Learned word frequencies and user-created words are stored in `%LOCALAPPDATA%\metasequoiaime\msime_user.db`. Diagnostic logs are written to `%LOCALAPPDATA%\metasequoiaime\log\`, and both diagnostic switches (`general.diagnostic_log` and `general.tsf_diagnostic_log`) ship `false`, so a fresh install keeps no diagnostic trace. When a log is switched on for troubleshooting it records process lifecycle, latency and error conditions only: timings, counts, byte/length figures, candidate counts, window and caret coordinates, virtual-key codes and coarse key classes. Typed text, pinyin, candidate words, committed text, clipboard contents, window titles and the translated character behind a key press are never written; the key trace deliberately logs the virtual-key code and a class such as `printable` or `navigation` instead of the character, and text-bearing fields appear only as their length. The standalone voice-input prototype logs the byte length of a transcript, never the transcript itself.

**Clipboard history is off by default and stores only text.** `utility.clipboard_history` ships `false`, and the value is read as `false` when the key is missing, so a fresh or legacy config records nothing. When you switch it on in 设置 → 工具, copied text is written to `<DataDir>\clipboard_history.json` — at most 50 items, each cut to 4000 UTF-16 code units, in plain text (no encryption, but the file is restricted to your Windows account; see below). Nothing is logged or sent anywhere. Switching the switch off deletes that file immediately, and every Server start while the switch is off deletes any file left behind by an earlier run. Uninstalling removes it with the rest of the data directory.

**The data directory is private to your Windows account.** Server startup replaces the data directory's ACL with a protected DACL naming the current user, `SYSTEM` and `Administrators` only (in place of the `BUILTIN\Users` grant the elevated installer used to apply), and the ACEs inherit, so `msime_user.db` and its WAL/SHM files, `stats.db`, `clipboard_history.json`, `config.toml`/`config.base.toml` and files created later are not readable by other local accounts. The same pass also marks the directory medium-integrity / no-write-up so lower-integrity processes cannot modify dictionaries and data. This applies to a custom data directory as well as the default one. If the chosen volume has no ACL support (for example a FAT32 stick), Windows cannot enforce this and the files are only as protected as the volume itself.

**API tokens are encrypted with Windows DPAPI.** Values such as `token_*`, `asr_token*`, `polish_token*`, `api_key`, `apikey`, `secret_id` and `secret_key` are stored in `config.toml` as `dpapi:`-prefixed ciphertext bound to the current Windows account; placeholders stay plaintext so the template merge can still recognise an unfilled slot, and an encrypted value copied to another account or machine is treated as empty rather than sent. The file itself is covered by the owner-only ACL above.

## Uninstall

Uninstalling deletes `%LOCALAPPDATA%\metasequoiaime` and `%PROGRAMDATA%\metasequoiaime` in full, including the user dictionary, settings and any installed skins. Back up `msime_user.db` first if you want to keep learned words. An in-place upgrade behaves differently: it preserves `msime_user.db`, `config.toml`, `config.base.toml`, `custom_translations.txt`, and the `skins`, `models` and `helpcodes\custom` directories, and replaces everything else.

## General

Metasequoia IME does not sell or share personal data, and installing or using it does not create an account. If a future release adds another network-backed feature, this notice must be updated before that feature ships.

Security or privacy concerns should be reported privately as described in the organisation [SECURITY.md](https://github.com/metasequoiaime/.github/blob/main/SECURITY.md).
