// Copyright 2023 Google LLC
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

#ifndef PRIVACY_NET_KRYPTON_DATAPATH_ANDROID_IPSEC_MTU_TRACKER_H_
#define PRIVACY_NET_KRYPTON_DATAPATH_ANDROID_IPSEC_MTU_TRACKER_H_

#include "privacy/net/krypton/datapath/android_ipsec/mtu_tracker_interface.h"
#include "privacy/net/krypton/pal/packet.h"

namespace privacy {
namespace krypton {
namespace datapath {
namespace android {

class MtuTracker : public MtuTrackerInterface {
 public:
  MtuTracker();
  explicit MtuTracker(int initial_path_mtu);

  void UpdateMtu(int path_mtu) override;

  void UpdateDestIpProtocol(IPProtocol dest_ip_protocol) override;

  int GetPathMtu() const override;

  int GetTunnelMtu() const override;

 private:
  IPProtocol dest_ip_protocol_;
  int tunnel_overhead_;
  int path_mtu_;
  int tunnel_mtu_;
};

}  // namespace android
}  // namespace datapath
}  // namespace krypton
}  // namespace privacy

#endif  // PRIVACY_NET_KRYPTON_DATAPATH_ANDROID_IPSEC_MTU_TRACKER_H_
