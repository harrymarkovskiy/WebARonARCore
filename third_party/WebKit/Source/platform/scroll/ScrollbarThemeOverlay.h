/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ScrollbarThemeOverlay_h
#define ScrollbarThemeOverlay_h

#include "platform/graphics/Color.h"
#include "platform/scroll/ScrollbarTheme.h"

namespace blink {

// This scrollbar theme is used to get overlay scrollbar for platforms other
// than Mac. Mac's overlay scrollbars are in ScrollbarThemeMac*.
class PLATFORM_EXPORT ScrollbarThemeOverlay : public ScrollbarTheme {
 public:
  enum HitTestBehavior { AllowHitTest, DisallowHitTest };

  ScrollbarThemeOverlay(int thumbThickness,
                        int scrollbarMargin,
                        HitTestBehavior);
  ScrollbarThemeOverlay(int thumbThickness,
                        int scrollbarMargin,
                        HitTestBehavior,
                        Color);
  ~ScrollbarThemeOverlay() override {}

  bool shouldRepaintAllPartsOnInvalidation() const override;

  ScrollbarPart invalidateOnThumbPositionChange(
      const ScrollbarThemeClient&,
      float oldPosition,
      float newPosition) const override;

  ScrollbarPart invalidateOnEnabledChange() const override;

  int scrollbarThickness(ScrollbarControlSize) override;
  int scrollbarMargin() const override;
  bool usesOverlayScrollbars() const override;
  double overlayScrollbarFadeOutDelaySeconds() const override;
  double overlayScrollbarFadeOutDurationSeconds() const override;

  int thumbPosition(const ScrollbarThemeClient&, float scrollPosition) override;
  int thumbLength(const ScrollbarThemeClient&) override;

  bool hasButtons(const ScrollbarThemeClient&) override { return false; }
  bool hasThumb(const ScrollbarThemeClient&) override;

  IntRect backButtonRect(const ScrollbarThemeClient&,
                         ScrollbarPart,
                         bool painting = false) override;
  IntRect forwardButtonRect(const ScrollbarThemeClient&,
                            ScrollbarPart,
                            bool painting = false) override;
  IntRect trackRect(const ScrollbarThemeClient&,
                    bool painting = false) override;
  int thumbThickness(const ScrollbarThemeClient&) override;
  int thumbThickness() { return m_thumbThickness; }

  void paintThumb(GraphicsContext&, const Scrollbar&, const IntRect&) override;
  ScrollbarPart hitTest(const ScrollbarThemeClient&, const IntPoint&) override;

  static ScrollbarThemeOverlay& mobileTheme();

 private:
  int m_thumbThickness;
  int m_scrollbarMargin;
  HitTestBehavior m_allowHitTest;
  Color m_color;
  const bool m_useSolidColor;
};

}  // namespace blink

#endif
