// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/protocol/proto_enum_conversions.h"

#include "base/logging.h"

namespace syncer {

#define ASSERT_ENUM_BOUNDS(enum_parent, enum_type, enum_min, enum_max) \
  static_assert(enum_parent::enum_type##_MIN == enum_parent::enum_min, \
                #enum_type "_MIN should be " #enum_min);               \
  static_assert(enum_parent::enum_type##_MAX == enum_parent::enum_max, \
                #enum_type "_MAX should be " #enum_max);

#define ENUM_CASE(enum_parent, enum_value) \
  case enum_parent::enum_value:            \
    return #enum_value

const char* ProtoEnumToString(
    sync_pb::AppListSpecifics::AppListItemType item_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::AppListSpecifics, AppListItemType, TYPE_APP,
                     TYPE_URL);
  switch (item_type) {
    ENUM_CASE(sync_pb::AppListSpecifics, TYPE_APP);
    ENUM_CASE(sync_pb::AppListSpecifics, TYPE_REMOVE_DEFAULT_APP);
    ENUM_CASE(sync_pb::AppListSpecifics, TYPE_FOLDER);
    ENUM_CASE(sync_pb::AppListSpecifics, TYPE_URL);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::SessionWindow::BrowserType browser_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::SessionWindow, BrowserType, TYPE_TABBED,
                     TYPE_CUSTOM_TAB);
  switch (browser_type) {
    ENUM_CASE(sync_pb::SessionWindow, TYPE_TABBED);
    ENUM_CASE(sync_pb::SessionWindow, TYPE_POPUP);
    ENUM_CASE(sync_pb::SessionWindow, TYPE_CUSTOM_TAB);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::SyncEnums::PageTransition page_transition) {
  ASSERT_ENUM_BOUNDS(sync_pb::SyncEnums, PageTransition, LINK,
                     KEYWORD_GENERATED);
  switch (page_transition) {
    ENUM_CASE(sync_pb::SyncEnums, LINK);
    ENUM_CASE(sync_pb::SyncEnums, TYPED);
    ENUM_CASE(sync_pb::SyncEnums, AUTO_BOOKMARK);
    ENUM_CASE(sync_pb::SyncEnums, AUTO_SUBFRAME);
    ENUM_CASE(sync_pb::SyncEnums, MANUAL_SUBFRAME);
    ENUM_CASE(sync_pb::SyncEnums, GENERATED);
    ENUM_CASE(sync_pb::SyncEnums, AUTO_TOPLEVEL);
    ENUM_CASE(sync_pb::SyncEnums, FORM_SUBMIT);
    ENUM_CASE(sync_pb::SyncEnums, RELOAD);
    ENUM_CASE(sync_pb::SyncEnums, KEYWORD);
    ENUM_CASE(sync_pb::SyncEnums, KEYWORD_GENERATED);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::SyncEnums::PageTransitionRedirectType page_transition_qualifier) {
  ASSERT_ENUM_BOUNDS(sync_pb::SyncEnums, PageTransitionRedirectType,
                     CLIENT_REDIRECT, SERVER_REDIRECT);
  switch (page_transition_qualifier) {
    ENUM_CASE(sync_pb::SyncEnums, CLIENT_REDIRECT);
    ENUM_CASE(sync_pb::SyncEnums, SERVER_REDIRECT);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::WifiCredentialSpecifics::SecurityClass security_class) {
  ASSERT_ENUM_BOUNDS(sync_pb::WifiCredentialSpecifics, SecurityClass,
                     SECURITY_CLASS_INVALID, SECURITY_CLASS_PSK);
  switch (security_class) {
    ENUM_CASE(sync_pb::WifiCredentialSpecifics, SECURITY_CLASS_INVALID);
    ENUM_CASE(sync_pb::WifiCredentialSpecifics, SECURITY_CLASS_NONE);
    ENUM_CASE(sync_pb::WifiCredentialSpecifics, SECURITY_CLASS_WEP);
    ENUM_CASE(sync_pb::WifiCredentialSpecifics, SECURITY_CLASS_PSK);
  }
  NOTREACHED();
  return "";
}
const char* ProtoEnumToString(
    sync_pb::GetUpdatesCallerInfo::GetUpdatesSource updates_source) {
  ASSERT_ENUM_BOUNDS(sync_pb::GetUpdatesCallerInfo, GetUpdatesSource, UNKNOWN,
                     PROGRAMMATIC);
  switch (updates_source) {
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, UNKNOWN);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, FIRST_UPDATE);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, LOCAL);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, NOTIFICATION);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, PERIODIC);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, SYNC_CYCLE_CONTINUATION);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, NEWLY_SUPPORTED_DATATYPE);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, MIGRATION);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, NEW_CLIENT);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, RECONFIGURATION);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, DATATYPE_REFRESH);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, RETRY);
    ENUM_CASE(sync_pb::GetUpdatesCallerInfo, PROGRAMMATIC);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(sync_pb::SyncEnums::GetUpdatesOrigin origin) {
  ASSERT_ENUM_BOUNDS(sync_pb::SyncEnums, GetUpdatesOrigin, UNKNOWN_ORIGIN,
                     PROGRAMMATIC);
  switch (origin) {
    ENUM_CASE(sync_pb::SyncEnums, UNKNOWN_ORIGIN);
    ENUM_CASE(sync_pb::SyncEnums, PERIODIC);
    ENUM_CASE(sync_pb::SyncEnums, NEWLY_SUPPORTED_DATATYPE);
    ENUM_CASE(sync_pb::SyncEnums, MIGRATION);
    ENUM_CASE(sync_pb::SyncEnums, NEW_CLIENT);
    ENUM_CASE(sync_pb::SyncEnums, RECONFIGURATION);
    ENUM_CASE(sync_pb::SyncEnums, GU_TRIGGER);
    ENUM_CASE(sync_pb::SyncEnums, RETRY);
    ENUM_CASE(sync_pb::SyncEnums, PROGRAMMATIC);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::CommitResponse::ResponseType response_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::CommitResponse, ResponseType, SUCCESS,
                     TRANSIENT_ERROR);
  switch (response_type) {
    ENUM_CASE(sync_pb::CommitResponse, SUCCESS);
    ENUM_CASE(sync_pb::CommitResponse, CONFLICT);
    ENUM_CASE(sync_pb::CommitResponse, RETRY);
    ENUM_CASE(sync_pb::CommitResponse, INVALID_MESSAGE);
    ENUM_CASE(sync_pb::CommitResponse, OVER_QUOTA);
    ENUM_CASE(sync_pb::CommitResponse, TRANSIENT_ERROR);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(sync_pb::SyncEnums::ErrorType error_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::SyncEnums, ErrorType, SUCCESS, UNKNOWN);
  switch (error_type) {
    ENUM_CASE(sync_pb::SyncEnums, SUCCESS);
    ENUM_CASE(sync_pb::SyncEnums, DEPRECATED_ACCESS_DENIED);
    ENUM_CASE(sync_pb::SyncEnums, NOT_MY_BIRTHDAY);
    ENUM_CASE(sync_pb::SyncEnums, THROTTLED);
    ENUM_CASE(sync_pb::SyncEnums, DEPRECATED_AUTH_EXPIRED);
    ENUM_CASE(sync_pb::SyncEnums, DEPRECATED_USER_NOT_ACTIVATED);
    ENUM_CASE(sync_pb::SyncEnums, DEPRECATED_AUTH_INVALID);
    ENUM_CASE(sync_pb::SyncEnums, CLEAR_PENDING);
    ENUM_CASE(sync_pb::SyncEnums, TRANSIENT_ERROR);
    ENUM_CASE(sync_pb::SyncEnums, MIGRATION_DONE);
    ENUM_CASE(sync_pb::SyncEnums, DISABLED_BY_ADMIN);
    ENUM_CASE(sync_pb::SyncEnums, DEPRECATED_USER_ROLLBACK);
    ENUM_CASE(sync_pb::SyncEnums, PARTIAL_FAILURE);
    ENUM_CASE(sync_pb::SyncEnums, CLIENT_DATA_OBSOLETE);
    ENUM_CASE(sync_pb::SyncEnums, UNKNOWN);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(sync_pb::SyncEnums::Action action) {
  ASSERT_ENUM_BOUNDS(sync_pb::SyncEnums, Action, UPGRADE_CLIENT,
                     UNKNOWN_ACTION);
  switch (action) {
    ENUM_CASE(sync_pb::SyncEnums, UPGRADE_CLIENT);
    ENUM_CASE(sync_pb::SyncEnums, CLEAR_USER_DATA_AND_RESYNC);
    ENUM_CASE(sync_pb::SyncEnums, ENABLE_SYNC_ON_ACCOUNT);
    ENUM_CASE(sync_pb::SyncEnums, STOP_AND_RESTART_SYNC);
    ENUM_CASE(sync_pb::SyncEnums, DISABLE_SYNC_ON_CLIENT);
    ENUM_CASE(sync_pb::SyncEnums, UNKNOWN_ACTION);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(sync_pb::AppSpecifics::LaunchType launch_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::AppSpecifics, LaunchType, PINNED, WINDOW);
  switch (launch_type) {
    ENUM_CASE(sync_pb::AppSpecifics, PINNED);
    ENUM_CASE(sync_pb::AppSpecifics, REGULAR);
    ENUM_CASE(sync_pb::AppSpecifics, FULLSCREEN);
    ENUM_CASE(sync_pb::AppSpecifics, WINDOW);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::AutofillWalletSpecifics::WalletInfoType wallet_info_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::AutofillWalletSpecifics, WalletInfoType, UNKNOWN,
                     POSTAL_ADDRESS);
  switch (wallet_info_type) {
    ENUM_CASE(sync_pb::AutofillWalletSpecifics, UNKNOWN);
    ENUM_CASE(sync_pb::AutofillWalletSpecifics, MASKED_CREDIT_CARD);
    ENUM_CASE(sync_pb::AutofillWalletSpecifics, POSTAL_ADDRESS);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::WalletMetadataSpecifics::Type wallet_metadata_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::WalletMetadataSpecifics, Type, UNKNOWN, ADDRESS);
  switch (wallet_metadata_type) {
    ENUM_CASE(sync_pb::WalletMetadataSpecifics, UNKNOWN);
    ENUM_CASE(sync_pb::WalletMetadataSpecifics, CARD);
    ENUM_CASE(sync_pb::WalletMetadataSpecifics, ADDRESS);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::WalletMaskedCreditCard::WalletCardStatus wallet_card_status) {
  switch (wallet_card_status) {
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, VALID);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, EXPIRED);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::WalletMaskedCreditCard::WalletCardType wallet_card_type) {
  switch (wallet_card_type) {
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, UNKNOWN);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, AMEX);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, DISCOVER);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, JCB);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, MAESTRO);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, MASTER_CARD);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, SOLO);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, SWITCH);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, VISA);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::WalletMaskedCreditCard::WalletCardClass wallet_card_class) {
  switch (wallet_card_class) {
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, UNKNOWN_CARD_CLASS);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, CREDIT);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, DEBIT);
    ENUM_CASE(sync_pb::WalletMaskedCreditCard, PREPAID);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(sync_pb::SyncEnums::DeviceType device_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::SyncEnums, DeviceType, TYPE_WIN, TYPE_TABLET);
  switch (device_type) {
    ENUM_CASE(sync_pb::SyncEnums, TYPE_WIN);
    ENUM_CASE(sync_pb::SyncEnums, TYPE_MAC);
    ENUM_CASE(sync_pb::SyncEnums, TYPE_LINUX);
    ENUM_CASE(sync_pb::SyncEnums, TYPE_CROS);
    ENUM_CASE(sync_pb::SyncEnums, TYPE_OTHER);
    ENUM_CASE(sync_pb::SyncEnums, TYPE_PHONE);
    ENUM_CASE(sync_pb::SyncEnums, TYPE_TABLET);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(sync_pb::SessionTab::FaviconType favicon_type) {
  ASSERT_ENUM_BOUNDS(sync_pb::SessionTab, FaviconType, TYPE_WEB_FAVICON,
                     TYPE_WEB_FAVICON);
  switch (favicon_type) { ENUM_CASE(sync_pb::SessionTab, TYPE_WEB_FAVICON); }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(sync_pb::NigoriSpecifics::PassphraseType type) {
  ASSERT_ENUM_BOUNDS(sync_pb::NigoriSpecifics, PassphraseType,
                     IMPLICIT_PASSPHRASE, CUSTOM_PASSPHRASE);
  switch (type) {
    ENUM_CASE(sync_pb::NigoriSpecifics, IMPLICIT_PASSPHRASE);
    ENUM_CASE(sync_pb::NigoriSpecifics, KEYSTORE_PASSPHRASE);
    ENUM_CASE(sync_pb::NigoriSpecifics, FROZEN_IMPLICIT_PASSPHRASE);
    ENUM_CASE(sync_pb::NigoriSpecifics, CUSTOM_PASSPHRASE);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::SyncEnums::SingletonDebugEventType type) {
  ASSERT_ENUM_BOUNDS(sync_pb::SyncEnums, SingletonDebugEventType,
                     CONNECTION_STATUS_CHANGE, BOOTSTRAP_TOKEN_UPDATED);
  switch (type) {
    ENUM_CASE(sync_pb::SyncEnums, CONNECTION_STATUS_CHANGE);
    ENUM_CASE(sync_pb::SyncEnums, UPDATED_TOKEN);
    ENUM_CASE(sync_pb::SyncEnums, PASSPHRASE_REQUIRED);
    ENUM_CASE(sync_pb::SyncEnums, PASSPHRASE_ACCEPTED);
    ENUM_CASE(sync_pb::SyncEnums, INITIALIZATION_COMPLETE);
    ENUM_CASE(sync_pb::SyncEnums, STOP_SYNCING_PERMANENTLY);
    ENUM_CASE(sync_pb::SyncEnums, ENCRYPTION_COMPLETE);
    ENUM_CASE(sync_pb::SyncEnums, ACTIONABLE_ERROR);
    ENUM_CASE(sync_pb::SyncEnums, ENCRYPTED_TYPES_CHANGED);
    ENUM_CASE(sync_pb::SyncEnums, PASSPHRASE_TYPE_CHANGED);
    ENUM_CASE(sync_pb::SyncEnums, KEYSTORE_TOKEN_UPDATED);
    ENUM_CASE(sync_pb::SyncEnums, CONFIGURE_COMPLETE);
    ENUM_CASE(sync_pb::SyncEnums, BOOTSTRAP_TOKEN_UPDATED);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(sync_pb::TabNavigation::BlockedState state) {
  ASSERT_ENUM_BOUNDS(sync_pb::TabNavigation, BlockedState, STATE_ALLOWED,
                     STATE_BLOCKED);
  switch (state) {
    ENUM_CASE(sync_pb::TabNavigation, STATE_ALLOWED);
    ENUM_CASE(sync_pb::TabNavigation, STATE_BLOCKED);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(sync_pb::TabNavigation::PasswordState state) {
  ASSERT_ENUM_BOUNDS(sync_pb::TabNavigation, PasswordState,
                     PASSWORD_STATE_UNKNOWN, HAS_PASSWORD_FIELD);
  switch (state) {
    ENUM_CASE(sync_pb::TabNavigation, PASSWORD_STATE_UNKNOWN);
    ENUM_CASE(sync_pb::TabNavigation, NO_PASSWORD_FIELD);
    ENUM_CASE(sync_pb::TabNavigation, HAS_PASSWORD_FIELD);
  }
  NOTREACHED();
  return "";
}

const char* ProtoEnumToString(
    sync_pb::ReadingListSpecifics::ReadingListEntryStatus status) {
  ASSERT_ENUM_BOUNDS(sync_pb::ReadingListSpecifics, ReadingListEntryStatus,
                     UNREAD, UNSEEN);
  switch (status) {
    ENUM_CASE(sync_pb::ReadingListSpecifics, UNREAD);
    ENUM_CASE(sync_pb::ReadingListSpecifics, READ);
    ENUM_CASE(sync_pb::ReadingListSpecifics, UNSEEN);
  }
  NOTREACHED();
  return "";
}

#undef ASSERT_ENUM_BOUNDS
#undef ENUM_CASE

}  // namespace syncer
