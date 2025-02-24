// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/physical_web/physical_web_data_source_android.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/memory/ptr_util.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::android::JavaParamRef;

namespace {

const char kRequestUrl[] = "https://awesomesite.us/";
const double kDistanceEstimate = 42.0;
const int64_t kScanTimestamp = 10000000L;  // 10M
const char kSiteUrl[] = "https://awesomesite.us/page.html";
const char kIconUrl[] = "https://awesomesite.us/favicon.ico";
const char kTitle[] = "AwesomeSite";
const char kDescription[] = "An awesome site";
const char kGroupId[] = "14a8ef122";

}  // namespace

class PhysicalWebCollectionTest : public testing::Test {
 public:
  PhysicalWebCollectionTest() {}
  ~PhysicalWebCollectionTest() override {}

  void SetUp() override {
    collection_ = base::MakeUnique<PhysicalWebCollection>();
    env_ = base::android::AttachCurrentThread();
  }

  void TearDown() override {
    collection_.reset();
  }

  PhysicalWebCollection* Collection();
  JNIEnv* Env();
  jstring JavaString(const std::string& value);

 private:
  std::unique_ptr<PhysicalWebCollection> collection_;
  JNIEnv* env_;
};

PhysicalWebCollection* PhysicalWebCollectionTest::Collection() {
  DCHECK(collection_);
  return collection_.get();
}

JNIEnv* PhysicalWebCollectionTest::Env() {
  return env_;
}

jstring PhysicalWebCollectionTest::JavaString(const std::string& value) {
  return base::android::ConvertUTF8ToJavaString(Env(), value).Release();
}

TEST_F(PhysicalWebCollectionTest, AppendMetadataItem_ListWork) {
  Collection()->AppendMetadataItem(
      Env(),
      JavaParamRef<jobject>(NULL),
      JavaParamRef<jstring>(Env(), JavaString(kRequestUrl)),
      static_cast<jdouble>(kDistanceEstimate),
      static_cast<jlong>(kScanTimestamp),
      JavaParamRef<jstring>(Env(), JavaString(kSiteUrl)),
      JavaParamRef<jstring>(Env(), JavaString(kIconUrl)),
      JavaParamRef<jstring>(Env(), JavaString(kTitle)),
      JavaParamRef<jstring>(Env(), JavaString(kDescription)),
      JavaParamRef<jstring>(Env(), JavaString(kGroupId)));

  std::unique_ptr<physical_web::MetadataList> metadata_list =
      Collection()->GetMetadataList();
  EXPECT_EQ(1U, metadata_list->size());
  auto metadata = (*metadata_list.get())[0];
  EXPECT_EQ(kRequestUrl, metadata.scanned_url.spec());
  EXPECT_EQ(kDistanceEstimate, metadata.distance_estimate);
  EXPECT_EQ(kScanTimestamp, metadata.scan_timestamp.ToJavaTime());
  EXPECT_EQ(kSiteUrl, metadata.resolved_url.spec());
  EXPECT_EQ(kIconUrl, metadata.icon_url.spec());
  EXPECT_EQ(kTitle, metadata.title);
  EXPECT_EQ(kDescription, metadata.description);
  EXPECT_EQ(kGroupId, metadata.group_id);
}

TEST_F(PhysicalWebCollectionTest, AppendMetadataItem_DictionaryValueWorks) {
  Collection()->AppendMetadataItem(
      Env(),
      JavaParamRef<jobject>(NULL),
      JavaParamRef<jstring>(Env(), JavaString(kRequestUrl)),
      static_cast<jdouble>(kDistanceEstimate),
      static_cast<jlong>(kScanTimestamp),
      JavaParamRef<jstring>(Env(), JavaString(kSiteUrl)),
      JavaParamRef<jstring>(Env(), JavaString(kIconUrl)),
      JavaParamRef<jstring>(Env(), JavaString(kTitle)),
      JavaParamRef<jstring>(Env(), JavaString(kDescription)),
      JavaParamRef<jstring>(Env(), JavaString(kGroupId)));

  std::unique_ptr<base::ListValue> list_value = Collection()->GetMetadata();
  EXPECT_EQ(1U, list_value->GetSize());
  const base::Value* value;
  EXPECT_TRUE(list_value->Get(0, &value));
  const base::DictionaryValue* dictionary_value;
  EXPECT_TRUE(value->GetAsDictionary(&dictionary_value));
  std::string stored_scanned_url;
  EXPECT_TRUE(dictionary_value->GetString(physical_web::kScannedUrlKey,
                                          &stored_scanned_url));
  EXPECT_EQ(kRequestUrl, stored_scanned_url);
  double stored_distance_estimate;
  EXPECT_TRUE(dictionary_value->GetDouble(physical_web::kDistanceEstimateKey,
                                          &stored_distance_estimate));
  EXPECT_EQ(kDistanceEstimate, stored_distance_estimate);
  std::string stored_resolved_url;
  EXPECT_TRUE(dictionary_value->GetString(physical_web::kResolvedUrlKey,
                                          &stored_resolved_url));
  EXPECT_EQ(kSiteUrl, stored_resolved_url);
  std::string stored_icon_url;
  EXPECT_TRUE(dictionary_value->GetString(physical_web::kIconUrlKey,
                                          &stored_icon_url));
  EXPECT_EQ(kIconUrl, stored_icon_url);
  std::string stored_title;
  EXPECT_TRUE(dictionary_value->GetString(physical_web::kTitleKey,
                                          &stored_title));
  EXPECT_EQ(kTitle, stored_title);
  std::string stored_description;
  EXPECT_TRUE(dictionary_value->GetString(physical_web::kDescriptionKey,
                                          &stored_description));
  EXPECT_EQ(kDescription, stored_description);
  std::string stored_group_id;
  EXPECT_TRUE(dictionary_value->GetString(physical_web::kGroupIdKey,
                                          &stored_group_id));
  EXPECT_EQ(kGroupId, stored_group_id);
}
