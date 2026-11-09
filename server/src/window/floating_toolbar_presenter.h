#pragma once

#include <memory>
#include <windows.h>

class FloatingToolbarPresenter
{
  public:
    static FloatingToolbarPresenter &Instance();

    bool Bind(HWND hwnd);
    bool IsBound() const;
    void Present();
    // Whole-toolbar opacity (0..1) for the auto-hide fade; redraws on change.
    void SetOpacity(float opacity);
    void ApplyTheme();
    void ApplyAppearance();
    // 选中翻译的按钮标签变了（译 / 译中 / 还原）：按当前状态重建按钮。
    void RefreshTranslateButton();
    // suggestedRect: WM_DPICHANGED's recommended placement (lParam).
    void RelayoutHost(FLOAT scaleOverride = 0.0f, const RECT *suggestedRect = nullptr);
    void SyncUi(int cnEn, int doubleSingleByte, int punctuation, int englishInputMode, int capsLock);
    bool HitCaptionDrag(POINT clientPoint) const;
    // Physical px per side of transparent margin (shadow, or the stroke slack
    // when the shadow is off) that may hang off a
    // screen edge (KeepRectOnVisibleScreens' offscreenInsets), for a host of
    // the given physical size. Everything inside it is the bar and its border.
    RECT ShadowInsetsPx(int hostWidthPx, int hostHeightPx, FLOAT scale) const;
    // A skin's image-mode toolbar is shaped like its background image: the
    // region (window coordinates) of the image's non-transparent pixels and the
    // button boxes, still owned by the presenter. Null for the bar, and when the
    // image cannot be read.
    HRGN ShapeRegion() const;
    bool HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

  private:
    FloatingToolbarPresenter();
    ~FloatingToolbarPresenter();
    FloatingToolbarPresenter(const FloatingToolbarPresenter &) = delete;
    FloatingToolbarPresenter &operator=(const FloatingToolbarPresenter &) = delete;

    void RebuildScene();
    // Rebuilds ShapeRegion for a background image drawn at this many physical px,
    // plus the button boxes at `scale` physical px per DIP.
    void UpdateShape(int imageWidthPx, int imageHeightPx, float scale);
    // Animated skin images: one timer per frame while the toolbar is on screen.
    void ScheduleAnimationFrame();
    void StopAnimationFrames();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    HWND hwnd_ = nullptr;
    bool bound_ = false;
};
