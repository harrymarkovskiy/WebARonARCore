// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_DATA_USE_MEASUREMENT_CORE_DATA_USE_USER_DATA_H_
#define COMPONENTS_DATA_USE_MEASUREMENT_CORE_DATA_USE_USER_DATA_H_

#include <string>

#include "base/macros.h"
#include "base/supports_user_data.h"

namespace net {
class URLFetcher;
}

namespace data_use_measurement {

// Used to annotate URLRequests with the service name if the URLRequest is used
// by a service.
class DataUseUserData : public base::SupportsUserData::Data {
 public:
  // This enum should be synced with DataUseServices enum in histograms.xml.
  // Please keep them synced after any updates. Also add service name to
  // GetServiceNameAsString function.
  enum ServiceName {
    NOT_TAGGED,
    SUGGESTIONS,
    TRANSLATE,
    SYNC,
    OMNIBOX,
    INVALIDATION,
    RAPPOR,
    VARIATIONS,
    UMA,
    DOMAIN_RELIABILITY,
    PROFILE_DOWNLOADER,
    GOOGLE_URL_TRACKER,
    AUTOFILL,
    POLICY,
    SPELL_CHECKER,
    NTP_SNIPPETS,
    SAFE_BROWSING,
    DATA_REDUCTION_PROXY,
    PRECACHE,
    NTP_TILES,
    FEEDBACK_UPLOADER,
    TRACING_UPLOADER,
    DOM_DISTILLER,
    CLOUD_PRINT,
    SEARCH_PROVIDER_LOGOS,
    UPDATE_CLIENT,
  };

  // Data use broken by content type. This enum must remain synchronized
  // with the enum of the same name in metrics/histograms/histograms.xml.
  // These values are written to logs.  New enum values can be added, but
  // existing enums must never be renumbered or deleted and reused.
  enum DataUseContentType {
    OTHER = 0,
    MAIN_FRAME_HTML = 1,
    NON_MAIN_FRAME_HTML = 2,
    CSS = 3,
    IMAGE = 4,
    JAVASCRIPT = 5,
    FONT = 6,
    AUDIO_APPBACKGROUND = 7,
    AUDIO_TABBACKGROUND = 8,
    AUDIO = 9,
    VIDEO_APPBACKGROUND = 10,
    VIDEO_TABBACKGROUND = 11,
    VIDEO = 12,
    TYPE_MAX = 13,
  };

  // The state of the application. Only available on Android and on other
  // platforms it is always FOREGROUND.
  enum AppState { UNKNOWN, BACKGROUND, FOREGROUND };

  DataUseUserData(ServiceName service_name, AppState app_state);
  ~DataUseUserData() override;

  // Helper function to create DataUseUserData. The caller takes the ownership
  // of the returned object.
  static base::SupportsUserData::Data* Create(
      DataUseUserData::ServiceName service);

  // Return the service name of the ServiceName enum.
  static std::string GetServiceNameAsString(ServiceName service);

  // Services should use this function to attach their |service_name| to the
  // URLFetcher serving them.
  static void AttachToFetcher(net::URLFetcher* fetcher,
                              ServiceName service_name);

  ServiceName service_name() const { return service_name_; }

  AppState app_state() const { return app_state_; }

  void set_app_state(AppState app_state) { app_state_ = app_state; }

  DataUseContentType content_type() { return content_type_; }

  void set_content_type(DataUseContentType content_type) {
    content_type_ = content_type;
  }

  // The key for retrieving back this type of user data.
  static const void* const kUserDataKey;

 private:
  const ServiceName service_name_;

  // App state when network access was performed for the request previously.
  AppState app_state_;

  DataUseContentType content_type_;

  DISALLOW_COPY_AND_ASSIGN(DataUseUserData);
};

}  // namespace data_use_measurement

#endif  // COMPONENTS_DATA_USE_MEASUREMENT_CORE_DATA_USE_USER_DATA_H_
