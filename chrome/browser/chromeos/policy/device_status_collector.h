// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_STATUS_COLLECTOR_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_STATUS_COLLECTOR_H_

#include <stdint.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/callback_list.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequenced_task_runner.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chromeos/system/version_loader.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "components/prefs/pref_member.h"
#include "ui/base/idle/idle.h"

namespace chromeos {
class CrosSettings;
namespace system {
class StatisticsProvider;
}
}

class PrefRegistrySimple;
class PrefService;

namespace policy {

struct DeviceLocalAccount;
class GetStatusState;

// Collects and summarizes the status of an enterprised-managed ChromeOS device.
class DeviceStatusCollector {
 public:
  using VolumeInfoFetcher = base::Callback<
    std::vector<enterprise_management::VolumeInfo>(
        const std::vector<std::string>& mount_points)>;

  // Reads the first CPU line from /proc/stat. Returns an empty string if
  // the cpu data could not be read. Broken out into a callback to enable
  // mocking for tests.
  //
  // The format of this line from /proc/stat is:
  //   cpu  user_time nice_time system_time idle_time
  using CPUStatisticsFetcher = base::Callback<std::string(void)>;

  // Reads CPU temperatures from /sys/class/hwmon/hwmon*/temp*_input and
  // appropriate labels from /sys/class/hwmon/hwmon*/temp*_label.
  using CPUTempFetcher =
      base::Callback<std::vector<enterprise_management::CPUTempInfo>()>;

  // Passed into asynchronous mojo interface for communicating with Android.
  using AndroidStatusReceiver =
      base::Callback<void(const std::string&, const std::string&)>;
  // Calls the enterprise reporting mojo interface, passing over the
  // AndroidStatusReceiver. Returns false if the mojo interface isn't available,
  // in which case no asynchronous query is emitted and the android status query
  // fails synchronously. The |AndroidStatusReceiver| is not called in this
  // case.
  using AndroidStatusFetcher =
      base::Callback<bool(const AndroidStatusReceiver&)>;

  // Called in the UI thread after the device and session status have been
  // collected asynchronously in GetDeviceAndSessionStatusAsync. Null pointers
  // indicate errors or that device or session status reporting is disabled.
  using StatusCallback = base::Callback<void(
      std::unique_ptr<enterprise_management::DeviceStatusReportRequest>,
      std::unique_ptr<enterprise_management::SessionStatusReportRequest>)>;

  // Constructor. Callers can inject their own *Fetcher callbacks, e.g. for unit
  // testing. A null callback can be passed for any *Fetcher parameter, to use
  // the default implementation. These callbacks are always executed on Blocking
  // Pool.
  DeviceStatusCollector(
      PrefService* local_state,
      chromeos::system::StatisticsProvider* provider,
      const VolumeInfoFetcher& volume_info_fetcher,
      const CPUStatisticsFetcher& cpu_statistics_fetcher,
      const CPUTempFetcher& cpu_temp_fetcher,
      const AndroidStatusFetcher& android_status_fetcher);
  virtual ~DeviceStatusCollector();

  // Gathers device and session status information and calls the passed response
  // callback. Null pointers passed into the response indicate errors or that
  // device or session status reporting is disabled.
  virtual void GetDeviceAndSessionStatusAsync(const StatusCallback& response);

  // Called after the status information has successfully been submitted to
  // the server.
  void OnSubmittedSuccessfully();

  static void RegisterPrefs(PrefRegistrySimple* registry);

  // Returns the DeviceLocalAccount associated with the currently active
  // kiosk session, if the session was auto-launched with zero delay
  // (this enables functionality such as network reporting).
  // Virtual to allow mocking.
  virtual std::unique_ptr<DeviceLocalAccount> GetAutoLaunchedKioskSessionInfo();

  // How often, in seconds, to poll to see if the user is idle.
  static const unsigned int kIdlePollIntervalSeconds = 30;

  // The total number of hardware resource usage samples cached internally.
  static const unsigned int kMaxResourceUsageSamples = 10;

 protected:
  // Check whether the user has been idle for a certain period of time.
  virtual void CheckIdleState();

  // Used instead of base::Time::Now(), to make testing possible.
  virtual base::Time GetCurrentTime();

  // Callback which receives the results of the idle state check.
  void IdleStateCallback(ui::IdleState state);

  // Gets the version of the passed app. Virtual to allow mocking.
  virtual std::string GetAppVersion(const std::string& app_id);

  // Samples the current hardware resource usage to be sent up with the
  // next device status update.
  void SampleResourceUsage();

  // The number of days in the past to store device activity.
  // This is kept in case device status uploads fail for a number of days.
  unsigned int max_stored_past_activity_days_;

  // The number of days in the future to store device activity.
  // When changing the system time and/or timezones, it's possible to record
  // activity time that is slightly in the future.
  unsigned int max_stored_future_activity_days_;

 private:
  // Prevents the local store of activity periods from growing too large by
  // removing entries that are outside the reporting window.
  void PruneStoredActivityPeriods(base::Time base_time);

  // Trims the store activity periods to only retain data within the
  // [|min_day_key|, |max_day_key|). The record for |min_day_key| will be
  // adjusted by subtracting |min_day_trim_duration|.
  void TrimStoredActivityPeriods(int64_t min_day_key,
                                 int min_day_trim_duration,
                                 int64_t max_day_key);

  void AddActivePeriod(base::Time start, base::Time end);

  // Clears the cached hardware resource usage.
  void ClearCachedResourceUsage();

  // Callbacks from chromeos::VersionLoader.
  void OnOSVersion(const std::string& version);
  void OnOSFirmware(const std::string& version);

  void GetDeviceStatus(scoped_refptr<GetStatusState> state);
  void GetSessionStatus(scoped_refptr<GetStatusState> state);

  // Helpers for the various portions of DEVICE STATUS. Return true if they
  // actually report any status. Functions that queue async queries take
  // a |GetStatusState| instance.
  bool GetActivityTimes(
      enterprise_management::DeviceStatusReportRequest* status);
  bool GetVersionInfo(enterprise_management::DeviceStatusReportRequest* status);
  bool GetBootMode(enterprise_management::DeviceStatusReportRequest* status);
  bool GetNetworkInterfaces(
      enterprise_management::DeviceStatusReportRequest* status);
  bool GetUsers(enterprise_management::DeviceStatusReportRequest* status);
  bool GetHardwareStatus(
      enterprise_management::DeviceStatusReportRequest* status,
      scoped_refptr<GetStatusState> state);  // Queues async queries!
  bool GetOsUpdateStatus(
      enterprise_management::DeviceStatusReportRequest* status);
  bool GetRunningKioskApp(
      enterprise_management::DeviceStatusReportRequest* status);

  // Helpers for the various portions of SESSION STATUS. Return true if they
  // actually report any status. Functions that queue async queries take
  // a |GetStatusState| instance.
  bool GetKioskSessionStatus(
      enterprise_management::SessionStatusReportRequest* status);
  bool GetAndroidStatus(
      enterprise_management::SessionStatusReportRequest* status,
      const scoped_refptr<GetStatusState>& state);  // Queues async queries!

  // Update the cached values of the reporting settings.
  void UpdateReportingSettings();

  // Callback invoked to update our cpu usage information.
  void ReceiveCPUStatistics(const std::string& statistics);

  PrefService* const local_state_;

  // The last time an idle state check was performed.
  base::Time last_idle_check_;

  // The maximum key that went into the last report generated by
  // GetDeviceStatusAsync(), and the duration for it. This is used to trim
  // the stored data in OnSubmittedSuccessfully(). Trimming is delayed so
  // unsuccessful uploads don't result in dropped data.
  int64_t last_reported_day_ = 0;
  int duration_for_last_reported_day_ = 0;

  base::RepeatingTimer idle_poll_timer_;
  base::RepeatingTimer resource_usage_sampling_timer_;

  std::string os_version_;
  std::string firmware_version_;

  struct ResourceUsage {
    // Sample of percentage-of-CPU-used.
    int cpu_usage_percent;

    // Amount of free RAM (measures raw memory used by processes, not internal
    // memory waiting to be reclaimed by GC).
    int64_t bytes_of_ram_free;
  };

  // Samples of resource usage (contains multiple samples taken
  // periodically every kHardwareStatusSampleIntervalSeconds).
  std::deque<ResourceUsage> resource_usage_;

  // Callback invoked to fetch information about the mounted disk volumes.
  VolumeInfoFetcher volume_info_fetcher_;

  // Callback invoked to fetch information about cpu usage.
  CPUStatisticsFetcher cpu_statistics_fetcher_;

  // Callback invoked to fetch information about cpu temperature.
  CPUTempFetcher cpu_temp_fetcher_;

  AndroidStatusFetcher android_status_fetcher_;

  chromeos::system::StatisticsProvider* const statistics_provider_;

  chromeos::CrosSettings* const cros_settings_;

  // The most recent CPU readings.
  uint64_t last_cpu_active_ = 0;
  uint64_t last_cpu_idle_ = 0;

  // Cached values of the reporting settings from the device policy.
  bool report_version_info_ = false;
  bool report_activity_times_ = false;
  bool report_boot_mode_ = false;
  bool report_network_interfaces_ = false;
  bool report_users_ = false;
  bool report_hardware_status_ = false;
  bool report_kiosk_session_status_ = false;
  bool report_android_status_ = false;
  bool report_os_update_status_ = false;
  bool report_running_kiosk_app_ = false;

  std::unique_ptr<chromeos::CrosSettings::ObserverSubscription>
      version_info_subscription_;
  std::unique_ptr<chromeos::CrosSettings::ObserverSubscription>
      activity_times_subscription_;
  std::unique_ptr<chromeos::CrosSettings::ObserverSubscription>
      boot_mode_subscription_;
  std::unique_ptr<chromeos::CrosSettings::ObserverSubscription>
      network_interfaces_subscription_;
  std::unique_ptr<chromeos::CrosSettings::ObserverSubscription>
      users_subscription_;
  std::unique_ptr<chromeos::CrosSettings::ObserverSubscription>
      hardware_status_subscription_;
  std::unique_ptr<chromeos::CrosSettings::ObserverSubscription>
      session_status_subscription_;
  std::unique_ptr<chromeos::CrosSettings::ObserverSubscription>
      os_update_status_subscription_;
  std::unique_ptr<chromeos::CrosSettings::ObserverSubscription>
      running_kiosk_app_subscription_;
  BooleanPrefMember report_arc_status_pref_;

  // Task runner in the creation thread where responses are sent to.
  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  base::ThreadChecker thread_checker_;

  base::WeakPtrFactory<DeviceStatusCollector> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DeviceStatusCollector);
};

}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_DEVICE_STATUS_COLLECTOR_H_
