// Copyright 2022 Google LLC
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

#include "privacy/net/krypton/datapath/android_ipsec/ipsec_tunnel.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "privacy/net/krypton/utils/status.h"
#include "third_party/absl/status/status.h"
#include "third_party/absl/strings/substitute.h"
#include "third_party/absl/time/time.h"

namespace privacy {
namespace krypton {
namespace datapath {
namespace android {

namespace {
constexpr int kMaxPacketSize = 4096;
}  // namespace

absl::StatusOr<std::unique_ptr<IpSecTunnel>> IpSecTunnel::Create(
    int tunnel_fd) {
  auto tunnel = std::unique_ptr<IpSecTunnel>(new IpSecTunnel(tunnel_fd));
  PPN_RETURN_IF_ERROR(tunnel->Init());
  return tunnel;
}

IpSecTunnel::IpSecTunnel(int tunnel_fd)
    : tunnel_fd_(tunnel_fd), keepalive_interval_millis_(-1) {}

IpSecTunnel::~IpSecTunnel() {
  if (tunnel_fd_ >= 0) {
    PPN_LOG_IF_ERROR(Close());
  }

  PPN_LOG_IF_ERROR(events_helper_.RemoveFile(close_event_.fd()));
}

absl::Status IpSecTunnel::Close() {
  int fd = tunnel_fd_.exchange(-1);
  if (fd < 0) {
    LOG(WARNING) << "Attempted to close tunnel that was already closed.";
    return absl::OkStatus();
  }
  LOG(INFO) << "Closing tunnel FD=" << fd;
  PPN_LOG_IF_ERROR(events_helper_.RemoveFile(fd));
  close(fd);
  PPN_LOG_IF_ERROR(close_event_.Notify(1));
  return absl::OkStatus();
}

absl::Status IpSecTunnel::CancelReadPackets() { return close_event_.Notify(1); }

absl::StatusOr<std::vector<Packet>> IpSecTunnel::ReadPackets() {
  if (tunnel_fd_ < 0) {
    return absl::InternalError("Attempted to read on a closed fd.");
  }
  EventsHelper::Event event;
  int num_events;
  auto status =
      events_helper_.Wait(&event, 1, keepalive_interval_millis_, &num_events);
  int fd = tunnel_fd_;
  if (!status.ok()) {
    return absl::InternalError(absl::Substitute(
        "Failed to listen for events on fd $0: $1", fd, strerror(errno)));
  }

  // Send a keepalive packet if we time out
  if (num_events == 0) {
    static const char* buffer = "\xFF";
    std::vector<Packet> packets;
    packets.emplace_back(buffer, 1, IPProtocol::kUnknown, []() {});

    return packets;
  }
  int notified_fd = datapath::android::EventsHelper::FileFromEvent(event);
  if (notified_fd == close_event_.fd()) {
    // An empty vector without an error status should be interpreted as a close
    return std::vector<Packet>();
  }
  if (datapath::android::EventsHelper::FileHasError(event)) {
    return absl::InternalError(absl::Substitute("Read on fd $0 failed.", fd));
  }
  if (datapath::android::EventsHelper::FileCanRead(event)) {
    if (fd < 0) {
      return absl::InternalError("Attempted to read on a closed fd.");
    }
    // TODO: Do reads in batches.

    // TODO: Don't allocate new memory for every packet.
    char* buffer = new char[kMaxPacketSize];

    int read_bytes;
    do {
      read_bytes = read(fd, buffer, kMaxPacketSize);
    } while (read_bytes == -1 && errno == EINTR);

    if (read_bytes <= 0) {
      delete[] buffer;
      return absl::AbortedError(
          absl::Substitute("Reading from FD $0: $1", fd, strerror(errno)));
    }

    std::vector<Packet> packets;
    packets.emplace_back(buffer, read_bytes, IPProtocol::kUnknown,
                         [buffer]() { delete[] buffer; });

    return packets;
  }

  // Should never get here
  return absl::InternalError("Unexpected event occurred.");
}

absl::Status IpSecTunnel::WritePackets(std::vector<Packet> packets) {
  int fd = tunnel_fd_;
  if (fd < 0) {
    return absl::InternalError("Attempted to write to a closed fd.");
  }
  for (const auto& packet : packets) {
    int write_bytes;
    do {
      write_bytes = write(fd, packet.data().data(), packet.data().size());
    } while (write_bytes == -1 && errno == EINTR);
    if (write_bytes != packet.data().size()) {
      return absl::InternalError(
          absl::StrCat("Error writing to FD=", fd, ": ", strerror(errno)));
    }
  }
  return absl::OkStatus();
}

void IpSecTunnel::SetKeepaliveInterval(absl::Duration keepalive_interval) {
  keepalive_interval_millis_ = absl::ToInt64Milliseconds(keepalive_interval);
  if (keepalive_interval_millis_ <= 0) {
    keepalive_interval_millis_ = -1;
  }
}

absl::Duration IpSecTunnel::GetKeepaliveInterval() {
  return (keepalive_interval_millis_ < 0)
             ? absl::ZeroDuration()
             : absl::Milliseconds(keepalive_interval_millis_);
}

bool IpSecTunnel::IsKeepaliveEnabled() {
  return (keepalive_interval_millis_ != -1);
}

absl::Status IpSecTunnel::Init() {
  int fd = tunnel_fd_;

  auto status = events_helper_.AddFile(fd, EventsHelper::EventReadableFlags());
  if (status.ok()) {
    status = events_helper_.AddFile(close_event_.fd(),
                                    EventsHelper::EventReadableFlags());
    if (!status.ok()) {
      LOG(ERROR) << "Failed to add close event for fd " << fd
                 << " to EventsHelper: " << status;
    }
  } else {
    LOG(ERROR) << "Failed to add fd " << fd << " to EventsHelper: " << status;
  }

  if (!status.ok()) {
    PPN_LOG_IF_ERROR(Close());
    return status;
  }

  return absl::OkStatus();
}

}  // namespace android
}  // namespace datapath
}  // namespace krypton
}  // namespace privacy
