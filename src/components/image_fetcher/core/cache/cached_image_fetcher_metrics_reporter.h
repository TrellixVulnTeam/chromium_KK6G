// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_IMAGE_FETCHER_CORE_CACHE_CACHED_IMAGE_FETCHER_METRICS_REPORTER_H_
#define COMPONENTS_IMAGE_FETCHER_CORE_CACHE_CACHED_IMAGE_FETCHER_METRICS_REPORTER_H_

#include "base/time/time.h"

namespace image_fetcher {

// Enum for the result of the fetch, reported through UMA. Present in enums.xml
// as CachedImageFetcherEvent. New values should be added at the end and things
// should not be renumbered.
enum class CachedImageFetcherEvent {
  kImageRequest = 0,
  kCacheHit = 1,
  kCacheMiss = 2,
  kCacheDecodingError = 3,
  kTranscodingError = 4,
  kFailure = 5,
  kMaxValue = kFailure,
};

// Tracks the various forms of timing events.
enum class LoadTimeType {
  kLoadFromCache = 0,
  kLoadFromNetwork = 1,
  kLoadFromNetworkAfterCacheHit = 2
};

class CachedImageFetcherMetricsReporter {
 public:
  // Report cache events, used by CachedImageFetcher and composing classes.
  static void ReportEvent(CachedImageFetcherEvent event);

  // Report timing for various Cache events related to CachedImageFetcher.
  static void ReportLoadTime(LoadTimeType type, base::Time start_time);
};

}  // namespace image_fetcher

#endif  // COMPONENTS_IMAGE_FETCHER_CORE_CACHE_CACHED_IMAGE_FETCHER_METRICS_REPORTER_H_