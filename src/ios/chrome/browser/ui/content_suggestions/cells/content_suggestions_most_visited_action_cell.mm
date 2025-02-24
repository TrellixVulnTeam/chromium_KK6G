// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/content_suggestions/cells/content_suggestions_most_visited_action_cell.h"

#import "ios/chrome/browser/ui/content_suggestions/cells/content_suggestions_most_visited_constants.h"
#import "ios/chrome/browser/ui/ntp_tile_views/ntp_shortcut_tile_view.h"
#include "ios/chrome/browser/ui/ui_util.h"
#import "ios/chrome/browser/ui/uikit_ui_util.h"
#import "ios/chrome/common/favicon/favicon_view.h"
#import "ios/chrome/common/material_timing.h"
#import "ios/chrome/common/ui_util/constraints_ui_util.h"
#import "ios/third_party/material_components_ios/src/components/Typography/src/MaterialTypography.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface ContentSuggestionsMostVisitedActionCell ()

@property(nonatomic, strong) NTPShortcutTileView* tileView;

@end

@implementation ContentSuggestionsMostVisitedActionCell : MDCCollectionViewCell

#pragma mark - Public

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    _tileView = [[NTPShortcutTileView alloc] initWithFrame:frame];
    _tileView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.contentView addSubview:_tileView];
    AddSameConstraints(self.contentView, _tileView);

    _tileView.countLabel.font = [MDCTypography captionFont];

    self.isAccessibilityElement = YES;
  }
  return self;
}

- (void)setHighlighted:(BOOL)highlighted {
  [super setHighlighted:highlighted];

  [UIView transitionWithView:self
                    duration:ios::material::kDuration8
                     options:UIViewAnimationOptionCurveEaseInOut
                  animations:^{
                    self.alpha = highlighted ? 0.5 : 1.0;
                  }
                  completion:nil];
}

+ (CGSize)defaultSize {
  return kCellSize;
}

- (CGSize)intrinsicContentSize {
  return [[self class] defaultSize];
}

- (void)prepareForReuse {
  [super prepareForReuse];
  self.tileView.countContainer.hidden = YES;
}

#pragma mark - properties

- (UIImageView*)iconView {
  return self.tileView.iconView;
}

- (UILabel*)titleLabel {
  return self.tileView.titleLabel;
}

- (UIView*)countContainer {
  return self.tileView.countContainer;
}

- (UILabel*)countLabel {
  return self.tileView.countLabel;
}

@end
