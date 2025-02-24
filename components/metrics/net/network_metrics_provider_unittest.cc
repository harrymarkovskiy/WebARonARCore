// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/net/network_metrics_provider.h"

#include "base/callback.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/metrics/proto/system_profile.pb.h"
#include "net/base/network_change_notifier.h"
#include "net/nqe/network_quality_estimator_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_CHROMEOS)
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/network/network_handler.h"
#endif  // OS_CHROMEOS

namespace metrics {

namespace {

class TestNetworkQualityEstimatorProvider
    : public NetworkMetricsProvider::NetworkQualityEstimatorProvider {
 public:
  explicit TestNetworkQualityEstimatorProvider(
      net::TestNetworkQualityEstimator* estimator)
      : estimator_(estimator) {}
  ~TestNetworkQualityEstimatorProvider() override {}

 private:
  // NetworkMetricsProvider::NetworkQualityEstimatorProvider:
  scoped_refptr<base::SequencedTaskRunner> GetTaskRunner() override {
    return base::ThreadTaskRunnerHandle::Get();
  }

  net::NetworkQualityEstimator* GetNetworkQualityEstimator() override {
    return estimator_;
  }

  net::TestNetworkQualityEstimator* estimator_;
  DISALLOW_COPY_AND_ASSIGN(TestNetworkQualityEstimatorProvider);
};

}  // namespace

// Verifies that the effective connection type is correctly set.
TEST(NetworkMetricsProviderTest, EffectiveConnectionType) {
  base::MessageLoop loop(base::MessageLoop::TYPE_IO);

#if defined(OS_CHROMEOS)
  chromeos::DBusThreadManager::Initialize();
  chromeos::NetworkHandler::Initialize();
#endif  // OS_CHROMEOS

  net::TestNetworkQualityEstimator estimator;
  std::unique_ptr<NetworkMetricsProvider::NetworkQualityEstimatorProvider>
      estimator_provider(base::WrapUnique(
          new TestNetworkQualityEstimatorProvider(&estimator)));
  SystemProfileProto system_profile;
  NetworkMetricsProvider network_metrics_provider(
      std::move(estimator_provider), base::ThreadTaskRunnerHandle::Get().get());

  EXPECT_EQ(net::EFFECTIVE_CONNECTION_TYPE_UNKNOWN,
            network_metrics_provider.effective_connection_type_);
  network_metrics_provider.ProvideSystemProfileMetrics(&system_profile);
  EXPECT_EQ(SystemProfileProto::Network::EFFECTIVE_CONNECTION_TYPE_UNKNOWN,
            system_profile.network().effective_connection_type());

  // Set RTT so that the effective connection type is computed as 2G.
  estimator.set_recent_http_rtt(base::TimeDelta::FromMilliseconds(1500));
  estimator.set_start_time_null_http_rtt(
      base::TimeDelta::FromMilliseconds(1500));

  EXPECT_EQ(net::EFFECTIVE_CONNECTION_TYPE_UNKNOWN,
            network_metrics_provider.effective_connection_type_);
  // Running a request would cause the effective connection type to be computed
  // as 2G, and observers to be notified.
  estimator.RunOneRequest();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(net::EFFECTIVE_CONNECTION_TYPE_2G,
            network_metrics_provider.effective_connection_type_);
  network_metrics_provider.ProvideSystemProfileMetrics(&system_profile);
  EXPECT_EQ(SystemProfileProto::Network::EFFECTIVE_CONNECTION_TYPE_2G,
            system_profile.network().effective_connection_type());

  // Set RTT so that the effective connection type is computed as SLOW_2G.
  estimator.set_recent_http_rtt(base::TimeDelta::FromMilliseconds(3000));
  estimator.set_start_time_null_http_rtt(
      base::TimeDelta::FromMilliseconds(3000));
  // Running a request would cause the effective connection type to be computed
  // as SLOW_2G, and observers to be notified.
  estimator.RunOneRequest();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(net::EFFECTIVE_CONNECTION_TYPE_SLOW_2G,
            network_metrics_provider.effective_connection_type_);
  network_metrics_provider.ProvideSystemProfileMetrics(&system_profile);
  // Effective connection type is set to ambiguous since the effective
  // connection type changed from 2G to SLOW_2G during the lifetime of the log.
  EXPECT_EQ(SystemProfileProto::Network::EFFECTIVE_CONNECTION_TYPE_AMBIGUOUS,
            system_profile.network().effective_connection_type());

  // Getting the system profile again should return the actual effective
  // connection type since the effective connection type did not change during
  // the lifetime of the log.
  network_metrics_provider.ProvideSystemProfileMetrics(&system_profile);
  EXPECT_EQ(SystemProfileProto::Network::EFFECTIVE_CONNECTION_TYPE_SLOW_2G,
            system_profile.network().effective_connection_type());
}

}  // namespace metrics