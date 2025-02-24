// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser;

import android.support.test.filters.MediumTest;

import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.RetryOnFailure;
import org.chromium.content.common.ContentSwitches;

/**
 * Test suite for email address detection.
 */
@CommandLineFlags.Add({ContentSwitches.ENABLE_CONTENT_INTENT_DETECTION})
public class EmailAddressDetectionTest extends ContentDetectionTestBase {

    private static final String EMAIL_INTENT_PREFIX = "mailto:";

    private boolean isExpectedEmailIntent(String intentUrl, String expectedContent) {
        if (intentUrl == null) return false;
        final String expectedUrl = EMAIL_INTENT_PREFIX + urlForContent(expectedContent);
        return intentUrl.equals(expectedUrl);
    }

    @MediumTest
    @Feature({"ContentDetection", "TabContents"})
    @RetryOnFailure
    public void testValidEmailAddresses() throws Throwable {
        startActivityWithTestUrl("content/test/data/android/content_detection/email.html");

        // valid_1: i.want.a.pony@chromium.org.
        String intentUrl = scrollAndTapExpectingIntent("valid_1");
        assertTrue(isExpectedEmailIntent(intentUrl, "i.want.a.pony@chromium.org"));

        // valid_2: nyan_cat@chromium.org.
        intentUrl = scrollAndTapExpectingIntent("valid_2");
        assertTrue(isExpectedEmailIntent(intentUrl, "nyan_cat@chromium.org"));

        // valid_3: 123@456.com.
        intentUrl = scrollAndTapExpectingIntent("valid_3");
        assertTrue(isExpectedEmailIntent(intentUrl, "123@456.com"));
    }
}
