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

#include <grpc/support/port_platform.h>

#include <functional>

#include <grpc/event_engine/event_engine.h>
#include "absl/status/status.h"

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/event_engine/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_event_engine {
namespace experimental {

std::shared_ptr<EventEngine> GetDefaultEventEngine() {
  // TODO(nnoble): instantiate a singleton LibuvEventEngine
  return nullptr;
}

// grpc_closure to std::function conversions for an EventEngine-based iomgr
EventEngine::Callback GrpcClosureToCallback(grpc_closure* closure) {
  return [&](absl::Status status) {
    // TODO(hork): Do we need to add grpc_error to closure's error data?
    // if (!status.ok()) {
    //   closure->error_data.error = grpc_error_add_child(
    //       closure->error_data.error,
    //       GRPC_ERROR_CREATE_FROM_COPIED_STRING(status.ToString().c_str()));
    // }
    grpc_core::Closure::Run(DEBUG_LOCATION, closure,
                            absl_status_to_grpc_error(status));
  };
}

grpc_resolved_address CreateGRPCResolvedAddress(
    const EventEngine::ResolvedAddress* ra) {
  grpc_resolved_address grpc_addr;
  memcpy(grpc_addr.addr, ra->address(), ra->size());
  grpc_addr.len = ra->size();
  return grpc_addr;
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_EVENT_ENGINE_TEST
