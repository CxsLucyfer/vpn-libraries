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

#include "privacy/net/krypton/krypton.h"

#include <optional>
#include <string>

#include "privacy/net/krypton/datapath_interface.h"
#include "privacy/net/krypton/pal/mock_http_fetcher_interface.h"
#include "privacy/net/krypton/pal/mock_notification_interface.h"
#include "privacy/net/krypton/pal/mock_oauth_interface.h"
#include "privacy/net/krypton/pal/mock_timer_interface.h"
#include "privacy/net/krypton/pal/mock_vpn_service_interface.h"
#include "privacy/net/krypton/proto/debug_info.proto.h"
#include "privacy/net/krypton/proto/krypton_telemetry.proto.h"
#include "privacy/net/krypton/timer_manager.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"
#include "third_party/absl/status/status.h"
#include "third_party/absl/strings/string_view.h"
#include "third_party/absl/synchronization/notification.h"
#include "third_party/absl/time/time.h"

namespace privacy {
namespace krypton {
using testing::_;
using testing::EqualsProto;
using testing::Return;

class MockDatapath : public DatapathInterface {
 public:
  MOCK_METHOD(absl::Status, Start,
              (const AddEgressResponse& egress_response,
               const TransformParams& params),
              (override));

  MOCK_METHOD(void, Stop, (), (override));

  MOCK_METHOD(void, RegisterNotificationHandler,
              (DatapathInterface::NotificationInterface*), (override));

  MOCK_METHOD(absl::Status, SwitchNetwork,
              (uint32_t session_id, const Endpoint& endpoint,
               std::optional<NetworkInfo> network_info, int counter),
              (override));

  MOCK_METHOD(absl::Status, SetKeyMaterials, (const TransformParams& params),
              (override));

  MOCK_METHOD(void, GetDebugInfo, (DatapathDebugInfo * debug_info), (override));
};

class KryptonTest : public testing::Test {
 public:
  KryptonTest() {
    config_.set_zinc_url("http://www.example.com/auth");
    config_.set_brass_url("http://brass.example.com/addegress");
    config_.set_service_type("some_type");
  }

  void SetUp() override {
    EXPECT_CALL(vpn_service_, BuildDatapath(_, _, _))
        .WillOnce(Return(new MockDatapath()));
  }

 protected:
  KryptonConfig config_;

  MockHttpFetcher http_fetcher_;
  MockNotification notification_;
  MockVpnService vpn_service_;
  MockOAuth oauth_;
  MockTimerInterface timer_interface_;

  TimerManager timer_manager_{&timer_interface_};
};

TEST_F(KryptonTest, InitializationTest) {
  Krypton krypton(&http_fetcher_, &notification_, &vpn_service_, &oauth_,
                  &timer_manager_);
  // TODO: Fix this test so that the notification is notified and
  // verified.
  absl::Notification done;
  EXPECT_CALL(oauth_, GetOAuthToken).WillRepeatedly(Return("some_token"));
  krypton.Start(config_);
  done.WaitForNotificationWithTimeout(absl::Seconds(2));
  krypton.Stop();
}

TEST_F(KryptonTest, DebugInfoTest) {
  Krypton krypton(&http_fetcher_, &notification_, &vpn_service_, &oauth_,
                  &timer_manager_);
  // TODO: Fix this test so that the notification is notified and
  // verified.
  absl::Notification done;
  EXPECT_CALL(oauth_, GetOAuthToken).WillRepeatedly(Return("some_token"));
  krypton.Start(config_);
  done.WaitForNotificationWithTimeout(absl::Seconds(2));

  KryptonDebugInfo debug_info;
  krypton.GetDebugInfo(&debug_info);

  EXPECT_THAT(debug_info, EqualsProto(R"pb(
                config {
                  zinc_url: "http://www.example.com/auth"
                  brass_url: "http://brass.example.com/addegress"
                  service_type: "some_type"
                }
                reconnector {
                  state: "WaitingToReconnect"
                  session_restart_counter: 1
                  successive_control_plane_failures: 2
                  successive_data_plane_failures: 1
                }
              )pb"));

  KryptonTelemetry telemetry;
  krypton.CollectTelemetry(&telemetry);

  EXPECT_THAT(telemetry, EqualsProto(R"pb(control_plane_failures: 1
                                          data_plane_failures: 0
                                          session_restarts: 1)pb"));
  krypton.Stop();
}

}  // namespace krypton
}  // namespace privacy
