// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PRIVACY_NET_KRYPTON_DATAPATH_INTERFACE_H_
#define PRIVACY_NET_KRYPTON_DATAPATH_INTERFACE_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "privacy/net/brass/rpc/brass.proto.h"
#include "privacy/net/krypton/add_egress_response.h"
#include "privacy/net/krypton/endpoint.h"
#include "privacy/net/krypton/proto/debug_info.proto.h"
#include "privacy/net/krypton/proto/network_info.proto.h"
#include "privacy/net/krypton/proto/network_type.proto.h"
#include "third_party/absl/status/status.h"

namespace privacy {
namespace krypton {

// Interface for datapath management. This is valid only for a single session,
// for recreating the session, callers need to create another instance.
class DatapathInterface {
 public:
  DatapathInterface() = default;
  virtual ~DatapathInterface() = default;

  // Notification for Datapath state changes.
  class NotificationInterface {
   public:
    NotificationInterface() = default;
    virtual ~NotificationInterface() = default;

    // Notifications.
    // Datapath is established.
    virtual void DatapathEstablished() = 0;
    // Datapath failed with status.
    virtual void DatapathFailed(const absl::Status&) = 0;
    // Permanent Datapath failure
    virtual void DatapathPermanentFailure(const absl::Status&) = 0;
    // Datapath needs rekey
    virtual void DoRekey() = 0;
  };

  // Initialize the data path.  Start takes two parameters, the transform params
  // and the egress response.
  virtual absl::Status Start(const AddEgressResponse& egress_response,
                             const TransformParams& params) = 0;

  // Stop the datapath.  Callers need to clear the object and recreate after
  // |stop|.
  virtual void Stop() = 0;

  // Register for datapath status changes.
  virtual void RegisterNotificationHandler(
      DatapathInterface::NotificationInterface* notification) {
    notification_ = notification;
  }

  // nullopt for NetworkInfo indicates there are no active networks.
  // Tunnel ownership is borrowed from the caller, who retains
  // ownership, but guarantees them to stay alive for the life of the datapath.
  virtual absl::Status SwitchNetwork(uint32_t session_id,
                                     const Endpoint& endpoint,
                                     std::optional<NetworkInfo> network_info,
                                     int counter) = 0;

  virtual absl::Status SetKeyMaterials(const TransformParams& params) = 0;

  virtual void GetDebugInfo(DatapathDebugInfo* debug_info) {}

 protected:
  NotificationInterface* notification_ = nullptr;  // Not Owned
};

}  // namespace krypton
}  // namespace privacy

#endif  // PRIVACY_NET_KRYPTON_DATAPATH_INTERFACE_H_
