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

#ifndef PRIVACY_NET_KRYPTON_ADD_EGRESS_REQUEST_H_
#define PRIVACY_NET_KRYPTON_ADD_EGRESS_REQUEST_H_

#include <optional>
#include <string>

#include "privacy/net/brass/rpc/brass.proto.h"
#include "privacy/net/krypton/crypto/session_crypto.h"
#include "privacy/net/krypton/proto/http_fetcher.proto.h"
#include "privacy/net/krypton/proto/krypton_config.proto.h"
#include "third_party/absl/time/time.h"
#include "third_party/json/include/nlohmann/json_fwd.hpp"

namespace privacy {
namespace krypton {

// Builds AddEgressRequest to fetch the public keys of the egress nodes.
// Proto definition is brass.proto
class AddEgressRequest {
 public:
  enum class RequestDestination { kBrass, kBeryllium };

  explicit AddEgressRequest(
      std::optional<std::string> api_key,
      RequestDestination request_destination = RequestDestination::kBrass)
      : api_key_(api_key), request_destination_(request_destination) {}
  ~AddEgressRequest() = default;

  // Parameters needed for PpnDataplane.
  struct PpnDataplaneRequestParams {
   public:
    const crypto::SessionCrypto* crypto;  // Not owned.
    std::string copper_control_plane_address;
    ppn::PpnDataplaneRequest::CryptoSuite suite;
    KryptonConfig::DatapathProtocol dataplane_protocol;
    bool is_rekey;
    std::string signature;
    uint32_t uplink_spi;
    // Raw text that was sent to Zinc also needs to be sent to Brass.
    std::string blind_message;
    // This is the unblinded signature after receiving the blinding signature
    // from Zinc that needs to be sent to Brass.
    std::string unblinded_token_signature;
    // Whether to enable dynamic mtu on the backend dataplane.
    bool dynamic_mtu_enabled = false;

    // Brass Only
    // This is the APN type from Zinc and used to decide APN in bridge-proxy.
    std::string apn_type;
    // This is the region overriding token and signature for sending to Brass.
    std::string region_token_and_signature;

    // Beryllium Only
    int64_t signing_key_version;
    // This is the public metadata
    std::string country;
    std::string city_geo_id;
    std::string service_type;
    absl::Time expiration;
  };

  HttpRequest EncodeToProtoForPpn(const PpnDataplaneRequestParams& params);

 private:
  nlohmann::json BuildBodyJson(const PpnDataplaneRequestParams& params);
  std::optional<std::string> api_key_;
  RequestDestination request_destination_;
};
}  // namespace krypton
}  // namespace privacy

#endif  // PRIVACY_NET_KRYPTON_ADD_EGRESS_REQUEST_H_
