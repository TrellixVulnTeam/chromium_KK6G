// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/snapshots/snapshot_tab_helper.h"

#include "base/macros.h"
#include "base/run_loop.h"
#include "ios/chrome/browser/browser_state/test_chrome_browser_state.h"
#import "ios/chrome/browser/snapshots/fake_snapshot_generator_delegate.h"
#import "ios/chrome/browser/snapshots/snapshot_cache.h"
#import "ios/chrome/browser/snapshots/snapshot_cache_factory.h"
#import "ios/chrome/browser/ui/image_util/image_util.h"
#import "ios/chrome/browser/ui/uikit_ui_util.h"
#import "ios/web/public/test/fakes/test_web_state.h"
#include "ios/web/public/test/test_web_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest_mac.h"
#include "testing/platform_test.h"
#include "ui/base/test/ios/ui_image_test_utils.h"
#include "ui/gfx/image/image.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using ui::test::uiimage_utils::UIImagesAreEqual;
using ui::test::uiimage_utils::UIImageWithSizeAndSolidColor;

// SnapshotGeneratorDelegate used to test SnapshotTabHelper by allowing to
// count the number of snapshot generated and control whether capturing a
// snapshot is possible.
@interface TabHelperSnapshotGeneratorDelegate : FakeSnapshotGeneratorDelegate

// Returns the number of times a snapshot was captured (count the number of
// calls to -willUpdateSnapshotForWebState:).
@property(nonatomic, readonly) NSUInteger snapshotTakenCount;

// This property controls the value returned by -canTakeSnapshotForWebState:
// method of the SnapshotGeneratorDelegate protocol.
@property(nonatomic, assign) BOOL canTakeSnapshot;

@end

@implementation TabHelperSnapshotGeneratorDelegate

@synthesize snapshotTakenCount = _snapshotTakenCount;
@synthesize canTakeSnapshot = _canTakeSnapshot;

#pragma mark - SnapshotGeneratorDelegate

- (BOOL)canTakeSnapshotForWebState:(web::WebState*)webState {
  return !_canTakeSnapshot;
}

- (void)willUpdateSnapshotForWebState:(web::WebState*)webState {
  ++_snapshotTakenCount;
}

@end

namespace {

// Returns whether the |image| dominant color is |color|.
bool IsDominantColorForImage(UIImage* image, UIColor* color) {
  UIColor* dominant_color =
      DominantColorForImage(gfx::Image(image), /*opacity=*/1.0);
  return [color isEqual:dominant_color];
}

// Dimension of the WebState's view (if defined).
constexpr CGSize kWebStateViewSize = {300, 400};

// Dimension of the cached snapshot images.
constexpr CGSize kCachedSnapshotSize = {15, 20};

// Dimension of the default snapshot image.
constexpr CGSize kDefaultSnapshotSize = {150, 200};

// Number of snapshot to take to test snapshot coalescing.
constexpr NSUInteger kCountSnapshotToTake = 5;

}  // namespace

class SnapshotTabHelperTest : public PlatformTest {
 public:
  SnapshotTabHelperTest() {
    // Create a SnapshotCache instance as the SnapshotGenerator first lookup
    // in the cache before generating the snapshot (thus the test would fail
    // if no cache existed).
    TestChromeBrowserState::Builder builder;
    builder.AddTestingFactory(SnapshotCacheFactory::GetInstance(),
                              SnapshotCacheFactory::GetDefaultFactory());
    browser_state_ = builder.Build();
    web_state_.SetBrowserState(browser_state_.get());

    // Create the SnapshotTabHelper with a fake delegate.
    snapshot_session_id_ = [[NSUUID UUID] UUIDString];
    delegate_ = [[TabHelperSnapshotGeneratorDelegate alloc] init];
    SnapshotTabHelper::CreateForWebState(&web_state_, snapshot_session_id_);
    SnapshotTabHelper::FromWebState(&web_state_)->SetDelegate(delegate_);
  }

  // Add a fake view to the TestWebState. This will be used to capture the
  // snapshot. By default the WebState is not ready for taking snapshot.
  void AddDefaultWebStateView() {
    CGRect frame = {CGPointZero, kWebStateViewSize};
    UIView* view = [[UIView alloc] initWithFrame:frame];
    view.backgroundColor = [UIColor redColor];
    web_state_superview_ = [[UIView alloc] initWithFrame:frame];
    [web_state_superview_ addSubview:view];
    web_state_.SetView(view);
  }

  void SetCachedSnapshot(UIImage* image) {
    [GetSnapshotCache() setImage:image withSessionID:snapshot_session_id_];
  }

  UIImage* GetCachedSnapshot() {
    base::RunLoop run_loop;
    base::RunLoop* run_loop_ptr = &run_loop;

    __block UIImage* snapshot = nil;
    [GetSnapshotCache() retrieveImageForSessionID:snapshot_session_id_
                                         callback:^(UIImage* cached_snapshot) {
                                           snapshot = cached_snapshot;
                                           run_loop_ptr->Quit();
                                         }];

    run_loop.Run();
    return snapshot;
  }

  SnapshotCache* GetSnapshotCache() {
    return SnapshotCacheFactory::GetForBrowserState(browser_state_.get());
  }

 protected:
  web::TestWebThreadBundle thread_bundle_;
  std::unique_ptr<ios::ChromeBrowserState> browser_state_;
  TabHelperSnapshotGeneratorDelegate* delegate_ = nil;
  NSString* snapshot_session_id_ = nil;
  web::TestWebState web_state_;
  // The webState's view needs a superview so a snapshot can be taken.
  UIView* web_state_superview_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SnapshotTabHelperTest);
};

// Tests that RetrieveColorSnapshot uses the image from the cache if
// there is one present.
TEST_F(SnapshotTabHelperTest, RetrieveColorSnapshotCachedSnapshot) {
  SetCachedSnapshot(
      UIImageWithSizeAndSolidColor(kCachedSnapshotSize, [UIColor greenColor]));

  base::RunLoop run_loop;
  base::RunLoop* run_loop_ptr = &run_loop;

  __block UIImage* snapshot = nil;
  SnapshotTabHelper::FromWebState(&web_state_)
      ->RetrieveColorSnapshot(^(UIImage* image) {
        snapshot = image;
        run_loop_ptr->Quit();
      });

  run_loop.Run();

  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(UIImagesAreEqual(snapshot, GetCachedSnapshot()));
  EXPECT_EQ(delegate_.snapshotTakenCount, 0u);
}

// Tests that RetrieveColorSnapshot returns the default snapshot image when
// there is no cached snapshot and the WebState web usage is disabled.
TEST_F(SnapshotTabHelperTest, RetrieveColorSnapshotWebUsageDisabled) {
  web_state_.SetWebUsageEnabled(false);
  AddDefaultWebStateView();

  base::RunLoop run_loop;
  base::RunLoop* run_loop_ptr = &run_loop;

  __block UIImage* snapshot = nil;
  SnapshotTabHelper::FromWebState(&web_state_)
      ->RetrieveColorSnapshot(^(UIImage* image) {
        snapshot = image;
        run_loop_ptr->Quit();
      });

  run_loop.Run();

  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(
      UIImagesAreEqual(snapshot, SnapshotTabHelper::GetDefaultSnapshotImage()));
  EXPECT_EQ(delegate_.snapshotTakenCount, 0u);
}

// Tests that RetrieveColorSnapshot returns the default snapshot image when
// there is no cached snapshot and the delegate says it is not possible to
// take a snapshot.
TEST_F(SnapshotTabHelperTest, RetrieveColorSnapshotCannotTakeSnapshot) {
  delegate_.canTakeSnapshot = YES;
  AddDefaultWebStateView();

  base::RunLoop run_loop;
  base::RunLoop* run_loop_ptr = &run_loop;

  __block UIImage* snapshot = nil;
  SnapshotTabHelper::FromWebState(&web_state_)
      ->RetrieveColorSnapshot(^(UIImage* image) {
        snapshot = image;
        run_loop_ptr->Quit();
      });

  run_loop.Run();

  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(
      UIImagesAreEqual(snapshot, SnapshotTabHelper::GetDefaultSnapshotImage()));
  EXPECT_EQ(delegate_.snapshotTakenCount, 0u);
}

// Tests that RetrieveGreySnapshot uses the image from the cache if
// there is one present, and that it is greyscale.
TEST_F(SnapshotTabHelperTest, RetrieveGreySnapshotCachedSnapshot) {
  SetCachedSnapshot(
      UIImageWithSizeAndSolidColor(kCachedSnapshotSize, [UIColor greenColor]));

  base::RunLoop run_loop;
  base::RunLoop* run_loop_ptr = &run_loop;

  __block UIImage* snapshot = nil;
  SnapshotTabHelper::FromWebState(&web_state_)
      ->RetrieveGreySnapshot(^(UIImage* image) {
        snapshot = image;
        run_loop_ptr->Quit();
      });

  run_loop.Run();

  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(UIImagesAreEqual(snapshot, GreyImage(GetCachedSnapshot())));
  EXPECT_EQ(delegate_.snapshotTakenCount, 0u);
}

// Tests that RetrieveGreySnapshot returns the default snapshot image when
// there is no cached snapshot and the WebState web usage is disabled.
TEST_F(SnapshotTabHelperTest, RetrieveGreySnapshotWebUsageDisabled) {
  web_state_.SetWebUsageEnabled(false);
  AddDefaultWebStateView();

  base::RunLoop run_loop;
  base::RunLoop* run_loop_ptr = &run_loop;

  __block UIImage* snapshot = nil;
  SnapshotTabHelper::FromWebState(&web_state_)
      ->RetrieveGreySnapshot(^(UIImage* image) {
        snapshot = image;
        run_loop_ptr->Quit();
      });

  run_loop.Run();

  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(UIImagesAreEqual(
      snapshot, GreyImage(SnapshotTabHelper::GetDefaultSnapshotImage())));
  EXPECT_EQ(delegate_.snapshotTakenCount, 0u);
}

// Tests that RetrieveGreySnapshot returns the default snapshot image when
// there is no cached snapshot and the WebState web usage is disabled.
TEST_F(SnapshotTabHelperTest, RetrieveGreySnapshotCannotTakeSnapshot) {
  delegate_.canTakeSnapshot = YES;
  AddDefaultWebStateView();

  base::RunLoop run_loop;
  base::RunLoop* run_loop_ptr = &run_loop;

  __block UIImage* snapshot = nil;
  SnapshotTabHelper::FromWebState(&web_state_)
      ->RetrieveGreySnapshot(^(UIImage* image) {
        snapshot = image;
        run_loop_ptr->Quit();
      });

  run_loop.Run();

  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(UIImagesAreEqual(
      snapshot, GreyImage(SnapshotTabHelper::GetDefaultSnapshotImage())));
  EXPECT_EQ(delegate_.snapshotTakenCount, 0u);
}

// Tests that RetrieveGreySnapshot generates the image if there is no
// image in the cache, and that it is greyscale.
TEST_F(SnapshotTabHelperTest, RetrieveGreySnapshotGenerate) {
  AddDefaultWebStateView();

  base::RunLoop run_loop;
  base::RunLoop* run_loop_ptr = &run_loop;

  __block UIImage* snapshot = nil;
  SnapshotTabHelper::FromWebState(&web_state_)
      ->RetrieveGreySnapshot(^(UIImage* image) {
        snapshot = image;
        run_loop_ptr->Quit();
      });

  run_loop.Run();

  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(CGSizeEqualToSize(snapshot.size, kWebStateViewSize));
  EXPECT_FALSE(IsDominantColorForImage(snapshot, [UIColor redColor]));
  EXPECT_EQ(delegate_.snapshotTakenCount, 1u);
}

// Tests that UpdateSnapshot ignores any cached snapshots, generate a new one
// and updates the cache.
TEST_F(SnapshotTabHelperTest, UpdateSnapshot) {
  AddDefaultWebStateView();

  SetCachedSnapshot(
      UIImageWithSizeAndSolidColor(kDefaultSnapshotSize, [UIColor greenColor]));

  UIImage* snapshot = SnapshotTabHelper::FromWebState(&web_state_)
                          ->UpdateSnapshot(/*with_overlays=*/true,
                                           /*visibible_frame_only=*/true);

  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(CGSizeEqualToSize(snapshot.size, kWebStateViewSize));
  EXPECT_TRUE(IsDominantColorForImage(snapshot, [UIColor redColor]));

  UIImage* cached_snapshot = GetCachedSnapshot();
  EXPECT_TRUE(UIImagesAreEqual(snapshot, cached_snapshot));
  EXPECT_EQ(delegate_.snapshotTakenCount, 1u);
}

// Tests that if snapshot coalescing is disabled, each call to UpdateSnapshot
// will cause a new snapshot to be generated.
TEST_F(SnapshotTabHelperTest, UpdateSnapshotNoCoalescing) {
  AddDefaultWebStateView();

  for (NSUInteger ii = 0; ii < kCountSnapshotToTake; ++ii) {
    UIImage* snapshot = SnapshotTabHelper::FromWebState(&web_state_)
                            ->UpdateSnapshot(/*with_overlays=*/true,
                                             /*visibible_frame_only=*/true);

    ASSERT_TRUE(snapshot);
    EXPECT_TRUE(CGSizeEqualToSize(snapshot.size, kWebStateViewSize));
    EXPECT_TRUE(IsDominantColorForImage(snapshot, [UIColor redColor]));
  }

  EXPECT_EQ(delegate_.snapshotTakenCount, kCountSnapshotToTake);
}

// Tests that if snapshot coalescing is enabled, only the first call to
// UpdateSnapshot will cause a new snapshot to be generated.
TEST_F(SnapshotTabHelperTest, UpdateSnapshotWithCoalescing) {
  AddDefaultWebStateView();

  SnapshotTabHelper::FromWebState(&web_state_)
      ->SetSnapshotCoalescingEnabled(true);
  for (NSUInteger ii = 0; ii < kCountSnapshotToTake; ++ii) {
    UIImage* snapshot = SnapshotTabHelper::FromWebState(&web_state_)
                            ->UpdateSnapshot(/*with_overlays=*/true,
                                             /*visibible_frame_only=*/true);

    ASSERT_TRUE(snapshot);
    EXPECT_TRUE(CGSizeEqualToSize(snapshot.size, kWebStateViewSize));
    EXPECT_TRUE(IsDominantColorForImage(snapshot, [UIColor redColor]));
  }
  SnapshotTabHelper::FromWebState(&web_state_)
      ->SetSnapshotCoalescingEnabled(false);

  EXPECT_EQ(delegate_.snapshotTakenCount, 1u);
}

// Tests that GenerateSnapshot ignores any cached snapshots and generate a new
// snapshot without adding it to the cache.
TEST_F(SnapshotTabHelperTest, GenerateSnapshot) {
  AddDefaultWebStateView();

  SetCachedSnapshot(
      UIImageWithSizeAndSolidColor(kDefaultSnapshotSize, [UIColor greenColor]));

  UIImage* snapshot = SnapshotTabHelper::FromWebState(&web_state_)
                          ->GenerateSnapshot(/*with_overlays=*/true,
                                             /*visibible_frame_only=*/true);

  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(CGSizeEqualToSize(snapshot.size, kWebStateViewSize));
  EXPECT_TRUE(IsDominantColorForImage(snapshot, [UIColor redColor]));

  UIImage* cached_snapshot = GetCachedSnapshot();
  EXPECT_FALSE(UIImagesAreEqual(snapshot, cached_snapshot));
}

// Tests that if snapshot coalescing is disabled, each call to GenerateSnapshot
// will cause a new snapshot to be generated.
TEST_F(SnapshotTabHelperTest, GenerateSnapshotNoCoalescing) {
  AddDefaultWebStateView();

  for (NSUInteger ii = 0; ii < kCountSnapshotToTake; ++ii) {
    UIImage* snapshot = SnapshotTabHelper::FromWebState(&web_state_)
                            ->GenerateSnapshot(/*with_overlays=*/true,
                                               /*visibible_frame_only=*/true);

    ASSERT_TRUE(snapshot);
    EXPECT_TRUE(CGSizeEqualToSize(snapshot.size, kWebStateViewSize));
    EXPECT_TRUE(IsDominantColorForImage(snapshot, [UIColor redColor]));
  }

  EXPECT_EQ(delegate_.snapshotTakenCount, kCountSnapshotToTake);
}

// Tests that if snapshot coalescing is enabled, only the first call to
// GenerateSnapshot will cause a new snapshot to be generated.
TEST_F(SnapshotTabHelperTest, GenerateSnapshotWithCoalescing) {
  AddDefaultWebStateView();

  SnapshotTabHelper::FromWebState(&web_state_)
      ->SetSnapshotCoalescingEnabled(true);
  for (NSUInteger ii = 0; ii < kCountSnapshotToTake; ++ii) {
    UIImage* snapshot = SnapshotTabHelper::FromWebState(&web_state_)
                            ->GenerateSnapshot(/*with_overlays=*/true,
                                               /*visibible_frame_only=*/true);

    ASSERT_TRUE(snapshot);
    EXPECT_TRUE(CGSizeEqualToSize(snapshot.size, kWebStateViewSize));
    EXPECT_TRUE(IsDominantColorForImage(snapshot, [UIColor redColor]));
  }
  SnapshotTabHelper::FromWebState(&web_state_)
      ->SetSnapshotCoalescingEnabled(false);

  EXPECT_EQ(delegate_.snapshotTakenCount, 1u);
}

// Tests that RemoveSnapshot deletes the cached snapshot from memory and
// disk (i.e. that SnapshotCache cannot retrieve a snapshot; depends on
// a correct implementation of SnapshotCache).
TEST_F(SnapshotTabHelperTest, RemoveSnapshot) {
  SetCachedSnapshot(
      UIImageWithSizeAndSolidColor(kDefaultSnapshotSize, [UIColor greenColor]));

  SnapshotTabHelper::FromWebState(&web_state_)->RemoveSnapshot();

  ASSERT_FALSE(GetCachedSnapshot());
}
