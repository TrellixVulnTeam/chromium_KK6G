// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/media/render_frame_audio_output_stream_factory.h"

#include <cstdint>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/containers/flat_set.h"
#include "base/containers/unique_ptr_adapters.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/task/post_task.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/unguessable_token.h"
#include "content/browser/media/forwarding_audio_stream_factory.h"
#include "content/browser/renderer_host/media/audio_output_authorization_handler.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "media/base/output_device_info.h"
#include "media/mojo/interfaces/audio_output_stream.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace content {

class RenderFrameAudioOutputStreamFactory::Core final
    : public mojom::RendererAudioOutputStreamFactory {
 public:
  Core(RenderFrameHost* frame,
       media::AudioSystem* audio_system,
       MediaStreamManager* media_stream_manager,
       mojom::RendererAudioOutputStreamFactoryRequest request);

  ~Core() final = default;

  void Init(mojom::RendererAudioOutputStreamFactoryRequest request);

  size_t current_number_of_providers_for_testing() {
    return stream_providers_.size();
  }

 private:
  // This class implements media::mojom::AudioOutputStreamProvider for a single
  // streams and cleans itself up (using the |owner| pointer) when done.
  class ProviderImpl final : public media::mojom::AudioOutputStreamProvider {
   public:
    ProviderImpl(media::mojom::AudioOutputStreamProviderRequest request,
                 RenderFrameAudioOutputStreamFactory::Core* owner,
                 const std::string& device_id)
        : owner_(owner),
          device_id_(device_id),
          binding_(this, std::move(request)) {
      DCHECK_CURRENTLY_ON(BrowserThread::IO);
      // Unretained is safe since |this| owns |binding_|.
      binding_.set_connection_error_handler(
          base::BindOnce(&ProviderImpl::Done, base::Unretained(this)));
    }

    ~ProviderImpl() final { DCHECK_CURRENTLY_ON(BrowserThread::IO); }

    void Acquire(
        const media::AudioParameters& params,
        media::mojom::AudioOutputStreamProviderClientPtr provider_client,
        const base::Optional<base::UnguessableToken>& processing_id) final {
      DCHECK_CURRENTLY_ON(BrowserThread::IO);
      TRACE_EVENT1("audio",
                   "RenderFrameAudioOutputStreamFactory::ProviderImpl::Acquire",
                   "raw device id", device_id_);

      owner_->forwarding_factory_->CreateOutputStream(
          owner_->process_id_, owner_->frame_id_, device_id_, params,
          processing_id, std::move(provider_client));

      // Since the stream creation has been propagated, |this| is no longer
      // needed.
      Done();
    }

    void Done() { owner_->DeleteProvider(this); }

   private:
    RenderFrameAudioOutputStreamFactory::Core* const owner_;
    const std::string device_id_;

    mojo::Binding<media::mojom::AudioOutputStreamProvider> binding_;

    DISALLOW_COPY_AND_ASSIGN(ProviderImpl);
  };

  using OutputStreamProviderSet =
      base::flat_set<std::unique_ptr<media::mojom::AudioOutputStreamProvider>,
                     base::UniquePtrComparator>;

  // mojom::RendererAudioOutputStreamFactory implementation.
  void RequestDeviceAuthorization(
      media::mojom::AudioOutputStreamProviderRequest provider_request,
      int32_t session_id,
      const std::string& device_id,
      RequestDeviceAuthorizationCallback callback) final;

  // Here, the |raw_device_id| is used to create the stream, and
  // |device_id_for_renderer| is nonempty in the case when the renderer
  // requested a device using a |session_id|, to let it know which device was
  // chosen. This id is hashed.
  void AuthorizationCompleted(
      base::TimeTicks auth_start_time,
      media::mojom::AudioOutputStreamProviderRequest request,
      RequestDeviceAuthorizationCallback callback,
      media::OutputDeviceStatus status,
      const media::AudioParameters& params,
      const std::string& raw_device_id,
      const std::string& device_id_for_renderer);

  void DeleteProvider(media::mojom::AudioOutputStreamProvider* stream_provider);

  const int process_id_;
  const int frame_id_;
  AudioOutputAuthorizationHandler authorization_handler_;

  mojo::Binding<mojom::RendererAudioOutputStreamFactory> binding_;
  ForwardingAudioStreamFactory::Core* forwarding_factory_;

  // The OutputStreamProviders for authorized streams are kept here while
  // waiting for the renderer to finish creating the stream, and destructed
  // afterwards.
  OutputStreamProviderSet stream_providers_;

  // Weak pointers are used to cancel device authorizations that are in flight
  // while |this| is destructed.
  base::WeakPtrFactory<Core> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(Core);
};

RenderFrameAudioOutputStreamFactory::RenderFrameAudioOutputStreamFactory(
    RenderFrameHost* frame,
    media::AudioSystem* audio_system,
    MediaStreamManager* media_stream_manager,
    mojom::RendererAudioOutputStreamFactoryRequest request)
    : core_(new Core(frame,
                     audio_system,
                     media_stream_manager,
                     std::move(request))) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

RenderFrameAudioOutputStreamFactory::~RenderFrameAudioOutputStreamFactory() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Ensure |core_| is deleted on the right thread. DeleteOnIOThread isn't used
  // as it doesn't post in case it is already executed on the right thread. That
  // causes issues in unit tests where the UI thread and the IO thread are the
  // same.
  base::PostTaskWithTraits(
      FROM_HERE, {BrowserThread::IO},
      base::BindOnce([](std::unique_ptr<Core>) {}, std::move(core_)));
}

size_t
RenderFrameAudioOutputStreamFactory::CurrentNumberOfProvidersForTesting() {
  return core_->current_number_of_providers_for_testing();
}

RenderFrameAudioOutputStreamFactory::Core::Core(
    RenderFrameHost* frame,
    media::AudioSystem* audio_system,
    MediaStreamManager* media_stream_manager,
    mojom::RendererAudioOutputStreamFactoryRequest request)
    : process_id_(frame->GetProcess()->GetID()),
      frame_id_(frame->GetRoutingID()),
      authorization_handler_(audio_system, media_stream_manager, process_id_),
      binding_(this),
      forwarding_factory_(ForwardingAudioStreamFactory::CoreForFrame(frame)),
      weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!forwarding_factory_) {
    // The only case when we not have a forwarding factory is when the
    // frame belongs to an interstitial. Interstitials don't need audio, so it's
    // fine to drop the request.
    return;
  }

  // Unretained is safe since the destruction of |this| is posted to the IO
  // thread.
  base::PostTaskWithTraits(
      FROM_HERE, {BrowserThread::IO},
      base::BindOnce(&Core::Init, base::Unretained(this), std::move(request)));
}

void RenderFrameAudioOutputStreamFactory::Core::Init(
    mojom::RendererAudioOutputStreamFactoryRequest request) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(forwarding_factory_);

  binding_.Bind(std::move(request));
}

void RenderFrameAudioOutputStreamFactory::Core::RequestDeviceAuthorization(
    media::mojom::AudioOutputStreamProviderRequest provider_request,
    int32_t session_id,
    const std::string& device_id,
    RequestDeviceAuthorizationCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(forwarding_factory_);
  TRACE_EVENT2(
      "audio",
      "RenderFrameAudioOutputStreamFactory::RequestDeviceAuthorization",
      "device id", device_id, "session_id", session_id);

  const base::TimeTicks auth_start_time = base::TimeTicks::Now();

  AudioOutputAuthorizationHandler::AuthorizationCompletedCallback
      completed_callback = base::BindOnce(
          &RenderFrameAudioOutputStreamFactory::Core::AuthorizationCompleted,
          weak_ptr_factory_.GetWeakPtr(), auth_start_time,
          std::move(provider_request), std::move(callback));

  authorization_handler_.RequestDeviceAuthorization(
      frame_id_, session_id, device_id, std::move(completed_callback));
}

void RenderFrameAudioOutputStreamFactory::Core::AuthorizationCompleted(
    base::TimeTicks auth_start_time,
    media::mojom::AudioOutputStreamProviderRequest request,
    RequestDeviceAuthorizationCallback callback,
    media::OutputDeviceStatus status,
    const media::AudioParameters& params,
    const std::string& raw_device_id,
    const std::string& device_id_for_renderer) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  TRACE_EVENT2("audio",
               "RenderFrameAudioOutputStreamFactory::AuthorizationCompleted",
               "raw device id", raw_device_id, "status", status);

  AudioOutputAuthorizationHandler::UMALogDeviceAuthorizationTime(
      auth_start_time);

  // If |status| is not OK, this call will be considered as an error signal by
  // the renderer.
  std::move(callback).Run(status, params, device_id_for_renderer);

  if (status == media::OUTPUT_DEVICE_STATUS_OK) {
    stream_providers_.insert(std::make_unique<ProviderImpl>(
        std::move(request), this, std::move(raw_device_id)));
  }
}

void RenderFrameAudioOutputStreamFactory::Core::DeleteProvider(
    media::mojom::AudioOutputStreamProvider* stream_provider) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  size_t deleted = stream_providers_.erase(stream_provider);
  DCHECK_EQ(1u, deleted);
}

}  // namespace content
