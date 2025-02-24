// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/image/canvas_image_source.h"

#include "base/logging.h"
#include "cc/paint/display_item_list.h"
#include "cc/paint/record_paint_canvas.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/switches.h"

namespace gfx {

namespace {

class PaddedImageSource : public CanvasImageSource {
 public:
  PaddedImageSource(const ImageSkia& image, const Insets& insets)
      : CanvasImageSource(Size(image.width() + insets.width(),
                               image.height() + insets.height()),
                          false),
        image_(image),
        insets_(insets) {}

  // CanvasImageSource:
  void Draw(Canvas* canvas) override {
    canvas->DrawImageInt(image_, insets_.left(), insets_.top());
  }

 private:
  const ImageSkia image_;
  const Insets insets_;

  DISALLOW_COPY_AND_ASSIGN(PaddedImageSource);
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// CanvasImageSource

// static
ImageSkia CanvasImageSource::CreatePadded(const ImageSkia& image,
                                          const Insets& insets) {
  return MakeImageSkia<PaddedImageSource>(image, insets);
}

CanvasImageSource::CanvasImageSource(const Size& size, bool is_opaque)
    : size_(size), is_opaque_(is_opaque) {}

ImageSkiaRep CanvasImageSource::GetImageForScale(float scale) {
  if (base::FeatureList::IsEnabled(features::kUsePaintRecordForImageSkia)) {
    scoped_refptr<cc::DisplayItemList> display_item_list =
        base::MakeRefCounted<cc::DisplayItemList>(
            cc::DisplayItemList::kToBeReleasedAsPaintOpBuffer);
    display_item_list->StartPaint();

    cc::RecordPaintCanvas record_canvas(
        display_item_list.get(),
        SkRect::MakeIWH(size_.width(), size_.height()));
    gfx::Canvas canvas(&record_canvas, scale);
    canvas.Scale(scale, scale);
    Draw(&canvas);

    display_item_list->EndPaintOfPairedEnd();
    display_item_list->Finalize();
    return ImageSkiaRep(display_item_list->ReleaseAsRecord(),
                        gfx::ScaleToCeiledSize(size_, scale), scale);
  }

  gfx::Canvas canvas(size_, scale, is_opaque_);
  Draw(&canvas);
  return ImageSkiaRep(canvas.GetBitmap(), scale);
}

}  // namespace gfx
