// Copyright 2021 The gRPC Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#if defined(GRPC_EVENT_ENGINE_TEST)

#include "absl/strings/string_view.h"
#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resource_quota.h"

struct grpc_event_engine_endpoint {
  grpc_endpoint base;
  grpc_event_engine::experimental::EventEngine::Endpoint* endpoint;
  absl::string_view peer_string;
  grpc_resource_user* ru;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine;
};

grpc_event_engine_endpoint* grpc_endpoint_create(
    const grpc_channel_args* channel_args, absl::string_view peer_string);

#endif  // GRPC_EVENT_ENGINE_TEST
