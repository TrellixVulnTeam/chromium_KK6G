// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/message_center/views/message_popup_collection.h"

#include "base/stl_util.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/message_center_constants.h"
#include "ui/message_center/views/message_popup_view.h"
#include "ui/message_center/views/popup_alignment_delegate.h"

namespace message_center {

namespace {

// Animation duration for FADE_IN and FADE_OUT.
constexpr base::TimeDelta kFadeInFadeOutDuration =
    base::TimeDelta::FromMilliseconds(200);

// Animation duration for MOVE_DOWN.
constexpr base::TimeDelta kMoveDownDuration =
    base::TimeDelta::FromMilliseconds(120);

}  // namespace

MessagePopupCollection::MessagePopupCollection(
    PopupAlignmentDelegate* alignment_delegate)
    : animation_(std::make_unique<gfx::LinearAnimation>(this)),
      alignment_delegate_(alignment_delegate) {
  MessageCenter::Get()->AddObserver(this);
  alignment_delegate_->set_collection(this);
}

MessagePopupCollection::~MessagePopupCollection() {
  for (const auto& item : popup_items_)
    item.popup->Close();
  MessageCenter::Get()->RemoveObserver(this);
}

void MessagePopupCollection::Update() {
  if (is_updating_)
    return;
  base::AutoReset<bool> reset(&is_updating_, true);

  RemoveClosedPopupItems();

  if (MessageCenter::Get()->IsMessageCenterVisible()) {
    CloseAllPopupsNow();
    return;
  }

  if (animation_->is_animating()) {
    UpdateByAnimation();
    return;
  }

  if (state_ != State::IDLE)
    TransitionFromAnimation();

  if (state_ == State::IDLE)
    TransitionToAnimation();

  UpdatePopupTimers();

  if (state_ != State::IDLE) {
    // If not in IDLE state, start animation.
    animation_->SetDuration(state_ == State::MOVE_DOWN
                                ? kMoveDownDuration
                                : kFadeInFadeOutDuration);
    animation_->Start();
    UpdateByAnimation();
  }

  DCHECK(state_ == State::IDLE || animation_->is_animating());
}

void MessagePopupCollection::ResetBounds() {
  if (is_updating_)
    return;
  {
    base::AutoReset<bool> reset(&is_updating_, true);

    RemoveClosedPopupItems();
    ResetHotMode();
    state_ = State::IDLE;
    animation_->End();

    CalculateBounds();

    // Remove popups that are no longer in work area.
    ClosePopupsOutsideWorkArea();

    // Reset bounds and opacity of popups.
    for (auto& item : popup_items_) {
      item.popup->SetPopupBounds(item.bounds);
      item.popup->SetOpacity(1.0);
    }
  }

  // Restart animation for FADE_OUT.
  Update();
}

void MessagePopupCollection::NotifyPopupResized() {
  resize_requested_ = true;
  Update();
}

void MessagePopupCollection::NotifyPopupClosed(MessagePopupView* popup) {
  for (auto& item : popup_items_) {
    if (item.popup == popup)
      item.popup = nullptr;
  }
}

void MessagePopupCollection::OnNotificationAdded(
    const std::string& notification_id) {
  Update();
}

void MessagePopupCollection::OnNotificationRemoved(
    const std::string& notification_id,
    bool by_user) {
  Update();
}

void MessagePopupCollection::OnNotificationUpdated(
    const std::string& notification_id) {
  if (is_updating_)
    return;

  // Find Notification object with |notification_id|.
  const auto& notifications = MessageCenter::Get()->GetPopupNotifications();
  auto it = notifications.begin();
  while (it != notifications.end()) {
    if ((*it)->id() == notification_id)
      break;
    ++it;
  }

  if (it == notifications.end()) {
    // If not found, probably |notification_id| is removed from popups by
    // timeout.
    Update();
    return;
  }

  {
    base::AutoReset<bool> reset(&is_updating_, true);

    RemoveClosedPopupItems();

    // Update contents of the notification.
    for (const auto& item : popup_items_) {
      if (item.id == notification_id) {
        item.popup->UpdateContents(**it);
        break;
      }
    }
  }

  Update();
}

void MessagePopupCollection::OnCenterVisibilityChanged(Visibility visibility) {
  Update();
}

void MessagePopupCollection::AnimationEnded(const gfx::Animation* animation) {
  Update();
}

void MessagePopupCollection::AnimationProgressed(
    const gfx::Animation* animation) {
  Update();
}

void MessagePopupCollection::AnimationCanceled(
    const gfx::Animation* animation) {
  Update();
}

MessagePopupView* MessagePopupCollection::CreatePopup(
    const Notification& notification) {
  return new MessagePopupView(notification, alignment_delegate_, this);
}

void MessagePopupCollection::RestartPopupTimers() {
  MessageCenter::Get()->RestartPopupTimers();
}

void MessagePopupCollection::PausePopupTimers() {
  MessageCenter::Get()->PausePopupTimers();
}

bool MessagePopupCollection::IsPrimaryDisplayForNotification() const {
  return alignment_delegate_->IsPrimaryDisplayForNotification();
}

void MessagePopupCollection::TransitionFromAnimation() {
  DCHECK_NE(state_, State::IDLE);
  DCHECK(!animation_->is_animating());

  // The animation of type |state_| is now finished.
  UpdateByAnimation();

  // If FADE_OUT animation is finished, remove the animated popup.
  if (state_ == State::FADE_OUT)
    CloseAnimatingPopups();

  if (state_ == State::FADE_IN || state_ == State::MOVE_DOWN ||
      (state_ == State::FADE_OUT && popup_items_.empty())) {
    // If the animation is finished, transition to IDLE.
    state_ = State::IDLE;
  } else if (state_ == State::FADE_OUT && !popup_items_.empty()) {
    // If FADE_OUT animation is finished and we still have remaining popups,
    // we have to MOVE_DOWN them.
    state_ = State::MOVE_DOWN;

    // If we're going to add a new popup after this MOVE_DOWN, do the collapse
    // animation at the same time. Otherwise it will take another MOVE_DOWN.
    if (HasAddedPopup())
      CollapseAllPopups();

    MoveDownPopups();
  }
}

void MessagePopupCollection::TransitionToAnimation() {
  DCHECK_EQ(state_, State::IDLE);
  DCHECK(!animation_->is_animating());

  if (HasRemovedPopup()) {
    MarkRemovedPopup();

    // Start hot mode to allow a user to continually close many notifications.
    StartHotMode();

    if (CloseTransparentPopups()) {
      // If the popup is already transparent, skip FADE_OUT.
      state_ = State::MOVE_DOWN;
      MoveDownPopups();
    } else {
      state_ = State::FADE_OUT;
    }
    return;
  }

  if (HasAddedPopup()) {
    if (CollapseAllPopups()) {
      // If we had existing popups that weren't collapsed, first show collapsing
      // animation.
      state_ = State::MOVE_DOWN;
      MoveDownPopups();
      return;
    } else if (AddPopup()) {
      // If a popup is actually added, show FADE_IN animation.
      state_ = State::FADE_IN;
      return;
    }
  }

  if (resize_requested_) {
    // Resize is requested e.g. a user manually expanded notification.
    resize_requested_ = false;
    state_ = State::MOVE_DOWN;
    MoveDownPopups();
    ClosePopupsOutsideWorkArea();
    return;
  }

  if (!IsAnyPopupHovered() && is_hot_) {
    // Reset hot mode and animate to the normal positions.
    state_ = State::MOVE_DOWN;
    ResetHotMode();
    MoveDownPopups();
  }
}

void MessagePopupCollection::UpdatePopupTimers() {
  if (state_ == State::IDLE) {
    if (IsAnyPopupHovered() || IsAnyPopupActive()) {
      // If any popup is hovered or activated, pause popup timer.
      PausePopupTimers();
    } else {
      // If in IDLE state, restart popup timer.
      RestartPopupTimers();
    }
  } else {
    // If not in IDLE state, pause popup timer.
    PausePopupTimers();
  }
}

void MessagePopupCollection::CalculateBounds() {
  int base = alignment_delegate_->GetBaseline();
  for (size_t i = 0; i < popup_items_.size(); ++i) {
    gfx::Size preferred_size(
        kNotificationWidth,
        popup_items_[i].popup->GetHeightForWidth(kNotificationWidth));

    // Align the top of i-th popup to |hot_top_|.
    if (is_hot_ && hot_index_ == i) {
      base = hot_top_;
      if (!alignment_delegate_->IsTopDown())
        base += preferred_size.height();
    }

    int origin_x =
        alignment_delegate_->GetToastOriginX(gfx::Rect(preferred_size));

    int origin_y = base;
    if (!alignment_delegate_->IsTopDown())
      origin_y -= preferred_size.height();

    popup_items_[i].start_bounds = popup_items_[i].bounds;
    popup_items_[i].bounds =
        gfx::Rect(gfx::Point(origin_x, origin_y), preferred_size);

    const int delta = preferred_size.height() + kMarginBetweenPopups;
    if (alignment_delegate_->IsTopDown())
      base += delta;
    else
      base -= delta;
  }
}

void MessagePopupCollection::UpdateByAnimation() {
  DCHECK_NE(state_, State::IDLE);

  for (auto& item : popup_items_) {
    if (!item.is_animating)
      continue;

    double value = gfx::Tween::CalculateValue(
        state_ == State::FADE_OUT ? gfx::Tween::EASE_IN : gfx::Tween::EASE_OUT,
        animation_->GetCurrentValue());

    if (state_ == State::FADE_IN)
      item.popup->SetOpacity(gfx::Tween::FloatValueBetween(value, 0.0f, 1.0f));
    else if (state_ == State::FADE_OUT)
      item.popup->SetOpacity(gfx::Tween::FloatValueBetween(value, 1.0f, 0.0f));

    if (state_ == State::FADE_IN || state_ == State::MOVE_DOWN) {
      item.popup->SetPopupBounds(
          gfx::Tween::RectValueBetween(value, item.start_bounds, item.bounds));
    }
  }
}

bool MessagePopupCollection::AddPopup() {
  std::set<std::string> existing_ids;
  for (const auto& item : popup_items_)
    existing_ids.insert(item.id);

  auto notifications = MessageCenter::Get()->GetPopupNotifications();
  Notification* new_notification = nullptr;
  // Reverse iterating because notifications are in reverse chronological order.
  for (auto it = notifications.rbegin(); it != notifications.rend(); ++it) {
    // Disables popup of custom notification on non-primary displays, since
    // currently custom notification supports only on one display at the same
    // time.
    // TODO(yoshiki): Support custom popup notification on multiple display
    // (https://crbug.com/715370).
    if (!IsPrimaryDisplayForNotification() &&
        (*it)->type() == NOTIFICATION_TYPE_CUSTOM) {
      continue;
    }

    if (!existing_ids.count((*it)->id())) {
      new_notification = *it;
      break;
    }
  }

  if (!new_notification)
    return false;

  // Reset animation flag of existing popups.
  for (auto& item : popup_items_)
    item.is_animating = false;

  {
    PopupItem item;
    item.id = new_notification->id();
    item.is_animating = true;
    item.popup = CreatePopup(*new_notification);

    if (IsNextEdgeOutsideWorkArea(item)) {
      item.popup->Close();
      return false;
    }

    item.popup->Show();
    popup_items_.push_back(item);
  }

  MessageCenter::Get()->DisplayedNotification(new_notification->id(),
                                              DISPLAY_SOURCE_POPUP);

  CalculateBounds();

  auto& item = popup_items_.back();
  item.start_bounds = item.bounds;
  item.start_bounds += gfx::Vector2d(
      (alignment_delegate_->IsFromLeft() ? -1 : 1) * item.bounds.width(), 0);
  return true;
}

void MessagePopupCollection::MarkRemovedPopup() {
  std::set<std::string> existing_ids;
  for (Notification* notification :
       MessageCenter::Get()->GetPopupNotifications()) {
    existing_ids.insert(notification->id());
  }

  for (auto& item : popup_items_)
    item.is_animating = !existing_ids.count(item.id);
}

void MessagePopupCollection::MoveDownPopups() {
  CalculateBounds();
  for (auto& item : popup_items_)
    item.is_animating = true;
}

int MessagePopupCollection::GetNextEdge(const PopupItem& item) const {
  const int delta =
      item.popup->GetHeightForWidth(kNotificationWidth) + kMarginBetweenPopups;

  int base = 0;
  if (popup_items_.empty()) {
    base = alignment_delegate_->GetBaseline();
  } else {
    base = alignment_delegate_->IsTopDown()
               ? popup_items_.back().bounds.bottom()
               : popup_items_.back().bounds.y();
  }

  return alignment_delegate_->IsTopDown() ? base + delta : base - delta;
}

bool MessagePopupCollection::IsNextEdgeOutsideWorkArea(
    const PopupItem& item) const {
  const int next_edge = GetNextEdge(item);
  const gfx::Rect work_area = alignment_delegate_->GetWorkArea();
  return alignment_delegate_->IsTopDown() ? next_edge > work_area.bottom()
                                          : next_edge < work_area.y();
}

void MessagePopupCollection::StartHotMode() {
  for (size_t i = 0; i < popup_items_.size(); ++i) {
    if (popup_items_[i].is_animating && popup_items_[i].popup->is_hovered()) {
      is_hot_ = true;
      hot_index_ = i;
      hot_top_ = popup_items_[i].bounds.y();
      break;
    }
  }
}

void MessagePopupCollection::ResetHotMode() {
  is_hot_ = false;
  hot_index_ = 0;
  hot_top_ = 0;
}

void MessagePopupCollection::CloseAnimatingPopups() {
  for (auto& item : popup_items_) {
    if (!item.is_animating)
      continue;
    item.popup->Close();
  }
  RemoveClosedPopupItems();
}

bool MessagePopupCollection::CloseTransparentPopups() {
  bool removed = false;
  for (auto& item : popup_items_) {
    if (item.popup->GetOpacity() > 0.0)
      continue;
    item.popup->Close();
    removed = true;
  }
  RemoveClosedPopupItems();
  return removed;
}

void MessagePopupCollection::ClosePopupsOutsideWorkArea() {
  const gfx::Rect work_area = alignment_delegate_->GetWorkArea();
  for (auto& item : popup_items_) {
    if (work_area.Contains(item.bounds))
      continue;
    item.popup->Close();
  }
  RemoveClosedPopupItems();
}

void MessagePopupCollection::RemoveClosedPopupItems() {
  base::EraseIf(popup_items_, [](const auto& item) { return !item.popup; });
}

void MessagePopupCollection::CloseAllPopupsNow() {
  for (auto& item : popup_items_)
    item.is_animating = true;
  CloseAnimatingPopups();

  ResetHotMode();
  state_ = State::IDLE;
  animation_->End();
}

bool MessagePopupCollection::CollapseAllPopups() {
  bool changed = false;
  for (auto& item : popup_items_) {
    int old_height = item.popup->GetHeightForWidth(kNotificationWidth);

    item.popup->AutoCollapse();

    int new_height = item.popup->GetHeightForWidth(kNotificationWidth);
    if (old_height != new_height)
      changed = true;
  }

  resize_requested_ = false;
  return changed;
}

bool MessagePopupCollection::HasAddedPopup() const {
  std::set<std::string> existing_ids;
  for (const auto& item : popup_items_)
    existing_ids.insert(item.id);

  for (Notification* notification :
       MessageCenter::Get()->GetPopupNotifications()) {
    if (!existing_ids.count(notification->id()))
      return true;
  }
  return false;
}

bool MessagePopupCollection::HasRemovedPopup() const {
  std::set<std::string> existing_ids;
  for (Notification* notification :
       MessageCenter::Get()->GetPopupNotifications()) {
    existing_ids.insert(notification->id());
  }

  for (const auto& item : popup_items_) {
    if (!existing_ids.count(item.id))
      return true;
  }
  return false;
}

bool MessagePopupCollection::IsAnyPopupHovered() const {
  for (const auto& item : popup_items_) {
    if (item.popup->is_hovered())
      return true;
  }
  return false;
}

bool MessagePopupCollection::IsAnyPopupActive() const {
  for (const auto& item : popup_items_) {
    if (item.popup->is_active())
      return true;
  }
  return false;
}

}  // namespace message_center
