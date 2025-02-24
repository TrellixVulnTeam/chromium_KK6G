// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_MESSAGE_CENTER_NEW_UNIFIED_MESSAGE_CENTER_VIEW_H_
#define ASH_SYSTEM_MESSAGE_CENTER_NEW_UNIFIED_MESSAGE_CENTER_VIEW_H_

#include "ash/ash_export.h"
#include "ash/system/message_center/message_center_scroll_bar.h"
#include "ash/system/message_center/unified_message_list_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"

namespace views {
class ScrollView;
}  // namespace views

namespace ash {

class MessageCenterScrollBar;
class StackingNotificationCounterView;
class UnifiedSystemTrayView;

// Manages scrolling of notification list.
// TODO(tetsui): Rename to UnifiedMessageCenterView after old code is removed.
class ASH_EXPORT NewUnifiedMessageCenterView
    : public views::View,
      public MessageCenterScrollBar::Observer,
      public views::ButtonListener,
      public views::FocusChangeListener {
 public:
  explicit NewUnifiedMessageCenterView(UnifiedSystemTrayView* parent);
  ~NewUnifiedMessageCenterView() override;

  // Sets the maximum height that the view can take.
  void SetMaxHeight(int max_height);

  // Called from UnifiedMessageListView when the preferred size is changed.
  void ListPreferredSizeChanged();

  // Configures MessageView to forward scroll events. Called from
  // UnifiedMessageListView.
  void ConfigureMessageView(message_center::MessageView* message_view);

  // views::View:
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void Layout() override;
  gfx::Size CalculatePreferredSize() const override;

  // MessageCenterScrollBar::Observer:
  void OnMessageCenterScrolled() override;

  // views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  // views::FocusChangeListener:
  void OnWillChangeFocus(views::View* before, views::View* now) override;
  void OnDidChangeFocus(views::View* before, views::View* now) override;

 protected:
  // Virtual for testing.
  virtual void SetNotificationHeightBelowScroll(int height_below_scroll);

 private:
  friend class NewUnifiedMessageCenterViewTest;

  void UpdateVisibility();

  // Scroll the notification list to |position_from_bottom_|.
  void ScrollToPositionFromBottom();

  // Notifies height below scroll to |parent_| so that it can update
  // TopCornerBorder.
  void NotifyHeightBelowScroll();

  // Count number of notifications that are above visible area.
  int GetStackedNotificationCount() const;

  UnifiedSystemTrayView* const parent_;
  StackingNotificationCounterView* const stacking_counter_;
  MessageCenterScrollBar* const scroll_bar_;
  views::ScrollView* const scroller_;
  UnifiedMessageListView* const message_list_view_;

  // Position from the bottom of scroll contents in dip.
  int position_from_bottom_;

  views::FocusManager* focus_manager_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(NewUnifiedMessageCenterView);
};

}  // namespace ash

#endif  // ASH_SYSTEM_MESSAGE_CENTER_NEW_UNIFIED_MESSAGE_CENTER_VIEW_H_
