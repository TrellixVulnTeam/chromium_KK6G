// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_MEDIA_RENDER_FRAME_AUDIO_OUTPUT_STREAM_FACTORY_H_
#define CONTENT_BROWSER_RENDERER_HOST_MEDIA_RENDER_FRAME_AUDIO_OUTPUT_STREAM_FACTORY_H_

#include <cstddef>
#include <memory>

#include "base/macros.h"
#include "content/common/content_export.h"
#include "content/common/media/renderer_audio_output_stream_factory.mojom.h"

namespace media {
class AudioSystem;
}  // namespace media

namespace content {

class MediaStreamManager;
class RenderFrameHost;

// This class is related to ForwardingAudioStreamFactory as follows:
//
//     WebContentsImpl       <--        RenderFrameHostImpl
//           ^                                  ^
//           |                                  |
//  ForwardingAudioStreamFactory   RenderFrameAudioOutputStreamFactory
//           ^                                  ^
//           |                                  |
//      FASF::Core           <--          RFAOSF::Core
//
// Both FASF::Core and RFAOSF::Core live on (and are destructed on) the IO
// thread. ForwardingAudioStreamFactory exists until destruction of its owning
// WebContentsImpl. Since WebContentsImpl outlives all of its
// RenderFrameHostImpls, ForwardingAudioStreamFactory will outlive
// RenderFrameAudioOutputStreamFactory. As a consequence, the destruction of
// FASF::Core will be posted to the IO thread after the destruction of
// RFAOSF::Core has been posted, so RFAOSF::Core can safely have a raw pointer
// to FASF::Core

// This class takes care of stream requests from a render frame. It verifies
// that the stream creation is allowed and then forwards the request to the
// appropriate ForwardingAudioStreamFactory. It should be constructed and
// destructed on the UI thread, but will process mojo messages on the IO thread.
class CONTENT_EXPORT RenderFrameAudioOutputStreamFactory final {
 public:
  RenderFrameAudioOutputStreamFactory(
      RenderFrameHost* frame,
      media::AudioSystem* audio_system,
      MediaStreamManager* media_stream_manager,
      mojom::RendererAudioOutputStreamFactoryRequest request);

  ~RenderFrameAudioOutputStreamFactory();

  size_t CurrentNumberOfProvidersForTesting();

 private:
  class Core;
  std::unique_ptr<Core> core_;

  DISALLOW_COPY_AND_ASSIGN(RenderFrameAudioOutputStreamFactory);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_MEDIA_RENDER_FRAME_AUDIO_OUTPUT_STREAM_FACTORY_H_
