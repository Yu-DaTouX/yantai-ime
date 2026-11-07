#pragma once

bool OpenSettingsApplication();
bool OpenSettingsAboutApplication();
bool CloseSettingsApplication();
// 面板已删掉，这三个只是让 update_check 在升级前关窗的调用点继续编译的兼容空实现。
bool CloseEmojiPanelApplication();
bool CloseKeyboardPanelApplication();
bool CloseHandwritingPanelApplication();
