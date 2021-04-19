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

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/event_engine/sockaddr.h"
#include "src/core/lib/iomgr/event_engine/endpoint.h"
#include "src/core/lib/iomgr/event_engine/util.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/transport/error_utils.h"

namespace {
using ::grpc_event_engine::experimental::ChannelArgs;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::GrpcClosureToCallback;
using ::grpc_event_engine::experimental::SliceAllocator;
using ::grpc_event_engine::experimental::SliceAllocatorFactory;
}  // namespace

struct grpc_tcp_server {
  gpr_refcount refs;
  gpr_mu mu;
  std::unique_ptr<EventEngine::Listener> listener;
  std::shared_ptr<EventEngine> engine;
  grpc_closure_list shutdown_starting;
  grpc_resource_quota* resource_quota;
};

namespace {

// NOTE: the closure is already initialized, and does not take an Endpoint.
// See chttp2_connector:L74. Instead, the closure arg contains a ptr to the
// endpoint that iomgr is expected to populate. When gRPC eventually uses the
// EventEngine directly, closures will be replaced with EE callback types.
EventEngine::OnConnectCallback GrpcClosureToOnConnectCallback(
    grpc_closure* closure, grpc_event_engine_endpoint* grpc_endpoint_out) {
  return [&](absl::Status status, EventEngine::Endpoint* endpoint) {
    grpc_endpoint_out->endpoint = endpoint;
    // Reusing the existing URI conversion logic for now.
    grpc_resolved_address gaddr =
        CreateGRPCResolvedAddress(endpoint->GetLocalAddress());
    grpc_endpoint_out->local_address = grpc_sockaddr_to_uri(&gaddr);
    // TODO(hork): Do we need to add grpc_error to closure's error data?
    grpc_core::Closure::Run(DEBUG_LOCATION, closure,
                            absl_status_to_grpc_error(status));
  };
}

EventEngine::Listener::AcceptCallback GrpcClosureToAcceptCallback(
    grpc_closure* closure) {
  (void)closure;
  return [](absl::Status, EventEngine::Endpoint*) {};
}

/// Argument ownership stories:
/// * closure: ?
/// * endpoint: owned by caller
/// * interested_parties: owned by caller
/// * channel_args: owned by caller
/// * addr: owned by caller
/// * deadline: copied
static void tcp_connect(grpc_closure* on_connect, grpc_endpoint** endpoint,
                        grpc_pollset_set* interested_parties,
                        const grpc_channel_args* channel_args,
                        const grpc_resolved_address* addr,
                        grpc_millis deadline) {
  (void)interested_parties;
  // TODO(hork): peer_string needs to be set to ResolvedAddress name
  grpc_event_engine_endpoint* ee_endpoint =
      grpc_endpoint_create(channel_args, "UNIMPLEMENTED");
  *endpoint = &ee_endpoint->base;
  EventEngine::OnConnectCallback ee_on_connect =
      GrpcClosureToOnConnectCallback(on_connect, ee_endpoint);
  SliceAllocator sa(ee_endpoint->ru);
  EventEngine::ResolvedAddress ra(reinterpret_cast<const sockaddr*>(addr->addr),
                                  addr->len);
  absl::Time ee_deadline = grpc_core::ToAbslTime(
      grpc_millis_to_timespec(deadline, GPR_CLOCK_MONOTONIC));
  // TODO(hork): retrieve EventEngine from Endpoint or from channel_args
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  // TODO(hork): Convert channel_args to ChannelArgs
  ChannelArgs ca;
  if (!ee->Connect(ee_on_connect, ra, ca, std::move(sa), ee_deadline).ok()) {
    // Do nothing. The callback will be executed once with an error
  }
}

static grpc_error* tcp_server_create(grpc_closure* shutdown_complete,
                                     const grpc_channel_args* args,
                                     grpc_tcp_server** server) {
  // TODO(hork): retrieve EventEngine from Endpoint or from channel_args
  std::shared_ptr<EventEngine> ee = GetDefaultEventEngine();
  // TODO(hork): Convert channel_args to ChannelArgs
  ChannelArgs ca;
  grpc_resource_quota* rq = grpc_resource_quota_from_channel_args(args);
  if (rq == nullptr) {
    rq = grpc_resource_quota_create(nullptr);
  }
  // TODO(nnoble): The on_accept callback needs to be set later due to iomgr
  // API differences. We can solve this with an overloaded
  // Listener::Start(on_accept) method in a custom EE impl. This should not be
  // needed once iomgr goes away.
  absl::StatusOr<std::unique_ptr<EventEngine::Listener>> listener =
      ee->CreateListener(GrpcClosureToAcceptCallback(nullptr),
                         GrpcClosureToCallback(shutdown_complete), ca,
                         SliceAllocatorFactory(rq));
  if (!listener.ok()) {
    return absl_status_to_grpc_error(listener.status());
  }
  grpc_tcp_server* s =
      static_cast<grpc_tcp_server*>(gpr_zalloc(sizeof(grpc_tcp_server)));
  gpr_ref_init(&s->refs, 1);
  s->listener = std::move(*listener);
  s->engine = ee;
  s->shutdown_starting.head = nullptr;
  s->shutdown_starting.tail = nullptr;
  gpr_mu_init(&s->mu);
  s->resource_quota = rq;
  *server = s;
  return GRPC_ERROR_NONE;
}

static void tcp_server_start(grpc_tcp_server* server,
                             const std::vector<grpc_pollset*>* pollsets,
                             grpc_tcp_server_cb on_accept_cb, void* cb_arg) {
  (void)server;
  (void)pollsets;
  (void)on_accept_cb;
  (void)cb_arg;
  // TODO(nnoble): Needs something like:
  // LibuvEventEngine::Listener::Start(AcceptCallback)
}

static grpc_error* tcp_server_add_port(grpc_tcp_server* s,
                                       const grpc_resolved_address* addr,
                                       int* out_port) {
  EventEngine::ResolvedAddress ra(reinterpret_cast<const sockaddr*>(addr->addr),
                                  addr->len);
  auto port = s->listener->Bind(ra);
  if (!port.ok()) {
    return absl_status_to_grpc_error(port.status());
  }
  *out_port = *port;
  return GRPC_ERROR_NONE;
}

static grpc_core::TcpServerFdHandler* tcp_server_create_fd_handler(
    grpc_tcp_server* s) {
  (void)s;
  // TODO(hork): verify
  return nullptr;
}

static unsigned tcp_server_port_fd_count(grpc_tcp_server* /* s */,
                                         unsigned /* port_index */) {
  return 0;
}

static int tcp_server_port_fd(grpc_tcp_server* /* s */,
                              unsigned /* port_index */,
                              unsigned /* fd_index */) {
  // Note: only used internally
  return -1;
}

static grpc_tcp_server* tcp_server_ref(grpc_tcp_server* s) {
  gpr_ref_non_zero(&s->refs);
  return s;
}

static void tcp_server_shutdown_starting_add(grpc_tcp_server* s,
                                             grpc_closure* shutdown_starting) {
  gpr_mu_lock(&s->mu);
  grpc_closure_list_append(&s->shutdown_starting, shutdown_starting,
                           GRPC_ERROR_NONE);
  gpr_mu_unlock(&s->mu);
}

static void tcp_server_destroy(grpc_tcp_server* s) {
  gpr_mu_destroy(&s->mu);
  s->listener = nullptr;
  s->engine = nullptr;
  // TODO(hork): see if we can handle this in ~SliceAllocatorFactory
  grpc_resource_quota_unref_internal(s->resource_quota);
  gpr_free(s);
}

static void tcp_server_unref(grpc_tcp_server* s) {
  if (gpr_unref(&s->refs)) {
    gpr_mu_lock(&s->mu);
    grpc_core::ExecCtx exec_ctx;
    grpc_core::ExecCtx::RunList(DEBUG_LOCATION, &s->shutdown_starting);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_unlock(&s->mu);
    tcp_server_destroy(s);
  }
}

// No-op, all are handled on listener unref
static void tcp_server_shutdown_listeners(grpc_tcp_server* /* s */) {}

}  // namespace

grpc_tcp_client_vtable grpc_event_engine_tcp_client_vtable = {tcp_connect};
grpc_tcp_server_vtable grpc_event_engine_tcp_server_vtable = {
    tcp_server_create,        tcp_server_start,
    tcp_server_add_port,      tcp_server_create_fd_handler,
    tcp_server_port_fd_count, tcp_server_port_fd,
    tcp_server_ref,           tcp_server_shutdown_starting_add,
    tcp_server_unref,         tcp_server_shutdown_listeners};

#endif
