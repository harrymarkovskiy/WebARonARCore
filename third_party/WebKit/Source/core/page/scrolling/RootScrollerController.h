// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RootScrollerController_h
#define RootScrollerController_h

#include "core/CoreExport.h"
#include "platform/heap/Handle.h"

namespace blink {

class Document;
class Element;
class PaintLayer;

// Manages the root scroller associated with a given document. The root
// scroller causes browser controls movement, overscroll effects and prevents
// chaining scrolls up further in the DOM. It can be set from script using
// document.setRootScroller.
//
// There are two notions of a root scroller in this class: m_rootScroller and
// m_effectiveRootScroller. The former is the Element that was set as the root
// scroller using document.setRootScroller. If the page didn't set a root
// scroller this will be nullptr. The "effective" root scroller is the current
// Node we're using internally to apply viewport scrolling actions.  Both these
// elements come from this controller's associated Document. The final "global"
// root scroller, the one whose scrolling hides browser controls, may be in a
// different frame.
//
// If the currently set m_rootScroller is a valid element to become the root
// scroller, it will be promoted to the effective root scroller. If it is not
// valid, the effective root scroller will fall back to the document Node. The
// rules for what makes an element a valid root scroller are set in
// isValidRootScroller(). The validity of the current root scroller is
// re-checked after each layout.
class CORE_EXPORT RootScrollerController
    : public GarbageCollected<RootScrollerController> {
 public:
  // Creates a RootScrollerController for the given document. Note: instances
  // of this class need to be made aware of layout updates.
  static RootScrollerController* create(Document&);

  DECLARE_TRACE();

  // Sets the element that will be used as the root scroller. This can be
  // nullptr, in which case we'll use the default element (documentElement) as
  // the effective root scroller.
  void set(Element*);

  // Returns the element currently set as the root scroller from script. This
  // differs from the effective root scroller since the set Element may not
  // currently be a valid root scroller. e.g. If the page sets an Element
  // with `display: none`, get() will return that element, even though the
  // effective root scroller will remain the document Node.
  Element* get() const;

  // This returns the Element that's actually being used to control viewport
  // actions right now. This is different from get() if a root scroller hasn't
  // been set, or if the set root scroller isn't currently a valid scroller.
  Node& effectiveRootScroller() const;

  // This class needs to be informed of changes in layout so that it can
  // determine if the current root scroller is still valid or if it must be
  // replaced by the default root scroller.
  void didUpdateLayout();

  // Returns the PaintLayer associated with the currently effective root
  // scroller.
  PaintLayer* rootScrollerPaintLayer() const;

  // Used to determine which Element should scroll the viewport.  This is
  // needed since Blink's scrolling machinery works on Elements whereas the
  // document *Node* also scrolls so we need to designate an element one
  // Element as the viewport scroller. Sadly, this is *not* the
  // document.scrollingElement in general.
  bool scrollsViewport(const Element&) const;

 private:
  RootScrollerController(Document&);

  // Ensures the effective root scroller is currently valid and replaces it
  // with the default if not.
  void recomputeEffectiveRootScroller();

  // Determines whether the given element meets the criteria to become the
  // effective root scroller.
  bool isValidRootScroller(const Element&) const;

  // The owning Document whose root scroller this object manages.
  WeakMember<Document> m_document;

  // The Element that was set from script as rootScroller for this Document.
  // Depending on its validity to be the root scroller (e.g. a display: none
  // element isn't a valid root scroller), this may not actually be the
  // Element being used as the root scroller.
  WeakMember<Element> m_rootScroller;

  // The Node currently being used as the root scroller in this Document.
  // If the m_rootScroller is valid this will point to it. Otherwise, it'll
  // use the document Node. It'll never be nullptr since the Document owns the
  // RootScrollerController.
  Member<Node> m_effectiveRootScroller;

  bool m_documentHasDocumentElement;
};

}  // namespace blink

#endif  // RootScrollerController_h
