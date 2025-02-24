// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/aw_contents_io_thread_client.h"

#include <map>
#include <memory>
#include <utility>

#include "android_webview/browser/net/aw_web_resource_request.h"
#include "android_webview/browser/net/aw_web_resource_response.h"
#include "android_webview/common/devtools_instrumentation.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/jni_weak_ref.h"
#include "base/containers/flat_set.h"
#include "base/lazy_instance.h"
#include "base/synchronization/lock.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/resource_request_info.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "jni/AwContentsBackgroundThreadClient_jni.h"
#include "jni/AwContentsIoThreadClient_jni.h"
#include "net/base/data_url.h"
#include "net/url_request/url_request.h"

using base::android::AttachCurrentThread;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;
using base::android::ToJavaArrayOfStrings;
using base::LazyInstance;
using content::BrowserThread;
using content::RenderFrameHost;
using content::ResourceType;
using content::WebContents;
using std::map;
using std::pair;
using std::string;

namespace android_webview {

namespace {

struct IoThreadClientData {
  bool pending_association;
  JavaObjectWeakGlobalRef io_thread_client;

  IoThreadClientData();
};

IoThreadClientData::IoThreadClientData() : pending_association(false) {}

typedef map<pair<int, int>, IoThreadClientData>
    RenderFrameHostToIoThreadClientType;

typedef pair<base::flat_set<RenderFrameHost*>, IoThreadClientData>
    HostsAndClientDataPair;

// When browser side navigation is enabled, RenderFrameIDs do not have
// valid render process host and render frame ids for frame navigations.
// We need to identify these by using FrameTreeNodeIds. Furthermore, we need
// to keep track of which RenderFrameHosts are associated with each
// FrameTreeNodeId, so we know when the last RenderFrameHost is deleted (and
// therefore the FrameTreeNodeId should be removed).
typedef map<int, HostsAndClientDataPair> FrameTreeNodeToIoThreadClientType;

static pair<int, int> GetRenderFrameHostIdPair(RenderFrameHost* rfh) {
  return pair<int, int>(rfh->GetProcess()->GetID(), rfh->GetRoutingID());
}

// RfhToIoThreadClientMap -----------------------------------------------------
class RfhToIoThreadClientMap {
 public:
  static RfhToIoThreadClientMap* GetInstance();
  void Set(pair<int, int> rfh_id, const IoThreadClientData& client);
  bool Get(pair<int, int> rfh_id, IoThreadClientData* client);

  bool Get(int frame_tree_node_id, IoThreadClientData* client);

  // Prefer to call these when RenderFrameHost* is available, because they
  // update both maps at the same time.
  void Set(RenderFrameHost* rfh, const IoThreadClientData& client);
  void Erase(RenderFrameHost* rfh);

 private:
  base::Lock map_lock_;
  // We maintain two maps simultaneously so that we can always get the correct
  // IoThreadClientData, even when only HostIdPair or FrameTreeNodeId is
  // available.
  RenderFrameHostToIoThreadClientType rfh_to_io_thread_client_;
  FrameTreeNodeToIoThreadClientType frame_tree_node_to_io_thread_client_;
};

// static
LazyInstance<RfhToIoThreadClientMap>::DestructorAtExit g_instance_ =
    LAZY_INSTANCE_INITIALIZER;

// static
LazyInstance<JavaObjectWeakGlobalRef>::DestructorAtExit g_sw_instance_ =
    LAZY_INSTANCE_INITIALIZER;

// static
RfhToIoThreadClientMap* RfhToIoThreadClientMap::GetInstance() {
  return g_instance_.Pointer();
}

void RfhToIoThreadClientMap::Set(pair<int, int> rfh_id,
                                 const IoThreadClientData& client) {
  base::AutoLock lock(map_lock_);
  rfh_to_io_thread_client_[rfh_id] = client;
}

bool RfhToIoThreadClientMap::Get(pair<int, int> rfh_id,
                                 IoThreadClientData* client) {
  base::AutoLock lock(map_lock_);
  RenderFrameHostToIoThreadClientType::iterator iterator =
      rfh_to_io_thread_client_.find(rfh_id);
  if (iterator == rfh_to_io_thread_client_.end())
    return false;

  *client = iterator->second;
  return true;
}

bool RfhToIoThreadClientMap::Get(int frame_tree_node_id,
                                 IoThreadClientData* client) {
  base::AutoLock lock(map_lock_);
  FrameTreeNodeToIoThreadClientType::iterator iterator =
      frame_tree_node_to_io_thread_client_.find(frame_tree_node_id);
  if (iterator == frame_tree_node_to_io_thread_client_.end())
    return false;

  *client = iterator->second.second;
  return true;
}

void RfhToIoThreadClientMap::Set(RenderFrameHost* rfh,
                                 const IoThreadClientData& client) {
  int frame_tree_node_id = rfh->GetFrameTreeNodeId();
  pair<int, int> rfh_id = GetRenderFrameHostIdPair(rfh);
  base::AutoLock lock(map_lock_);

  // If this FrameTreeNodeId already has an associated IoThreadClientData, add
  // this RenderFrameHost to the hosts set (it's harmless to overwrite the
  // IoThreadClientData). Otherwise, operator[] creates a new map entry and we
  // add this RenderFrameHost to the hosts set and insert |client| in the pair.
  HostsAndClientDataPair& current_entry =
      frame_tree_node_to_io_thread_client_[frame_tree_node_id];
  current_entry.second = client;
  current_entry.first.insert(rfh);

  // Always add the entry to the HostIdPair map, since entries are 1:1 with
  // RenderFrameHosts.
  rfh_to_io_thread_client_[rfh_id] = client;
}

void RfhToIoThreadClientMap::Erase(RenderFrameHost* rfh) {
  int frame_tree_node_id = rfh->GetFrameTreeNodeId();
  pair<int, int> rfh_id = GetRenderFrameHostIdPair(rfh);
  base::AutoLock lock(map_lock_);
  HostsAndClientDataPair& current_entry =
      frame_tree_node_to_io_thread_client_[frame_tree_node_id];
  size_t num_erased = current_entry.first.erase(rfh);
  DCHECK(num_erased == 1);
  // Only remove this entry from the FrameTreeNodeId map if there are no more
  // live RenderFrameHosts.
  if (current_entry.first.empty()) {
    frame_tree_node_to_io_thread_client_.erase(frame_tree_node_id);
  }

  // Always safe to remove the entry from the HostIdPair map, since entries are
  // 1:1 with RenderFrameHosts.
  rfh_to_io_thread_client_.erase(rfh_id);
}

// ClientMapEntryUpdater ------------------------------------------------------

class ClientMapEntryUpdater : public content::WebContentsObserver {
 public:
  ClientMapEntryUpdater(JNIEnv* env,
                        WebContents* web_contents,
                        jobject jdelegate);

  void RenderFrameCreated(RenderFrameHost* render_frame_host) override;
  void RenderFrameDeleted(RenderFrameHost* render_frame_host) override;
  void WebContentsDestroyed() override;

 private:
  JavaObjectWeakGlobalRef jdelegate_;
};

ClientMapEntryUpdater::ClientMapEntryUpdater(JNIEnv* env,
                                             WebContents* web_contents,
                                             jobject jdelegate)
    : content::WebContentsObserver(web_contents), jdelegate_(env, jdelegate) {
  DCHECK(web_contents);
  DCHECK(jdelegate);

  if (web_contents->GetMainFrame())
    RenderFrameCreated(web_contents->GetMainFrame());
}

void ClientMapEntryUpdater::RenderFrameCreated(RenderFrameHost* rfh) {
  IoThreadClientData client_data;
  client_data.io_thread_client = jdelegate_;
  client_data.pending_association = false;
  RfhToIoThreadClientMap::GetInstance()->Set(rfh, client_data);
}

void ClientMapEntryUpdater::RenderFrameDeleted(RenderFrameHost* rfh) {
  RfhToIoThreadClientMap::GetInstance()->Erase(rfh);
}

void ClientMapEntryUpdater::WebContentsDestroyed() {
  delete this;
}

}  // namespace

// AwContentsIoThreadClient -----------------------------------------------

// static
std::unique_ptr<AwContentsIoThreadClient> AwContentsIoThreadClient::FromID(
    int render_process_id,
    int render_frame_id) {
  pair<int, int> rfh_id(render_process_id, render_frame_id);
  IoThreadClientData client_data;
  if (!RfhToIoThreadClientMap::GetInstance()->Get(rfh_id, &client_data))
    return std::unique_ptr<AwContentsIoThreadClient>();

  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> java_delegate =
      client_data.io_thread_client.get(env);
  DCHECK(!client_data.pending_association || java_delegate.is_null());
  return std::unique_ptr<AwContentsIoThreadClient>(new AwContentsIoThreadClient(
      client_data.pending_association, java_delegate));
}

std::unique_ptr<AwContentsIoThreadClient> AwContentsIoThreadClient::FromID(
    int frame_tree_node_id) {
  IoThreadClientData client_data;
  if (!RfhToIoThreadClientMap::GetInstance()->Get(frame_tree_node_id,
                                                  &client_data))
    return std::unique_ptr<AwContentsIoThreadClient>();

  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> java_delegate =
      client_data.io_thread_client.get(env);
  DCHECK(!client_data.pending_association || java_delegate.is_null());
  return std::unique_ptr<AwContentsIoThreadClient>(new AwContentsIoThreadClient(
      client_data.pending_association, java_delegate));
}

// static
void AwContentsIoThreadClient::SubFrameCreated(int render_process_id,
                                               int parent_render_frame_id,
                                               int child_render_frame_id) {
  pair<int, int> parent_rfh_id(render_process_id, parent_render_frame_id);
  pair<int, int> child_rfh_id(render_process_id, child_render_frame_id);
  IoThreadClientData client_data;
  if (!RfhToIoThreadClientMap::GetInstance()->Get(parent_rfh_id,
                                                  &client_data)) {
    NOTREACHED();
    return;
  }

  RfhToIoThreadClientMap::GetInstance()->Set(child_rfh_id, client_data);
}

// static
void AwContentsIoThreadClient::RegisterPendingContents(
    WebContents* web_contents) {
  IoThreadClientData client_data;
  client_data.pending_association = true;
  RfhToIoThreadClientMap::GetInstance()->Set(
      GetRenderFrameHostIdPair(web_contents->GetMainFrame()), client_data);
}

// static
void AwContentsIoThreadClient::Associate(WebContents* web_contents,
                                         const JavaRef<jobject>& jclient) {
  JNIEnv* env = AttachCurrentThread();
  // The ClientMapEntryUpdater lifespan is tied to the WebContents.
  new ClientMapEntryUpdater(env, web_contents, jclient.obj());
}

// static
void AwContentsIoThreadClient::SetServiceWorkerIoThreadClient(
    const base::android::JavaRef<jobject>& jclient,
    const base::android::JavaRef<jobject>& browser_context) {
  // TODO: currently there is only one browser context so it is ok to
  // store in a global variable, in the future use browser_context to
  // obtain the correct instance.
  JavaObjectWeakGlobalRef temp(AttachCurrentThread(), jclient.obj());
  g_sw_instance_.Get() = temp;
}

// static
std::unique_ptr<AwContentsIoThreadClient>
AwContentsIoThreadClient::GetServiceWorkerIoThreadClient() {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> java_delegate = g_sw_instance_.Get().get(env);

  if (java_delegate.is_null())
    return std::unique_ptr<AwContentsIoThreadClient>();

  return std::unique_ptr<AwContentsIoThreadClient>(
      new AwContentsIoThreadClient(false, java_delegate));
}

AwContentsIoThreadClient::AwContentsIoThreadClient(bool pending_association,
                                                   const JavaRef<jobject>& obj)
    : pending_association_(pending_association), java_object_(obj) {}

AwContentsIoThreadClient::~AwContentsIoThreadClient() {
  // explict, out-of-line destructor.
}

bool AwContentsIoThreadClient::PendingAssociation() const {
  return pending_association_;
}

AwContentsIoThreadClient::CacheMode AwContentsIoThreadClient::GetCacheMode()
    const {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (java_object_.is_null())
    return AwContentsIoThreadClient::LOAD_DEFAULT;

  JNIEnv* env = AttachCurrentThread();
  return static_cast<AwContentsIoThreadClient::CacheMode>(
      Java_AwContentsIoThreadClient_getCacheMode(env, java_object_));
}

namespace {

std::unique_ptr<AwWebResourceResponse> RunShouldInterceptRequest(
    const AwWebResourceRequest& request,
    JavaObjectWeakGlobalRef ref) {
  base::AssertBlockingAllowedDeprecated();

  JNIEnv* env = AttachCurrentThread();
  base::android::ScopedJavaLocalRef<jobject> obj = ref.get(env);
  if (obj.is_null())
    return nullptr;

  AwWebResourceRequest::AwJavaWebResourceRequest java_web_resource_request;
  AwWebResourceRequest::ConvertToJava(env, request, &java_web_resource_request);

  devtools_instrumentation::ScopedEmbedderCallbackTask embedder_callback(
      "shouldInterceptRequest");
  ScopedJavaLocalRef<jobject> ret =
      Java_AwContentsBackgroundThreadClient_shouldInterceptRequestFromNative(
          env, obj, java_web_resource_request.jurl, request.is_main_frame,
          request.has_user_gesture, java_web_resource_request.jmethod,
          java_web_resource_request.jheader_names,
          java_web_resource_request.jheader_values);
  return std::unique_ptr<AwWebResourceResponse>(
      ret.is_null() ? nullptr : new AwWebResourceResponse(ret));
}

std::unique_ptr<AwWebResourceResponse> ReturnNull() {
  return std::unique_ptr<AwWebResourceResponse>();
}

}  // namespace

void AwContentsIoThreadClient::ShouldInterceptRequestAsync(
    const net::URLRequest* request,
    ShouldInterceptRequestResultCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  base::OnceCallback<std::unique_ptr<AwWebResourceResponse>()> get_response =
      base::BindOnce(&ReturnNull);
  JNIEnv* env = AttachCurrentThread();
  if (bg_thread_client_object_.is_null() && !java_object_.is_null()) {
    bg_thread_client_object_.Reset(
        Java_AwContentsIoThreadClient_getBackgroundThreadClient(env,
                                                                java_object_));
  }
  if (!bg_thread_client_object_.is_null()) {
    get_response = base::BindOnce(
        &RunShouldInterceptRequest, AwWebResourceRequest(*request),
        JavaObjectWeakGlobalRef(env, bg_thread_client_object_.obj()));
  }
  base::PostTaskAndReplyWithResult(sequenced_task_runner_.get(), FROM_HERE,
                                   std::move(get_response),
                                   std::move(callback));
}

bool AwContentsIoThreadClient::ShouldBlockContentUrls() const {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (java_object_.is_null())
    return false;

  JNIEnv* env = AttachCurrentThread();
  return Java_AwContentsIoThreadClient_shouldBlockContentUrls(env,
                                                              java_object_);
}

bool AwContentsIoThreadClient::ShouldBlockFileUrls() const {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (java_object_.is_null())
    return false;

  JNIEnv* env = AttachCurrentThread();
  return Java_AwContentsIoThreadClient_shouldBlockFileUrls(env, java_object_);
}

bool AwContentsIoThreadClient::ShouldAcceptThirdPartyCookies() const {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (java_object_.is_null())
    return false;

  JNIEnv* env = AttachCurrentThread();
  return Java_AwContentsIoThreadClient_shouldAcceptThirdPartyCookies(
      env, java_object_);
}

bool AwContentsIoThreadClient::GetSafeBrowsingEnabled() const {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (java_object_.is_null())
    return false;

  JNIEnv* env = AttachCurrentThread();
  return Java_AwContentsIoThreadClient_getSafeBrowsingEnabled(env,
                                                              java_object_);
}

bool AwContentsIoThreadClient::ShouldBlockNetworkLoads() const {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (java_object_.is_null())
    return false;

  JNIEnv* env = AttachCurrentThread();
  return Java_AwContentsIoThreadClient_shouldBlockNetworkLoads(env,
                                                               java_object_);
}

}  // namespace android_webview
