// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/skia_common.h"

#include <stddef.h>
#include <string>

#include "base/strings/string_number_conversions.h"
#include "cc/paint/display_item_list.h"
#include "cc/paint/draw_image.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_image_builder.h"
#include "cc/paint/skottie_wrapper.h"
#include "cc/test/fake_paint_image_generator.h"
#include "third_party/skia/include/core/SkImageGenerator.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/skia_util.h"

namespace cc {
namespace {
constexpr char kSkottieJSON[] =
    "{"
    "  \"v\" : \"4.12.0\","
    "  \"fr\": 30,"
    "  \"w\" : $WIDTH,"
    "  \"h\" : $HEIGHT,"
    "  \"ip\": 0,"
    "  \"op\": $DURATION,"
    "  \"assets\": [],"

    "  \"layers\": ["
    "    {"
    "      \"ty\": 1,"
    "      \"sw\": $WIDTH,"
    "      \"sh\": $HEIGHT,"
    "      \"sc\": \"#00ff00\","
    "      \"ip\": 0,"
    "      \"op\": $DURATION"
    "    }"
    "  ]"
    "}";

constexpr char kSkottieWidthToken[] = "$WIDTH";
constexpr char kSkottieHeightToken[] = "$HEIGHT";
constexpr char kSkottieDurationToken[] = "$DURATION";
constexpr int kFps = 30;
}  // namespace

void DrawDisplayList(unsigned char* buffer,
                     const gfx::Rect& layer_rect,
                     scoped_refptr<const DisplayItemList> list) {
  SkImageInfo info =
      SkImageInfo::MakeN32Premul(layer_rect.width(), layer_rect.height());
  SkBitmap bitmap;
  bitmap.installPixels(info, buffer, info.minRowBytes());
  SkCanvas canvas(bitmap);
  canvas.clipRect(gfx::RectToSkRect(layer_rect));
  list->Raster(&canvas);
}

bool AreDisplayListDrawingResultsSame(const gfx::Rect& layer_rect,
                                      const DisplayItemList* list_a,
                                      const DisplayItemList* list_b) {
  const size_t pixel_size = 4 * layer_rect.size().GetArea();

  std::unique_ptr<unsigned char[]> pixels_a(new unsigned char[pixel_size]);
  std::unique_ptr<unsigned char[]> pixels_b(new unsigned char[pixel_size]);
  memset(pixels_a.get(), 0, pixel_size);
  memset(pixels_b.get(), 0, pixel_size);
  DrawDisplayList(pixels_a.get(), layer_rect, list_a);
  DrawDisplayList(pixels_b.get(), layer_rect, list_b);

  return !memcmp(pixels_a.get(), pixels_b.get(), pixel_size);
}

Region ImageRectsToRegion(const DiscardableImageMap::Rects& rects) {
  Region region;
  for (const auto& r : rects.container())
    region.Union(r);
  return region;
}

sk_sp<PaintImageGenerator> CreatePaintImageGenerator(const gfx::Size& size) {
  return sk_make_sp<FakePaintImageGenerator>(
      SkImageInfo::MakeN32Premul(size.width(), size.height()));
}

PaintImage CreateDiscardablePaintImage(const gfx::Size& size,
                                       sk_sp<SkColorSpace> color_space,
                                       bool allocate_encoded_data,
                                       PaintImage::Id id,
                                       SkColorType color_type) {
  if (!color_space)
    color_space = SkColorSpace::MakeSRGB();
  if (id == PaintImage::kInvalidId)
    id = PaintImage::GetNextId();

  SkImageInfo info = SkImageInfo::Make(size.width(), size.height(), color_type,
                                       kPremul_SkAlphaType, color_space);
  auto paint_image =
      PaintImageBuilder::WithDefault()
          .set_id(id)
          .set_paint_image_generator(sk_make_sp<FakePaintImageGenerator>(
              info, std::vector<FrameMetadata>{FrameMetadata()},
              allocate_encoded_data))
          // For simplicity, assume that any paint image created for testing is
          // unspecified decode mode as would be the case with most img tags on
          // the web.
          .set_decoding_mode(PaintImage::DecodingMode::kUnspecified)
          .TakePaintImage();
  return paint_image;
}

DrawImage CreateDiscardableDrawImage(const gfx::Size& size,
                                     sk_sp<SkColorSpace> color_space,
                                     SkRect rect,
                                     SkFilterQuality filter_quality,
                                     const SkMatrix& matrix) {
  SkIRect irect;
  rect.roundOut(&irect);

  return DrawImage(CreateDiscardablePaintImage(size, color_space), irect,
                   filter_quality, matrix);
}

PaintImage CreateAnimatedImage(const gfx::Size& size,
                               std::vector<FrameMetadata> frames,
                               int repetition_count,
                               PaintImage::Id id) {
  return PaintImageBuilder::WithDefault()
      .set_id(id)
      .set_paint_image_generator(sk_make_sp<FakePaintImageGenerator>(
          SkImageInfo::MakeN32Premul(size.width(), size.height()),
          std::move(frames)))
      .set_animation_type(PaintImage::AnimationType::ANIMATED)
      .set_repetition_count(repetition_count)
      .TakePaintImage();
}

PaintImage CreateBitmapImage(const gfx::Size& size, SkColorType color_type) {
  SkBitmap bitmap;
  auto info = SkImageInfo::Make(size.width(), size.height(), color_type,
                                kPremul_SkAlphaType, nullptr /* color_space */);
  bitmap.allocPixels(info);
  bitmap.eraseColor(SK_AlphaTRANSPARENT);
  return PaintImageBuilder::WithDefault()
      .set_id(PaintImage::GetNextId())
      .set_image(SkImage::MakeFromBitmap(bitmap),
                 PaintImage::GetNextContentId())
      .TakePaintImage();
}

scoped_refptr<SkottieWrapper> CreateSkottie(const gfx::Size& size,
                                            int duration_secs) {
  std::string json(kSkottieJSON);

  for (size_t pos = json.find(kSkottieWidthToken); pos != std::string::npos;
       pos = json.find(kSkottieWidthToken)) {
    json.replace(pos, strlen(kSkottieWidthToken),
                 base::IntToString(size.width()));
  }

  for (size_t pos = json.find(kSkottieHeightToken); pos != std::string::npos;
       pos = json.find(kSkottieHeightToken)) {
    json.replace(pos, strlen(kSkottieHeightToken),
                 base::IntToString(size.height()));
  }

  for (size_t pos = json.find(kSkottieDurationToken); pos != std::string::npos;
       pos = json.find(kSkottieDurationToken)) {
    json.replace(pos, strlen(kSkottieDurationToken),
                 base::IntToString(duration_secs * kFps));
  }

  return base::MakeRefCounted<SkottieWrapper>(
      std::make_unique<SkMemoryStream>(json.c_str(), json.size()));
}

}  // namespace cc
