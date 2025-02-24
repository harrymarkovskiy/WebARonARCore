// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/test/scoped_views_test_helper.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/ime/input_method_initializer.h"
#include "ui/base/test/test_clipboard.h"
#include "ui/compositor/test/context_factories_for_test.h"
#include "ui/views/test/platform_test_helper.h"
#include "ui/views/test/test_views_delegate.h"
#include "ui/views/test/views_test_helper.h"

namespace views {

ScopedViewsTestHelper::ScopedViewsTestHelper()
    : ScopedViewsTestHelper(base::WrapUnique(new TestViewsDelegate)) {}

ScopedViewsTestHelper::ScopedViewsTestHelper(
    std::unique_ptr<TestViewsDelegate> views_delegate)
    : views_delegate_(std::move(views_delegate)),
      platform_test_helper_(PlatformTestHelper::Create()) {
  // The ContextFactory must exist before any Compositors are created.
  bool enable_pixel_output = false;
  ui::ContextFactory* context_factory = nullptr;
  ui::ContextFactoryPrivate* context_factory_private = nullptr;
  ui::InitializeContextFactoryForTests(enable_pixel_output, &context_factory,
                                       &context_factory_private);

  views_delegate_->set_context_factory(context_factory);
  views_delegate_->set_context_factory_private(context_factory_private);

  test_helper_.reset(ViewsTestHelper::Create(base::MessageLoopForUI::current(),
                                             context_factory,
                                             context_factory_private));
  platform_test_helper_->OnTestHelperCreated(test_helper_.get());
  test_helper_->SetUp();

  ui::InitializeInputMethodForTesting();
  ui::TestClipboard::CreateForCurrentThread();
}

ScopedViewsTestHelper::~ScopedViewsTestHelper() {
  ui::Clipboard::DestroyClipboardForCurrentThread();
  ui::ShutdownInputMethodForTesting();
  test_helper_->TearDown();
  test_helper_.reset();

  views_delegate_.reset();

  // The Mus PlatformTestHelper has state that is deleted by
  // ui::TerminateContextFactoryForTests().
  platform_test_helper_.reset();

  ui::TerminateContextFactoryForTests();
}

gfx::NativeWindow ScopedViewsTestHelper::GetContext() {
  return test_helper_->GetContext();
}

}  // namespace views
