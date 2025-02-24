// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_EXPLORE_SITES_EXPLORE_SITES_SERVICE_IMPL_H_
#define CHROME_BROWSER_ANDROID_EXPLORE_SITES_EXPLORE_SITES_SERVICE_IMPL_H_

#include <memory>

#include "base/macros.h"
#include "chrome/browser/android/explore_sites/explore_sites_fetcher.h"
#include "chrome/browser/android/explore_sites/explore_sites_service.h"
#include "chrome/browser/android/explore_sites/explore_sites_store.h"
#include "chrome/browser/android/explore_sites/explore_sites_types.h"
#include "chrome/browser/android/explore_sites/history_statistics_reporter.h"
#include "chrome/browser/android/explore_sites/image_helper.h"
#include "components/offline_pages/task/task_queue.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

using offline_pages::TaskQueue;

namespace explore_sites {

class ExploreSitesServiceImpl : public ExploreSitesService,
                                public TaskQueue::Delegate {
 public:
  ExploreSitesServiceImpl(
      std::unique_ptr<ExploreSitesStore> store,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      std::unique_ptr<HistoryStatisticsReporter> history_statistics_reporter);
  ~ExploreSitesServiceImpl() override;

  static bool IsExploreSitesEnabled();

  // ExploreSitesService implementation.
  void GetCatalog(CatalogCallback callback) override;

  void GetCategoryImage(int category_id,
                        int pixel_size,
                        BitmapCallback callback) override;

  // Compose a single site icon and return via |callback|.
  void GetSiteImage(int site_id, BitmapCallback callback) override;

  // Compose a category icon containing [1 - 4] site icons and return via
  // |callback|.
  void UpdateCatalogFromNetwork(bool is_immediate_fetch,
                                const std::string& accept_languages,
                                BooleanCallback callback) override;

  // Add the url to the blacklist.
  void BlacklistSite(const std::string& url) override;

 private:
  // KeyedService implementation:
  void Shutdown() override;

  // TaskQueue::Delegate implementation:
  void OnTaskQueueIsIdle() override;

  // Callback returning from the UpdateCatalogFromNetwork operation.  It
  // passes along the call back to the bridge and eventually back to Java land.
  void OnCatalogFetched(ExploreSitesRequestStatus status,
                        std::unique_ptr<std::string> serialized_protobuf);

  void NotifyCatalogUpdated(std::vector<BooleanCallback> callbacks,
                            bool success);

  // Wrappers to call ImageHelper::Compose[Site|Category]Image.
  void ComposeSiteImage(BitmapCallback callback, EncodedImageList images);
  void ComposeCategoryImage(BitmapCallback callback,
                            int pixel_size,
                            EncodedImageList images);

  // True when Chrome starts up, this is reset after the catalog is requested
  // the first time in Chrome. This prevents the ESP from changing out from
  // under a viewer.
  bool check_for_new_catalog_ = true;

  ImageHelper image_helper_;

  // Used to control access to the ExploreSitesStore.
  TaskQueue task_queue_;
  std::unique_ptr<ExploreSitesStore> explore_sites_store_;
  scoped_refptr<network ::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<ExploreSitesFetcher> explore_sites_fetcher_;
  std::unique_ptr<HistoryStatisticsReporter> history_statistics_reporter_;
  std::vector<BooleanCallback> update_catalog_callbacks_;
  base::WeakPtrFactory<ExploreSitesServiceImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ExploreSitesServiceImpl);
};

}  // namespace explore_sites

#endif  // CHROME_BROWSER_ANDROID_EXPLORE_SITES_EXPLORE_SITES_SERVICE_IMPL_H_
