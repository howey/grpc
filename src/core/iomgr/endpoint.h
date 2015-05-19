/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_INTERNAL_CORE_IOMGR_ENDPOINT_H
#define GRPC_INTERNAL_CORE_IOMGR_ENDPOINT_H

#include "src/core/iomgr/pollset.h"
#include <grpc/support/slice.h>
#include <grpc/support/slice_buffer.h>
#include <grpc/support/time.h>

/* An endpoint caps a streaming channel between two communicating processes.
   Examples may be: a tcp socket, <stdin+stdout>, or some shared memory. */

typedef struct grpc_endpoint grpc_endpoint;
typedef struct grpc_endpoint_vtable grpc_endpoint_vtable;

typedef enum grpc_endpoint_op_status {
  GRPC_ENDPOINT_OP_DONE,    /* completed immediately successfully */
  GRPC_ENDPOINT_OP_PENDING, /* cb will be called when completed */
  GRPC_ENDPOINT_OP_ERROR    /* operation errored out */
} grpc_endpoint_op_status;

typedef void (*grpc_endpoint_op_cb)(void *user_data, 
                                    grpc_endpoint_op_status status);

struct grpc_endpoint_vtable {
  grpc_endpoint_op_status(*read)(grpc_endpoint *ep,
                                 gpr_slice_buffer *slices,
                                 grpc_endpoint_op_cb cb,
                                 void *user_data);
  grpc_endpoint_op_status(*write)(grpc_endpoint *ep,
                                  gpr_slice_buffer *slices,
                                  grpc_endpoint_op_cb cb,
                                  void *user_data);
  void (*add_to_pollset)(grpc_endpoint *ep, grpc_pollset *pollset);
  void (*shutdown)(grpc_endpoint *ep);
  void (*destroy)(grpc_endpoint *ep);
};

/* Reads slices from the socket.

   If the read could be done immediately, the function will return
   GPR_ENDPOINT_OP_DONE. The callback won't be called in that case. If no
   data was available at the time of the call, an asynchronous read is
   scheduled, no slice will be stored in the slices buffer, the function
   will return GRPC_ENDPOINT_OP_PENDING, and the callback will be called
   later upon completion. */
grpc_endpoint_op_status grpc_endpoint_read(grpc_endpoint *ep,
                                           gpr_slice_buffer *slices,
                                           grpc_endpoint_op_cb cb,
                                           void *user_data)
                                           GRPC_MUST_USE_RESULT;

/* Write slices out to the socket.

   If the connection is ready for more data after the end of the call, it
   returns GRPC_ENDPOINT_OP_DONE.
   Otherwise it returns GRPC_ENDPOINT_OP_PENDING and calls cb when the
   connection is ready for more data. */
grpc_endpoint_op_status grpc_endpoint_write(grpc_endpoint *ep,
                                            gpr_slice_buffer *slices,
                                            grpc_endpoint_op_cb cb,
                                            void *user_data)
                                            GRPC_MUST_USE_RESULT;

/* Causes any pending read/write callbacks to run immediately with
   GRPC_ENDPOINT_OP_ERROR status */
void grpc_endpoint_shutdown(grpc_endpoint *ep);
void grpc_endpoint_destroy(grpc_endpoint *ep);

/* Add an endpoint to a pollset, so that when the pollset is polled, events from
   this endpoint are considered */
void grpc_endpoint_add_to_pollset(grpc_endpoint *ep, grpc_pollset *pollset);

struct grpc_endpoint {
  const grpc_endpoint_vtable *vtable;
};

#endif  /* GRPC_INTERNAL_CORE_IOMGR_ENDPOINT_H */
