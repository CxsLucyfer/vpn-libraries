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

#ifndef PRIVACY_NET_KRYPTON_AUTH_AND_SIGN_REQUEST_H_
#define PRIVACY_NET_KRYPTON_AUTH_AND_SIGN_REQUEST_H_

#include <optional>
#include <string>

#include "privacy/net/attestation/proto/attestation.proto.h"
#include "privacy/net/krypton/proto/http_fetcher.proto.h"
#include "privacy/net/zinc/rpc/zinc.proto.h"
#include "third_party/absl/strings/string_view.h"
#include "third_party/absl/types/optional.h"
#include "third_party/json/include/nlohmann/json_fwd.hpp"

namespace privacy {
namespace krypton {

// Requesting public key from the Zinc server.
class PublicKeyRequest {
 public:
  explicit PublicKeyRequest(bool request_nonce,
                            std::optional<std::string> api_key)
      : request_nonce_(request_nonce), api_key_(api_key) {}
  ~PublicKeyRequest() = default;
  HttpRequest EncodeToProto() const;

 private:
  HttpRequest http_request_;
  const bool request_nonce_;
  std::optional<std::string> api_key_;
};

// A class for constructing http AuthAndSignRequest to Zinc.
class AuthAndSignRequest {
 public:
  AuthAndSignRequest(
      absl::string_view auth_token, absl::string_view service_type,
      absl::string_view selected_session_manager_ip,
      std::optional<std::string> blinded_token,
      std::optional<std::string> public_key_hash,
      std::optional<privacy::ppn::AttestationData> attestation_data,
      bool attach_oauth_as_header);
  ~AuthAndSignRequest() = default;

  std::optional<HttpRequest> EncodeToProto() const;

 private:
  nlohmann::json BuildBodyJson() const;
  ppn::AuthAndSignRequest BuildBodyProto() const;

  const std::string auth_token_;
  const std::string service_type_;
  const std::string selected_session_manager_ip_;
  std::optional<std::string> blinded_token_;
  std::optional<std::string> public_key_hash_;
  std::optional<privacy::ppn::AttestationData> attestation_data_;
  bool attach_oauth_as_header_;
};

}  // namespace krypton
}  // namespace privacy
#endif  // PRIVACY_NET_KRYPTON_AUTH_AND_SIGN_REQUEST_H_
