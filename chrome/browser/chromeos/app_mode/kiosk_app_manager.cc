// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/app_mode/kiosk_app_manager.h"

#include <stddef.h>

#include <utility>

#include "base/barrier_closure.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/sys_info.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/app_mode/app_session.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_data.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_external_loader.h"
#include "chrome/browser/chromeos/app_mode/kiosk_app_manager_observer.h"
#include "chrome/browser/chromeos/app_mode/kiosk_external_updater.h"
#include "chrome/browser/chromeos/ownership/owner_settings_service_chromeos.h"
#include "chrome/browser/chromeos/ownership/owner_settings_service_chromeos_factory.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/policy/device_local_account.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/extensions/external_loader.h"
#include "chrome/browser/extensions/external_provider_impl.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chromeos/chromeos_paths.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/cryptohome/async_method_caller.h"
#include "chromeos/cryptohome/cryptohome_parameters.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/settings/cros_settings_names.h"
#include "components/ownership/owner_key_util.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/known_user.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/manifest_handlers/kiosk_mode_info.h"

namespace chromeos {

namespace {

// Domain that is used for kiosk-app account IDs.
const char kKioskAppAccountDomain[] = "kiosk-apps";

// Preference for the dictionary of user ids for which cryptohomes have to be
// removed upon browser restart.
const char kKioskUsersToRemove[] = "kiosk-users-to-remove";

std::string GenerateKioskAppAccountId(const std::string& app_id) {
  return app_id + '@' + kKioskAppAccountDomain;
}

void ScheduleDelayedCryptohomeRemoval(const cryptohome::Identification& id,
                                      const std::string& app_id) {
  PrefService* local_state = g_browser_process->local_state();
  DictionaryPrefUpdate dict_update(local_state, kKioskUsersToRemove);

  // We are using cryptohome::Identification here because it cannot change
  // before actual removal will take place. (Possible cryptohome migration
  // happens only on session start, but deletion should happen before it.)
  dict_update->SetStringWithoutPathExpansion(id.id(), app_id);
  local_state->CommitPendingWrite();
}

void CancelDelayedCryptohomeRemoval(const cryptohome::Identification& id) {
  PrefService* local_state = g_browser_process->local_state();
  DictionaryPrefUpdate dict_update(local_state, kKioskUsersToRemove);
  dict_update->RemoveWithoutPathExpansion(id.id(), nullptr);
  local_state->CommitPendingWrite();
}

void OnRemoveAppCryptohomeComplete(const cryptohome::Identification& id,
                                   const std::string& app,
                                   const base::Closure& callback,
                                   bool success,
                                   cryptohome::MountError return_code) {
  if (success) {
    CancelDelayedCryptohomeRemoval(id);
  } else {
    ScheduleDelayedCryptohomeRemoval(id, app);
    LOG(ERROR) << "Remove cryptohome for " << app
        << " failed, return code: " << return_code;
  }
  if (!callback.is_null())
    callback.Run();
}

void PerformDelayedCryptohomeRemovals(bool service_is_available) {
  if (!service_is_available) {
    LOG(ERROR) << "Crypthomed is not available.";
    return;
  }

  PrefService* local_state = g_browser_process->local_state();
  const base::DictionaryValue* dict =
      local_state->GetDictionary(kKioskUsersToRemove);
  for (base::DictionaryValue::Iterator it(*dict); !it.IsAtEnd(); it.Advance()) {
    const cryptohome::Identification cryptohome_id(
        cryptohome::Identification::FromString(it.key()));
    std::string app_id;
    it.value().GetAsString(&app_id);
    VLOG(1) << "Removing obsolete crypthome for " << app_id;
    cryptohome::AsyncMethodCaller::GetInstance()->AsyncRemove(
        cryptohome_id, base::Bind(&OnRemoveAppCryptohomeComplete, cryptohome_id,
                                  app_id, base::Closure()));
  }
}

// Check for presence of machine owner public key file.
void CheckOwnerFilePresence(bool *present) {
  scoped_refptr<ownership::OwnerKeyUtil> util =
      OwnerSettingsServiceChromeOSFactory::GetInstance()->GetOwnerKeyUtil();
  *present = util.get() && util->IsPublicKeyPresent();
}

scoped_refptr<base::SequencedTaskRunner> GetBackgroundTaskRunner() {
  base::SequencedWorkerPool* pool = content::BrowserThread::GetBlockingPool();
  CHECK(pool);
  return pool->GetSequencedTaskRunnerWithShutdownBehavior(
      pool->GetSequenceToken(), base::SequencedWorkerPool::SKIP_ON_SHUTDOWN);
}

base::Version GetPlatformVersion() {
  int32_t major_version;
  int32_t minor_version;
  int32_t bugfix_version;
  base::SysInfo::OperatingSystemVersionNumbers(&major_version, &minor_version,
                                               &bugfix_version);
  return base::Version(base::StringPrintf("%d.%d.%d", major_version,
                                          minor_version, bugfix_version));
}

}  // namespace

// static
const char KioskAppManager::kKioskDictionaryName[] = "kiosk";
const char KioskAppManager::kKeyApps[] = "apps";
const char KioskAppManager::kKeyAutoLoginState[] = "auto_login_state";
const char KioskAppManager::kIconCacheDir[] = "kiosk/icon";
const char KioskAppManager::kCrxCacheDir[] = "kiosk/crx";
const char KioskAppManager::kCrxUnpackDir[] = "kiosk_unpack";

// static
static base::LazyInstance<KioskAppManager> instance = LAZY_INSTANCE_INITIALIZER;
KioskAppManager* KioskAppManager::Get() {
  return instance.Pointer();
}

// static
void KioskAppManager::Shutdown() {
  if (instance == nullptr)
    return;

  instance.Pointer()->CleanUp();
}

// static
void KioskAppManager::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kKioskDictionaryName);
  registry->RegisterDictionaryPref(kKioskUsersToRemove);
}

// static
void KioskAppManager::RemoveObsoleteCryptohomes() {
  chromeos::CryptohomeClient* client =
      chromeos::DBusThreadManager::Get()->GetCryptohomeClient();
  client->WaitForServiceToBeAvailable(
      base::Bind(&PerformDelayedCryptohomeRemovals));
}

// static
bool KioskAppManager::IsConsumerKioskEnabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableConsumerKiosk);
}

KioskAppManager::App::App(const KioskAppData& data,
                          bool is_extension_pending,
                          bool auto_launched_with_zero_delay)
    : app_id(data.app_id()),
      account_id(data.account_id()),
      name(data.name()),
      icon(data.icon()),
      required_platform_version(data.required_platform_version()),
      is_loading(data.IsLoading() || is_extension_pending),
      was_auto_launched_with_zero_delay(auto_launched_with_zero_delay) {}

KioskAppManager::App::App()
    : account_id(EmptyAccountId()),
      is_loading(false),
      was_auto_launched_with_zero_delay(false) {}

KioskAppManager::App::App(const App& other) = default;

KioskAppManager::App::~App() {}

std::string KioskAppManager::GetAutoLaunchApp() const {
  return auto_launch_app_id_;
}

void KioskAppManager::SetAutoLaunchApp(const std::string& app_id,
                                       OwnerSettingsServiceChromeOS* service) {
  SetAutoLoginState(AUTOLOGIN_REQUESTED);
  // Clean first, so the proper change callbacks are triggered even
  // if we are only changing AutoLoginState here.
  if (!auto_launch_app_id_.empty()) {
    service->SetString(kAccountsPrefDeviceLocalAccountAutoLoginId,
                       std::string());
  }

  service->SetString(
      kAccountsPrefDeviceLocalAccountAutoLoginId,
      app_id.empty() ? std::string() : GenerateKioskAppAccountId(app_id));
  service->SetInteger(kAccountsPrefDeviceLocalAccountAutoLoginDelay, 0);
}

void KioskAppManager::SetAppWasAutoLaunchedWithZeroDelay(
    const std::string& app_id) {
  DCHECK_EQ(auto_launch_app_id_, app_id);
  currently_auto_launched_with_zero_delay_app_ = app_id;
}

void KioskAppManager::InitSession(Profile* profile,
                                   const std::string& app_id) {
  LOG_IF(FATAL, app_session_) << "Kiosk session is already initialized.";

  app_session_.reset(new AppSession);
  app_session_->Init(profile, app_id);
}

void KioskAppManager::AddAppForTest(
    const std::string& app_id,
    const AccountId& account_id,
    const GURL& update_url,
    const std::string& required_platform_version) {
  for (auto it = apps_.begin(); it != apps_.end(); ++it) {
    if ((*it)->app_id() == app_id) {
      apps_.erase(it);
      break;
    }
  }

  apps_.emplace_back(KioskAppData::CreateForTest(
      this, app_id, account_id, update_url, required_platform_version));
}

void KioskAppManager::EnableConsumerKioskAutoLaunch(
    const KioskAppManager::EnableKioskAutoLaunchCallback& callback) {
  if (!IsConsumerKioskEnabled()) {
    if (!callback.is_null())
      callback.Run(false);
    return;
  }

  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  connector->GetInstallAttributes()->LockDevice(
      policy::DEVICE_MODE_CONSUMER_KIOSK_AUTOLAUNCH,
      std::string(),  // domain
      std::string(),  // realm
      std::string(),  // device_id
      base::Bind(
          &KioskAppManager::OnLockDevice, base::Unretained(this), callback));
}

void KioskAppManager::GetConsumerKioskAutoLaunchStatus(
    const KioskAppManager::GetConsumerKioskAutoLaunchStatusCallback& callback) {
  if (!IsConsumerKioskEnabled()) {
    if (!callback.is_null())
      callback.Run(CONSUMER_KIOSK_AUTO_LAUNCH_DISABLED);
    return;
  }

  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  connector->GetInstallAttributes()->ReadImmutableAttributes(
      base::Bind(&KioskAppManager::OnReadImmutableAttributes,
                 base::Unretained(this),
                 callback));
}

bool KioskAppManager::IsConsumerKioskDeviceWithAutoLaunch() {
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  return connector->GetInstallAttributes() &&
         connector->GetInstallAttributes()
             ->IsConsumerKioskDeviceWithAutoLaunch();
}

void KioskAppManager::OnLockDevice(
    const KioskAppManager::EnableKioskAutoLaunchCallback& callback,
    InstallAttributes::LockResult result) {
  if (callback.is_null())
    return;

  callback.Run(result == InstallAttributes::LOCK_SUCCESS);
}

void KioskAppManager::OnOwnerFileChecked(
    const KioskAppManager::GetConsumerKioskAutoLaunchStatusCallback& callback,
    bool* owner_present) {
  ownership_established_ = *owner_present;

  if (callback.is_null())
    return;

  // If we have owner already established on the machine, don't let
  // consumer kiosk to be enabled.
  if (ownership_established_)
    callback.Run(CONSUMER_KIOSK_AUTO_LAUNCH_DISABLED);
  else
    callback.Run(CONSUMER_KIOSK_AUTO_LAUNCH_CONFIGURABLE);
}

void KioskAppManager::OnReadImmutableAttributes(
    const KioskAppManager::GetConsumerKioskAutoLaunchStatusCallback&
        callback) {
  if (callback.is_null())
    return;

  ConsumerKioskAutoLaunchStatus status =
      CONSUMER_KIOSK_AUTO_LAUNCH_DISABLED;
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  InstallAttributes* attributes = connector->GetInstallAttributes();
  switch (attributes->GetMode()) {
    case policy::DEVICE_MODE_NOT_SET: {
      if (!base::SysInfo::IsRunningOnChromeOS()) {
        status = CONSUMER_KIOSK_AUTO_LAUNCH_CONFIGURABLE;
      } else if (!ownership_established_) {
        bool* owner_present = new bool(false);
        content::BrowserThread::PostBlockingPoolTaskAndReply(
            FROM_HERE,
            base::Bind(&CheckOwnerFilePresence,
                       owner_present),
            base::Bind(&KioskAppManager::OnOwnerFileChecked,
                       base::Unretained(this),
                       callback,
                       base::Owned(owner_present)));
        return;
      }
      break;
    }
    case policy::DEVICE_MODE_CONSUMER_KIOSK_AUTOLAUNCH:
      status = CONSUMER_KIOSK_AUTO_LAUNCH_ENABLED;
      break;
    default:
      break;
  }

  callback.Run(status);
}

void KioskAppManager::SetEnableAutoLaunch(bool value) {
  SetAutoLoginState(value ? AUTOLOGIN_APPROVED : AUTOLOGIN_REJECTED);
}

bool KioskAppManager::IsAutoLaunchRequested() const {
  if (GetAutoLaunchApp().empty())
    return false;

  // Apps that were installed by the policy don't require machine owner
  // consent through UI.
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  if (connector->IsEnterpriseManaged())
    return false;

  return GetAutoLoginState() == AUTOLOGIN_REQUESTED;
}

bool KioskAppManager::IsAutoLaunchEnabled() const {
  if (GetAutoLaunchApp().empty())
    return false;

  // Apps that were installed by the policy don't require machine owner
  // consent through UI.
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  if (connector->IsEnterpriseManaged())
    return true;

  return GetAutoLoginState() == AUTOLOGIN_APPROVED;
}

std::string KioskAppManager::GetAutoLaunchAppRequiredPlatformVersion() const {
  // Bail out if there is no auto launched app with zero delay.
  if (!IsAutoLaunchEnabled() || !GetAutoLaunchDelay().is_zero())
    return std::string();

  const KioskAppData* data = GetAppData(GetAutoLaunchApp());
  return data == nullptr ? std::string() : data->required_platform_version();
}

void KioskAppManager::AddApp(const std::string& app_id,
                             OwnerSettingsServiceChromeOS* service) {
  std::vector<policy::DeviceLocalAccount> device_local_accounts =
      policy::GetDeviceLocalAccounts(CrosSettings::Get());

  // Don't insert the app if it's already in the list.
  for (std::vector<policy::DeviceLocalAccount>::const_iterator
           it = device_local_accounts.begin();
       it != device_local_accounts.end(); ++it) {
    if (it->type == policy::DeviceLocalAccount::TYPE_KIOSK_APP &&
        it->kiosk_app_id == app_id) {
      return;
    }
  }

  // Add the new account.
  device_local_accounts.push_back(policy::DeviceLocalAccount(
      policy::DeviceLocalAccount::TYPE_KIOSK_APP,
      GenerateKioskAppAccountId(app_id),
      app_id,
      std::string()));

  policy::SetDeviceLocalAccounts(service, device_local_accounts);
}

void KioskAppManager::RemoveApp(const std::string& app_id,
                                OwnerSettingsServiceChromeOS* service) {
  // Resets auto launch app if it is the removed app.
  if (auto_launch_app_id_ == app_id)
    SetAutoLaunchApp(std::string(), service);

  std::vector<policy::DeviceLocalAccount> device_local_accounts =
      policy::GetDeviceLocalAccounts(CrosSettings::Get());
  if (device_local_accounts.empty())
    return;

  // Remove entries that match |app_id|.
  for (std::vector<policy::DeviceLocalAccount>::iterator
           it = device_local_accounts.begin();
       it != device_local_accounts.end(); ++it) {
    if (it->type == policy::DeviceLocalAccount::TYPE_KIOSK_APP &&
        it->kiosk_app_id == app_id) {
      device_local_accounts.erase(it);
      break;
    }
  }

  policy::SetDeviceLocalAccounts(service, device_local_accounts);
}

void KioskAppManager::GetApps(Apps* apps) const {
  apps->clear();
  apps->reserve(apps_.size());
  for (size_t i = 0; i < apps_.size(); ++i) {
    const KioskAppData& app_data = *apps_[i];
    if (app_data.status() != KioskAppData::STATUS_ERROR) {
      apps->push_back(App(
          app_data, external_cache_->IsExtensionPending(app_data.app_id()),
          app_data.app_id() == currently_auto_launched_with_zero_delay_app_));
    }
  }
}

bool KioskAppManager::GetApp(const std::string& app_id, App* app) const {
  const KioskAppData* data = GetAppData(app_id);
  if (!data)
    return false;

  *app = App(*data, external_cache_->IsExtensionPending(app_id),
             app_id == currently_auto_launched_with_zero_delay_app_);
  return true;
}

bool KioskAppManager::GetDisableBailoutShortcut() const {
  bool enable;
  if (CrosSettings::Get()->GetBoolean(
          kAccountsPrefDeviceLocalAccountAutoLoginBailoutEnabled, &enable)) {
    return !enable;
  }

  return false;
}

void KioskAppManager::ClearAppData(const std::string& app_id) {
  KioskAppData* app_data = GetAppDataMutable(app_id);
  if (!app_data)
    return;

  app_data->ClearCache();
}

void KioskAppManager::UpdateAppDataFromProfile(
    const std::string& app_id,
    Profile* profile,
    const extensions::Extension* app) {
  KioskAppData* app_data = GetAppDataMutable(app_id);
  if (!app_data)
    return;

  app_data->LoadFromInstalledApp(profile, app);
}

void KioskAppManager::RetryFailedAppDataFetch() {
  for (size_t i = 0; i < apps_.size(); ++i) {
    if (apps_[i]->status() == KioskAppData::STATUS_ERROR)
      apps_[i]->Load();
  }
}

bool KioskAppManager::HasCachedCrx(const std::string& app_id) const {
  base::FilePath crx_path;
  std::string version;
  return GetCachedCrx(app_id, &crx_path, &version);
}

bool KioskAppManager::GetCachedCrx(const std::string& app_id,
                                   base::FilePath* file_path,
                                   std::string* version) const {
  return external_cache_->GetExtension(app_id, file_path, version);
}

void KioskAppManager::AddObserver(KioskAppManagerObserver* observer) {
  observers_.AddObserver(observer);
}

void KioskAppManager::RemoveObserver(KioskAppManagerObserver* observer) {
  observers_.RemoveObserver(observer);
}

extensions::ExternalLoader* KioskAppManager::CreateExternalLoader() {
  if (external_loader_created_) {
    NOTREACHED();
    return nullptr;
  }
  external_loader_created_ = true;
  KioskAppExternalLoader* loader = new KioskAppExternalLoader();
  external_loader_ = loader->AsWeakPtr();

  return loader;
}

extensions::ExternalLoader*
KioskAppManager::CreateSecondaryAppExternalLoader() {
  if (secondary_app_external_loader_created_) {
    NOTREACHED();
    return nullptr;
  }
  secondary_app_external_loader_created_ = true;
  KioskAppExternalLoader* secondary_loader = new KioskAppExternalLoader();
  secondary_app_external_loader_ = secondary_loader->AsWeakPtr();

  return secondary_loader;
}

void KioskAppManager::InstallFromCache(const std::string& id) {
  const base::DictionaryValue* extension = nullptr;
  if (external_cache_->cached_extensions()->GetDictionary(id, &extension)) {
    std::unique_ptr<base::DictionaryValue> prefs(new base::DictionaryValue);
    base::DictionaryValue* extension_copy = extension->DeepCopy();
    prefs->Set(id, extension_copy);
    external_loader_->SetCurrentAppExtensions(std::move(prefs));
  } else {
    LOG(ERROR) << "Can't find app in the cached externsions"
               << " id = " << id;
  }
}

void KioskAppManager::InstallSecondaryApps(
    const std::vector<std::string>& ids) {
  std::unique_ptr<base::DictionaryValue> prefs(new base::DictionaryValue);
  for (const std::string& id : ids) {
    std::unique_ptr<base::DictionaryValue> extension_entry(
        new base::DictionaryValue);
    extension_entry->SetStringWithoutPathExpansion(
        extensions::ExternalProviderImpl::kExternalUpdateUrl,
        extension_urls::GetWebstoreUpdateUrl().spec());
    extension_entry->SetBoolean(
        extensions::ExternalProviderImpl::kIsFromWebstore, true);
    prefs->Set(id, std::move(extension_entry));
  }
  secondary_app_external_loader_->SetCurrentAppExtensions(std::move(prefs));
}

void KioskAppManager::UpdateExternalCache() {
  UpdateAppData();
}

void KioskAppManager::OnKioskAppCacheUpdated(const std::string& app_id) {
  for (auto& observer : observers_)
    observer.OnKioskAppCacheUpdated(app_id);
}

void KioskAppManager::OnKioskAppExternalUpdateComplete(bool success) {
  for (auto& observer : observers_)
    observer.OnKioskAppExternalUpdateComplete(success);
}

void KioskAppManager::PutValidatedExternalExtension(
    const std::string& app_id,
    const base::FilePath& crx_path,
    const std::string& version,
    const ExternalCache::PutExternalExtensionCallback& callback) {
  external_cache_->PutExternalExtension(app_id, crx_path, version, callback);
}

bool KioskAppManager::IsPlatformCompliant(
    const std::string& required_platform_version) const {
  // Empty required version is compliant with any platform version.
  if (required_platform_version.empty())
    return true;

  // Not compliant for bad formatted required versions.
  const base::Version required_version(required_platform_version);
  if (!required_version.IsValid() ||
      required_version.components().size() > 3u) {
    LOG(ERROR) << "Bad formatted required platform version: "
               << required_platform_version;
    return false;
  }

  // Not compliant if the platform version components do not match.
  const size_t count = required_version.components().size();
  const base::Version platform_version = GetPlatformVersion();
  const auto& platform_version_components = platform_version.components();
  const auto& required_version_components = required_version.components();
  for (size_t i = 0; i < count; ++i) {
    if (platform_version_components[i] != required_version_components[i])
      return false;
  }

  return true;
}

bool KioskAppManager::IsPlatformCompliantWithApp(
    const extensions::Extension* app) const {
  // Compliant if the app is not the auto launched with zero delay app.
  if (currently_auto_launched_with_zero_delay_app_ != app->id())
    return true;

  // Compliant if the app does not specify required platform version.
  const extensions::KioskModeInfo* info = extensions::KioskModeInfo::Get(app);
  if (info == nullptr)
    return true;

  // Compliant if the app wants to be always updated.
  if (info->always_update)
    return true;

  return IsPlatformCompliant(info->required_platform_version);
}

KioskAppManager::KioskAppManager()
    : ownership_established_(false),
      external_loader_created_(false),
      secondary_app_external_loader_created_(false) {
  base::FilePath cache_dir;
  GetCrxCacheDir(&cache_dir);
  external_cache_.reset(
      new ExternalCache(cache_dir,
                        g_browser_process->system_request_context(),
                        GetBackgroundTaskRunner(),
                        this,
                        true /* always_check_updates */,
                        false /* wait_for_cache_initialization */));
  external_cache_->set_flush_on_put(true);
  UpdateAppData();
  local_accounts_subscription_ =
      CrosSettings::Get()->AddSettingsObserver(
          kAccountsPrefDeviceLocalAccounts,
          base::Bind(&KioskAppManager::UpdateAppData, base::Unretained(this)));
  local_account_auto_login_id_subscription_ =
      CrosSettings::Get()->AddSettingsObserver(
          kAccountsPrefDeviceLocalAccountAutoLoginId,
          base::Bind(&KioskAppManager::UpdateAppData, base::Unretained(this)));
}

KioskAppManager::~KioskAppManager() {}

void KioskAppManager::MonitorKioskExternalUpdate() {
  base::FilePath cache_dir;
  GetCrxCacheDir(&cache_dir);
  base::FilePath unpack_dir;
  GetCrxUnpackDir(&unpack_dir);
  usb_stick_updater_.reset(new KioskExternalUpdater(
      GetBackgroundTaskRunner(), cache_dir, unpack_dir));
}

void KioskAppManager::CleanUp() {
  local_accounts_subscription_.reset();
  local_account_auto_login_id_subscription_.reset();
  apps_.clear();
  usb_stick_updater_.reset();
  external_cache_.reset();
}

const KioskAppData* KioskAppManager::GetAppData(
    const std::string& app_id) const {
  for (const auto& app : apps_) {
    if (app->app_id() == app_id)
      return app.get();
  }

  return nullptr;
}

KioskAppData* KioskAppManager::GetAppDataMutable(const std::string& app_id) {
  return const_cast<KioskAppData*>(GetAppData(app_id));
}

void KioskAppManager::UpdateAppData() {
  // Gets app id to data mapping for existing apps.
  std::map<std::string, std::unique_ptr<KioskAppData>> old_apps;
  for (auto& app : apps_)
    old_apps[app->app_id()] = std::move(app);
  apps_.clear();

  auto_launch_app_id_.clear();
  std::string auto_login_account_id;
  CrosSettings::Get()->GetString(kAccountsPrefDeviceLocalAccountAutoLoginId,
                                 &auto_login_account_id);

  // Re-populates |apps_| and reuses existing KioskAppData when possible.
  const std::vector<policy::DeviceLocalAccount> device_local_accounts =
      policy::GetDeviceLocalAccounts(CrosSettings::Get());
  for (std::vector<policy::DeviceLocalAccount>::const_iterator
           it = device_local_accounts.begin();
       it != device_local_accounts.end(); ++it) {
    if (it->type != policy::DeviceLocalAccount::TYPE_KIOSK_APP)
      continue;

    if (it->account_id == auto_login_account_id)
      auto_launch_app_id_ = it->kiosk_app_id;

    // Note that app ids are not canonical, i.e. they can contain upper
    // case letters.
    const AccountId account_id(AccountId::FromUserEmail(it->user_id));
    auto old_it = old_apps.find(it->kiosk_app_id);
    if (old_it != old_apps.end()) {
      apps_.push_back(std::move(old_it->second));
      old_apps.erase(old_it);
    } else {
      base::FilePath cached_crx;
      std::string version;
      GetCachedCrx(it->kiosk_app_id, &cached_crx, &version);

      apps_.push_back(base::MakeUnique<KioskAppData>(
          this, it->kiosk_app_id, account_id, GURL(it->kiosk_app_update_url),
          cached_crx));
      apps_.back()->Load();
    }
    CancelDelayedCryptohomeRemoval(cryptohome::Identification(account_id));
  }

  ClearRemovedApps(old_apps);
  UpdateExternalCachePrefs();
  RetryFailedAppDataFetch();

  for (auto& observer : observers_)
    observer.OnKioskAppsSettingsChanged();
}

void KioskAppManager::ClearRemovedApps(
    const std::map<std::string, std::unique_ptr<KioskAppData>>& old_apps) {
  base::Closure cryptohomes_barrier_closure;

  const user_manager::User* active_user =
      user_manager::UserManager::Get()->GetActiveUser();
  if (active_user) {
    const AccountId active_account_id = active_user->GetAccountId();
    for (const auto& it : old_apps) {
      if (it.second->account_id() == active_account_id) {
        VLOG(1) << "Currently running kiosk app removed from policy, exiting";
        cryptohomes_barrier_closure = BarrierClosure(
            old_apps.size(), base::Bind(&chrome::AttemptUserExit));
        break;
      }
    }
  }

  // Clears cache and deletes the remaining old data.
  std::vector<std::string> apps_to_remove;
  for (auto& entry : old_apps) {
    entry.second->ClearCache();
    const cryptohome::Identification cryptohome_id(entry.second->account_id());
    cryptohome::AsyncMethodCaller::GetInstance()->AsyncRemove(
        cryptohome_id, base::Bind(&OnRemoveAppCryptohomeComplete, cryptohome_id,
                                  entry.first, cryptohomes_barrier_closure));
    apps_to_remove.push_back(entry.second->app_id());
  }
  external_cache_->RemoveExtensions(apps_to_remove);
}

void KioskAppManager::UpdateExternalCachePrefs() {
  // Request external_cache_ to download new apps and update the existing apps.
  std::unique_ptr<base::DictionaryValue> prefs(new base::DictionaryValue);
  for (size_t i = 0; i < apps_.size(); ++i) {
    std::unique_ptr<base::DictionaryValue> entry(new base::DictionaryValue);

    if (apps_[i]->update_url().is_valid()) {
      entry->SetString(extensions::ExternalProviderImpl::kExternalUpdateUrl,
                       apps_[i]->update_url().spec());
    } else {
      entry->SetString(extensions::ExternalProviderImpl::kExternalUpdateUrl,
                       extension_urls::GetWebstoreUpdateUrl().spec());
    }

    prefs->Set(apps_[i]->app_id(), entry.release());
  }
  external_cache_->UpdateExtensionsList(std::move(prefs));
}

void KioskAppManager::GetKioskAppIconCacheDir(base::FilePath* cache_dir) {
  base::FilePath user_data_dir;
  CHECK(PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
  *cache_dir = user_data_dir.AppendASCII(kIconCacheDir);
}

void KioskAppManager::OnKioskAppDataChanged(const std::string& app_id) {
  for (auto& observer : observers_)
    observer.OnKioskAppDataChanged(app_id);
}

void KioskAppManager::OnKioskAppDataLoadFailure(const std::string& app_id) {
  for (auto& observer : observers_)
    observer.OnKioskAppDataLoadFailure(app_id);
}

void KioskAppManager::OnExtensionListsUpdated(
    const base::DictionaryValue* prefs) {
}

void KioskAppManager::OnExtensionLoadedInCache(const std::string& id) {
  KioskAppData* app_data = GetAppDataMutable(id);
  if (!app_data)
    return;

  base::FilePath crx_path;
  std::string version;
  if (GetCachedCrx(id, &crx_path, &version))
    app_data->SetCachedCrx(crx_path);

  for (auto& observer : observers_)
    observer.OnKioskExtensionLoadedInCache(id);
}

void KioskAppManager::OnExtensionDownloadFailed(
    const std::string& id,
    extensions::ExtensionDownloaderDelegate::Error error) {
  KioskAppData* app_data = GetAppDataMutable(id);
  if (!app_data)
    return;
  for (auto& observer : observers_)
    observer.OnKioskExtensionDownloadFailed(id);
}

KioskAppManager::AutoLoginState KioskAppManager::GetAutoLoginState() const {
  PrefService* prefs = g_browser_process->local_state();
  const base::DictionaryValue* dict =
      prefs->GetDictionary(KioskAppManager::kKioskDictionaryName);
  int value;
  if (!dict->GetInteger(kKeyAutoLoginState, &value))
    return AUTOLOGIN_NONE;

  return static_cast<AutoLoginState>(value);
}

void KioskAppManager::SetAutoLoginState(AutoLoginState state) {
  PrefService* prefs = g_browser_process->local_state();
  DictionaryPrefUpdate dict_update(prefs,
                                   KioskAppManager::kKioskDictionaryName);
  dict_update->SetInteger(kKeyAutoLoginState, state);
  prefs->CommitPendingWrite();
}

void KioskAppManager::GetCrxCacheDir(base::FilePath* cache_dir) {
  base::FilePath user_data_dir;
  CHECK(PathService::Get(chrome::DIR_USER_DATA, &user_data_dir));
  *cache_dir = user_data_dir.AppendASCII(kCrxCacheDir);
}

void KioskAppManager::GetCrxUnpackDir(base::FilePath* unpack_dir) {
  base::FilePath temp_dir;
  base::GetTempDir(&temp_dir);
  *unpack_dir = temp_dir.AppendASCII(kCrxUnpackDir);
}

base::TimeDelta KioskAppManager::GetAutoLaunchDelay() const {
  int delay;
  if (!CrosSettings::Get()->GetInteger(
          kAccountsPrefDeviceLocalAccountAutoLoginDelay, &delay)) {
    return base::TimeDelta();  // Default delay is 0ms.
  }
  return base::TimeDelta::FromMilliseconds(delay);
}

}  // namespace chromeos
