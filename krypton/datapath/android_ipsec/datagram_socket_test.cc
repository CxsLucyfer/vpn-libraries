/*
 * Copyright (C) 2022 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "privacy/net/krypton/datapath/android_ipsec/datagram_socket.h"

#include <memory>
#include <utility>
#include <vector>

#include "privacy/net/krypton/datapath/android_ipsec/simple_udp_server.h"
#include "privacy/net/krypton/endpoint.h"
#include "privacy/net/krypton/utils/looper.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"
#include "third_party/absl/status/status.h"

namespace privacy {
namespace krypton {
namespace datapath {
namespace android {
namespace {

using ::testing::status::StatusIs;

class DatagramSocketTest : public ::testing::Test {
 public:
  void SetUp() override {}
  void TearDown() override {}

 protected:
  static absl::StatusOr<std::unique_ptr<DatagramSocket>> CreateSocket() {
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) {
      return absl::InternalError("Unable to create socket");
    }
    return DatagramSocket::Create(fd);
  }

  static absl::StatusOr<Endpoint> GetLocalhost(int port) {
    return GetEndpointFromHostPort(absl::StrFormat("[::1]:%d", port));
  }
};

TEST_F(DatagramSocketTest, BasicReadAndWrite) {
  testing::SimpleUdpServer server;

  // Connect to the server.
  ASSERT_OK_AND_ASSIGN(auto sock, CreateSocket());
  ASSERT_OK_AND_ASSIGN(auto localhost, GetLocalhost(server.port()));
  ASSERT_OK(sock->Connect(localhost));

  // Send a packet to the server, to establish the client port.
  std::vector<Packet> packets;
  packets.emplace_back("foo", 3, IPProtocol::kIPv6, []() {});
  ASSERT_OK(sock->WritePackets(std::move(packets)));

  // Verify the server received the packet.
  ASSERT_OK_AND_ASSIGN((auto [port, data]), server.ReceivePacket());
  EXPECT_EQ("foo", data);

  // Send a packet back to the client.
  server.SendSamplePacket(port, "bar");

  // Read the packet on the client.
  ASSERT_OK_AND_ASSIGN(auto recv_packets, sock->ReadPackets());
  ASSERT_EQ(1, recv_packets.size());
  EXPECT_EQ("bar", recv_packets[0].data());

  // Close the socket.
  ASSERT_OK(sock->Close());

  // Make sure reading from the socket immediately returns.
  LOG(INFO) << "Trying to read packets after close.";
  ASSERT_THAT(sock->ReadPackets(), StatusIs(absl::StatusCode::kInternal));
}

TEST_F(DatagramSocketTest, CloseBeforeRead) {
  testing::SimpleUdpServer server;

  // Connect to the server.
  ASSERT_OK_AND_ASSIGN(auto sock, CreateSocket());
  ASSERT_OK_AND_ASSIGN(auto localhost, GetLocalhost(server.port()));
  ASSERT_OK(sock->Connect(localhost));

  // Send a packet to the server, to establish the client port.
  std::vector<Packet> packets;
  packets.emplace_back("foo", 3, IPProtocol::kIPv6, []() {});
  ASSERT_OK(sock->WritePackets(std::move(packets)));

  // Verify the server received the packet.
  ASSERT_OK_AND_ASSIGN((auto [port, data]), server.ReceivePacket());
  EXPECT_EQ("foo", data);

  // Send a packet back to the client.
  server.SendSamplePacket(port, "bar");

  // Close the socket.
  ASSERT_OK(sock->Close());

  // The "bar" packet is dropped, because the FD was closed before it was read.

  // Make sure reading from the socket immediately returns.
  LOG(INFO) << "Trying to read packets after close.";
  ASSERT_THAT(sock->ReadPackets(), StatusIs(absl::StatusCode::kInternal));
}

TEST_F(DatagramSocketTest, ReadBeforeWrite) {
  testing::SimpleUdpServer server;

  // Connect to the server.
  ASSERT_OK_AND_ASSIGN(auto sock, CreateSocket());
  ASSERT_OK_AND_ASSIGN(auto localhost, GetLocalhost(server.port()));
  ASSERT_OK(sock->Connect(localhost));

  // Send a packet to the server, to establish the client port.
  std::vector<Packet> packets;
  packets.emplace_back("foo", 3, IPProtocol::kIPv6, []() {});
  ASSERT_OK(sock->WritePackets(std::move(packets)));

  // Verify the server received the packet.
  ASSERT_OK_AND_ASSIGN((auto [port, data]), server.ReceivePacket());
  EXPECT_EQ("foo", data);

  krypton::utils::LooperThread looper("ReadBeforeWrite Writer");
  looper.Post([&server, port = port]() {
    // Wait a second, so that the read can start.
    sleep(1);

    // Send a packet back to the client.
    server.SendSamplePacket(port, "bar");
  });

  // Read the packet on the client.
  ASSERT_OK_AND_ASSIGN(auto recv_packets, sock->ReadPackets());
  ASSERT_EQ(1, recv_packets.size());
  EXPECT_EQ("bar", recv_packets[0].data());

  // Close the socket.
  ASSERT_OK(sock->Close());

  // Make sure reading from the socket immediately returns.
  LOG(INFO) << "Trying to read packets after close.";
  ASSERT_THAT(sock->ReadPackets(), StatusIs(absl::StatusCode::kInternal));
}

TEST_F(DatagramSocketTest, ReadBeforeClose) {
  testing::SimpleUdpServer server;

  // Connect to the server.
  ASSERT_OK_AND_ASSIGN(auto sock, CreateSocket());
  ASSERT_OK_AND_ASSIGN(auto localhost, GetLocalhost(server.port()));
  ASSERT_OK(sock->Connect(localhost));

  // Send a packet to the server, to establish the client port.
  std::vector<Packet> packets;
  packets.emplace_back("foo", 3, IPProtocol::kIPv6, []() {});
  ASSERT_OK(sock->WritePackets(std::move(packets)));

  // Verify the server received the packet.
  ASSERT_OK_AND_ASSIGN((auto [port, data]), server.ReceivePacket());
  EXPECT_EQ("foo", data);

  krypton::utils::LooperThread looper("ReadBeforeWrite Writer");
  looper.Post([&sock]() {
    // Wait a second, so that the read can start.
    sleep(1);

    // Close the socket.
    ASSERT_OK(sock->Close());
  });

  // Make sure reading from the socket immediately returns.
  LOG(INFO) << "Trying to read packets after close.";
  ASSERT_OK_AND_ASSIGN(auto read_packets, sock->ReadPackets());
  ASSERT_TRUE(read_packets.empty());
}

TEST_F(DatagramSocketTest, WriteAfterClose) {
  testing::SimpleUdpServer server;

  // Connect to the server.
  ASSERT_OK_AND_ASSIGN(auto sock, CreateSocket());
  ASSERT_OK_AND_ASSIGN(auto localhost, GetLocalhost(server.port()));
  ASSERT_OK(sock->Connect(localhost));

  // Close the socket.
  ASSERT_OK(sock->Close());

  // Verify that writing to the FD now fails.
  std::vector<Packet> packets;
  packets.emplace_back("foo", 3, IPProtocol::kIPv6, []() {});
  ASSERT_THAT(sock->WritePackets(std::move(packets)),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(DatagramSocketTest, ReadAfterShutdown) {
  // Create the socket manually to get the fd
  int fd = socket(AF_INET6, SOCK_DGRAM, 0);
  ASSERT_GE(fd, 0);
  ASSERT_OK_AND_ASSIGN(auto sock, DatagramSocket::Create(fd));

  shutdown(fd, SHUT_RDWR);

  ASSERT_THAT(sock->ReadPackets(), StatusIs(absl::StatusCode::kAborted));

  ASSERT_OK(sock->Close());
}

TEST_F(DatagramSocketTest, CloseAfterClose) {
  // Create the socket.
  ASSERT_OK_AND_ASSIGN(auto fd, CreateSocket());

  // Close the fd twice.
  ASSERT_OK(fd->Close());
  ASSERT_OK(fd->Close());
}

}  // namespace
}  // namespace android
}  // namespace datapath
}  // namespace krypton
}  // namespace privacy
