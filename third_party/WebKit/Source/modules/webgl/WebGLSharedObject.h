/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebGLSharedObject_h
#define WebGLSharedObject_h

#include "bindings/core/v8/TraceWrapperMember.h"
#include "modules/webgl/WebGLObject.h"

namespace blink {

class WebGLContextGroup;
class WebGLRenderingContextBase;

// WebGLSharedObject is the base class for objects that can be shared by
// multiple WebGLRenderingContexts.
class WebGLSharedObject : public WebGLObject {
 public:
  WebGLContextGroup* contextGroup() const { return m_contextGroup; }

  virtual bool isBuffer() const { return false; }
  virtual bool isProgram() const { return false; }
  virtual bool isQuery() const { return false; }
  virtual bool isRenderbuffer() const { return false; }
  virtual bool isSampler() const { return false; }
  virtual bool isShader() const { return false; }
  virtual bool isSync() const { return false; }
  virtual bool isTexture() const { return false; }
  virtual bool isTransformFeedback() const { return false; }

  bool validate(const WebGLContextGroup* contextGroup,
                const WebGLRenderingContextBase*) const final;

  DECLARE_VIRTUAL_TRACE();

  DECLARE_VIRTUAL_TRACE_WRAPPERS();

 protected:
  explicit WebGLSharedObject(WebGLRenderingContextBase*);

  bool hasGroupOrContext() const final { return m_contextGroup; }

  uint32_t currentNumberOfContextLosses() const final;

  gpu::gles2::GLES2Interface* getAGLInterface() const final;

 private:
  TraceWrapperMember<WebGLContextGroup> m_contextGroup;
};

}  // namespace blink

#endif  // WebGLSharedObject_h
