/*
 * Copyright (C) 2004, 2006, 2009 Apple Inc. All rights reserved.
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

#ifndef TextIteratorTextState_h
#define TextIteratorTextState_h

#include "core/CoreExport.h"
#include "core/dom/Range.h"
#include "core/editing/iterators/ForwardsTextBuffer.h"
#include "wtf/text/WTFString.h"

namespace blink {

class LayoutText;

class CORE_EXPORT TextIteratorTextState {
  STACK_ALLOCATED();

 public:
  explicit TextIteratorTextState(bool emitsOriginalText);
  ~TextIteratorTextState() {}

  const String& string() const { return m_text; }

  int length() const { return m_textLength; }
  UChar characterAt(unsigned index) const;
  String substring(unsigned position, unsigned length) const;
  void appendTextToStringBuilder(StringBuilder&,
                                 unsigned position = 0,
                                 unsigned maxLength = UINT_MAX) const;

  void spliceBuffer(UChar,
                    Node* textNode,
                    Node* offsetBaseNode,
                    int textStartOffset,
                    int textEndOffset);
  void emitText(Node* textNode,
                LayoutText* layoutObject,
                int textStartOffset,
                int textEndOffset);
  void emitAltText(Node*);
  void updateForReplacedElement(Node* baseNode);
  void flushPositionOffsets() const;
  int positionStartOffset() const { return m_positionStartOffset; }
  int positionEndOffset() const { return m_positionEndOffset; }
  Node* positionNode() const { return m_positionNode; }
  bool hasEmitted() const { return m_hasEmitted; }
  UChar lastCharacter() const { return m_lastCharacter; }
  int textStartOffset() const { return m_textStartOffset; }
  void resetRunInformation() {
    m_positionNode = nullptr;
    m_textLength = 0;
  }

  void appendTextTo(ForwardsTextBuffer* output,
                    unsigned position,
                    unsigned lengthToAppend) const;

 private:
  int m_textLength;
  String m_text;

  // Used for whitespace characters that aren't in the DOM, so we can point at
  // them.
  // If non-zero, overrides m_text.
  UChar m_singleCharacterBuffer;

  // The current text and its position, in the form to be returned from the
  // iterator.
  Member<Node> m_positionNode;
  mutable Member<Node> m_positionOffsetBaseNode;
  mutable int m_positionStartOffset;
  mutable int m_positionEndOffset;

  // Used when deciding whether to emit a "positioning" (e.g. newline) before
  // any other content
  bool m_hasEmitted;
  UChar m_lastCharacter;
  bool m_emitsOriginalText;

  // Stores the length of :first-letter when we are at the remaining text.
  // Equals to 0 in all other cases.
  int m_textStartOffset;
};

}  // namespace blink

#endif  // TextIteratorTextState_h
