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

#include "privacy/net/krypton/jni/vpn_service.h"

#include <jni.h>
#include <jni_md.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/logging.h"
#include "google/protobuf/duration.proto.h"
#include "privacy/net/krypton/datapath/android_ipsec/datagram_socket.h"
#include "privacy/net/krypton/datapath/android_ipsec/ipsec_datapath.h"
#include "privacy/net/krypton/datapath/android_ipsec/ipsec_tunnel.h"

#include "privacy/net/krypton/jni/jni_cache.h"
#include "privacy/net/krypton/proto/network_info.proto.h"
#include "privacy/net/krypton/proto/tun_fd_data.proto.h"
#include "privacy/net/krypton/utils/status.h"
#include "privacy/net/krypton/utils/time_util.h"
#include "third_party/absl/status/status.h"
#include "third_party/absl/status/statusor.h"
#include "third_party/absl/strings/str_cat.h"
#include "third_party/absl/time/time.h"

namespace privacy {
namespace krypton {
namespace jni {

constexpr absl::Duration kDefaultIpv4KeepaliveInterval = absl::Seconds(20);
constexpr absl::Duration kDefaultIpv6KeepaliveInterval = absl::Hours(1);

DatapathInterface* VpnService::BuildDatapath(const KryptonConfig& config,
                                             utils::LooperThread* looper,
                                             TimerManager* /*timer_manager*/) {
  keepalive_interval_ipv4_ = kDefaultIpv4KeepaliveInterval;
  if (config.has_ipv4_keepalive_interval()) {
    auto keepalive = utils::DurationFromProto(config.ipv4_keepalive_interval());
    if (keepalive.ok()) {
      keepalive_interval_ipv4_ = *keepalive;
    } else {
      LOG(ERROR) << "Failed to convert IPv4 keepalive interval: "
                 << keepalive.status();
    }
  }

  keepalive_interval_ipv6_ = kDefaultIpv6KeepaliveInterval;
  if (config.has_ipv6_keepalive_interval()) {
    auto keepalive = utils::DurationFromProto(config.ipv6_keepalive_interval());
    if (keepalive.ok()) {
      keepalive_interval_ipv6_ = *keepalive;
    } else {
      LOG(ERROR) << "Failed to convert IPv6 keepalive interval: "
                 << keepalive.status();
    }
  }

  if (config.datapath_protocol() == KryptonConfig::IPSEC) {
    return new datapath::android::IpSecDatapath(config, looper, this);
  }
}

absl::Status VpnService::CreateTunnel(const TunFdData& tun_fd_data) {
  LOG(INFO) << "Requesting TUN fd from Java with tun data "
            << tun_fd_data.DebugString();

  auto jni_cache = JniCache::Get();
  auto env = jni_cache->GetJavaEnv();
  if (!env) {
    LOG(ERROR) << "Cannot find JavaEnv to request TUN fd";
    return absl::Status(absl::StatusCode::kInternal, "Unable to get Java Env");
  }

  std::string tun_fd_bytes;
  tun_fd_data.SerializeToString(&tun_fd_bytes);

  jint fd = env.value()->CallIntMethod(
      krypton_instance_->get(), jni_cache->GetKryptonCreateTunFdMethod(),
      JavaByteArray(env.value(), tun_fd_bytes).get());

  if (fd < 0) {
    return absl::Status(absl::StatusCode::kUnavailable,
                        absl::StrCat("Unable to create TUN fd: ", fd));
  }

  absl::MutexLock l(&mutex_);
  if (tunnel_ != nullptr) {
    LOG(WARNING) << "Old tunnel was still open. Closing now.";
    PPN_LOG_IF_ERROR(tunnel_->Close());
    tunnel_.reset();
    tunnel_fd_ = -1;
  }

  auto tunnel = datapath::android::IpSecTunnel::Create(fd);
  if (!tunnel.ok()) {
    close(tunnel_fd_);
    tunnel_fd_ = -1;
    return tunnel.status();
  }
  tunnel_ = *std::move(tunnel);
  tunnel_fd_ = fd;
  return absl::OkStatus();
}

datapath::android::TunnelInterface* VpnService::GetTunnel() {
  absl::MutexLock l(&mutex_);
  return tunnel_.get();
}

absl::StatusOr<int> VpnService::GetTunnelFd() {
  absl::MutexLock l(&mutex_);
  if (tunnel_fd_ < 0) {
    return absl::InternalError("Tunnel is closed");
  }
  return tunnel_fd_;
}

void VpnService::CloseTunnel() {
  absl::MutexLock l(&mutex_);
  if (tunnel_ == nullptr) {
    return;
  }
  PPN_LOG_IF_ERROR(tunnel_->Close());
  tunnel_.reset();
  tunnel_fd_ = -1;
}

absl::StatusOr<int> VpnService::CreateProtectedNetworkSocket(
    const NetworkInfo& network_info) {
  LOG(INFO) << "Requesting network fd from Java with network info "
            << network_info.DebugString();

  auto jni_cache = JniCache::Get();
  auto env = jni_cache->GetJavaEnv();
  if (!env) {
    LOG(ERROR) << "Cannot find JavaEnv to request network fd";
    return absl::Status(absl::StatusCode::kInternal, "Unable to get Java Env");
  }

  std::string network_info_bytes;
  network_info.SerializeToString(&network_info_bytes);

  jint fd = env.value()->CallIntMethod(
      krypton_instance_->get(), jni_cache->GetKryptonCreateNetworkFdMethod(),
      JavaByteArray(env.value(), network_info_bytes).get());

  if (fd < 0) {
    return absl::Status(absl::StatusCode::kUnavailable,
                        absl::StrCat("Unable to create network fd: ", fd));
  }

  return fd;
}

absl::StatusOr<int> VpnService::CreateProtectedTcpSocket(
    const NetworkInfo& network_info) {
  LOG(INFO) << "Requesting TCP fd from Java with network info "
            << network_info.DebugString();

  auto jni_cache = JniCache::Get();
  auto env = jni_cache->GetJavaEnv();
  if (!env) {
    LOG(ERROR) << "Cannot find JavaEnv to request TCP fd";
    return absl::Status(absl::StatusCode::kInternal, "Unable to get Java Env");
  }

  std::string network_info_bytes;
  network_info.SerializeToString(&network_info_bytes);

  jint fd = env.value()->CallIntMethod(
      krypton_instance_->get(), jni_cache->GetKryptonCreateTcpFdMethod(),
      JavaByteArray(env.value(), network_info_bytes).get());

  if (fd < 0) {
    return absl::Status(absl::StatusCode::kUnavailable,
                        absl::StrCat("Unable to create TCP fd: ", fd));
  }

  return fd;
}

absl::StatusOr<std::unique_ptr<datapath::android::IpSecSocketInterface>>
VpnService::CreateProtectedNetworkSocket(const NetworkInfo& network_info,
                                         const Endpoint& endpoint) {
  PPN_ASSIGN_OR_RETURN(auto fd, CreateProtectedNetworkSocket(network_info));
  PPN_ASSIGN_OR_RETURN(auto socket,
                       datapath::android::DatagramSocket::Create(fd));
  auto status = socket->Connect(endpoint);

  {
    absl::MutexLock l(&mutex_);
    if (endpoint.ip_protocol() == IPProtocol::kIPv4) {
      tunnel_->SetKeepaliveInterval(keepalive_interval_ipv4_);
    } else {
      tunnel_->SetKeepaliveInterval(keepalive_interval_ipv6_);
    }
  }

  if (!status.ok()) {
    LOG(ERROR) << "Socket connect failed: " << status;
    PPN_LOG_IF_ERROR(socket->Close());
    return status;
  }
  return socket;
}

absl::Status VpnService::ConfigureIpSec(const IpSecTransformParams& params) {
  LOG(INFO) << "Configuring IPSec for fd: " << params.network_fd();

  auto jni_cache = JniCache::Get();
  auto env = jni_cache->GetJavaEnv();
  if (!env) {
    LOG(ERROR) << "Cannot find JavaEnv to configure IPSec.";
    return absl::Status(absl::StatusCode::kInternal, "Unable to get Java Env");
  }

  std::string transform_params_bytes;
  params.SerializeToString(&transform_params_bytes);
  jboolean status = env.value()->CallBooleanMethod(
      krypton_instance_->get(), jni_cache->GetKryptonConfigureIpSecMethod(),
      JavaByteArray(env.value(), transform_params_bytes).get());
  if (static_cast<bool>(status)) {
    return absl::OkStatus();
  }
  return absl::Status(
      absl::StatusCode::kUnavailable,
      absl::StrCat("Error encountered when applying transform to fd: ",
                   params.network_fd()));
}

void VpnService::DisableKeepalive() {
  absl::MutexLock l(&mutex_);
  if (tunnel_ == nullptr) {
    return;
  }
  tunnel_->SetKeepaliveInterval(absl::ZeroDuration());
}

}  // namespace jni
}  // namespace krypton
}  // namespace privacy
