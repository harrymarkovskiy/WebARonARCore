/*
 * Copyright (C) 2005, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "core/html/forms/RadioInputType.h"

#include "core/HTMLNames.h"
#include "core/InputTypeNames.h"
#include "core/dom/Document.h"
#include "core/dom/ElementTraversal.h"
#include "core/events/KeyboardEvent.h"
#include "core/events/MouseEvent.h"
#include "core/html/HTMLFormElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/page/SpatialNavigation.h"
#include "platform/text/PlatformLocale.h"

namespace blink {

namespace {

HTMLInputElement* nextInputElement(const HTMLInputElement& element,
                                   const HTMLFormElement* stayWithin,
                                   bool forward) {
  return forward ? Traversal<HTMLInputElement>::next(element, stayWithin)
                 : Traversal<HTMLInputElement>::previous(element, stayWithin);
}

}  // namespace

using namespace HTMLNames;

InputType* RadioInputType::create(HTMLInputElement& element) {
  return new RadioInputType(element);
}

const AtomicString& RadioInputType::formControlType() const {
  return InputTypeNames::radio;
}

bool RadioInputType::valueMissing(const String&) const {
  return element().isInRequiredRadioButtonGroup() &&
         !element().checkedRadioButtonForGroup();
}

String RadioInputType::valueMissingText() const {
  return locale().queryString(
      WebLocalizedString::ValidationValueMissingForRadio);
}

void RadioInputType::handleClickEvent(MouseEvent* event) {
  event->setDefaultHandled();
}

HTMLInputElement* RadioInputType::findNextFocusableRadioButtonInGroup(
    HTMLInputElement* currentElement,
    bool forward) {
  for (HTMLInputElement* inputElement =
           nextRadioButtonInGroup(currentElement, forward);
       inputElement;
       inputElement = nextRadioButtonInGroup(inputElement, forward)) {
    if (inputElement->isFocusable())
      return inputElement;
  }
  return nullptr;
}

void RadioInputType::handleKeydownEvent(KeyboardEvent* event) {
  // TODO(tkent): We should return more earlier.
  if (!element().layoutObject())
    return;
  BaseCheckableInputType::handleKeydownEvent(event);
  if (event->defaultHandled())
    return;
  const String& key = event->key();
  if (key != "ArrowUp" && key != "ArrowDown" && key != "ArrowLeft" &&
      key != "ArrowRight")
    return;

  if (event->ctrlKey() || event->metaKey() || event->altKey())
    return;

  // Left and up mean "previous radio button".
  // Right and down mean "next radio button".
  // Tested in WinIE, and even for RTL, left still means previous radio button
  // (and so moves to the right). Seems strange, but we'll match it. However,
  // when using Spatial Navigation, we need to be able to navigate without
  // changing the selection.
  Document& document = element().document();
  if (isSpatialNavigationEnabled(document.frame()))
    return;
  bool forward = computedTextDirection() == TextDirection::kRtl
                     ? (key == "ArrowDown" || key == "ArrowLeft")
                     : (key == "ArrowDown" || key == "ArrowRight");

  // We can only stay within the form's children if the form hasn't been demoted
  // to a leaf because of malformed HTML.
  HTMLInputElement* inputElement = findNextFocusableRadioButtonInGroup(
      toHTMLInputElement(&element()), forward);
  if (!inputElement) {
    // Traverse in reverse direction till last or first radio button
    forward = !(forward);
    HTMLInputElement* nextInputElement = findNextFocusableRadioButtonInGroup(
        toHTMLInputElement(&element()), forward);
    while (nextInputElement) {
      inputElement = nextInputElement;
      nextInputElement =
          findNextFocusableRadioButtonInGroup(nextInputElement, forward);
    }
  }
  if (inputElement) {
    document.setFocusedElement(inputElement,
                               FocusParams(SelectionBehaviorOnFocus::Restore,
                                           WebFocusTypeNone, nullptr));
    inputElement->dispatchSimulatedClick(event, SendNoEvents);
    event->setDefaultHandled();
    return;
  }
}

void RadioInputType::handleKeyupEvent(KeyboardEvent* event) {
  const String& key = event->key();
  if (key != " ")
    return;
  // If an unselected radio is tabbed into (because the entire group has nothing
  // checked, or because of some explicit .focus() call), then allow space to
  // check it.
  if (element().checked())
    return;
  dispatchSimulatedClickIfActive(event);
}

bool RadioInputType::isKeyboardFocusable() const {
  if (!InputType::isKeyboardFocusable())
    return false;

  // When using Spatial Navigation, every radio button should be focusable.
  if (isSpatialNavigationEnabled(element().document().frame()))
    return true;

  // Never allow keyboard tabbing to leave you in the same radio group. Always
  // skip any other elements in the group.
  Element* currentFocusedElement = element().document().focusedElement();
  if (isHTMLInputElement(currentFocusedElement)) {
    HTMLInputElement& focusedInput = toHTMLInputElement(*currentFocusedElement);
    if (focusedInput.type() == InputTypeNames::radio &&
        focusedInput.form() == element().form() &&
        focusedInput.name() == element().name())
      return false;
  }

  // Allow keyboard focus if we're checked or if nothing in the group is
  // checked.
  return element().checked() || !element().checkedRadioButtonForGroup();
}

bool RadioInputType::shouldSendChangeEventAfterCheckedChanged() {
  // Don't send a change event for a radio button that's getting unchecked.
  // This was done to match the behavior of other browsers.
  return element().checked();
}

ClickHandlingState* RadioInputType::willDispatchClick() {
  // An event handler can use preventDefault or "return false" to reverse the
  // selection we do here.  The ClickHandlingState object contains what we need
  // to undo what we did here in didDispatchClick.

  // We want radio groups to end up in sane states, i.e., to have something
  // checked.  Therefore if nothing is currently selected, we won't allow the
  // upcoming action to be "undone", since we want some object in the radio
  // group to actually get selected.

  ClickHandlingState* state = new ClickHandlingState;

  state->checked = element().checked();
  state->checkedRadioButton = element().checkedRadioButtonForGroup();
  element().setChecked(true, DispatchChangeEvent);
  m_isInClickHandler = true;
  return state;
}

void RadioInputType::didDispatchClick(Event* event,
                                      const ClickHandlingState& state) {
  if (event->defaultPrevented() || event->defaultHandled()) {
    // Restore the original selected radio button if possible.
    // Make sure it is still a radio button and only do the restoration if it
    // still belongs to our group.
    HTMLInputElement* checkedRadioButton = state.checkedRadioButton.get();
    if (!checkedRadioButton)
      element().setChecked(false);
    else if (checkedRadioButton->type() == InputTypeNames::radio &&
             checkedRadioButton->form() == element().form() &&
             checkedRadioButton->name() == element().name())
      checkedRadioButton->setChecked(true);
  } else if (state.checked != element().checked()) {
    element().dispatchChangeEventIfNeeded();
  }
  m_isInClickHandler = false;
  // The work we did in willDispatchClick was default handling.
  event->setDefaultHandled();
}

bool RadioInputType::shouldAppearIndeterminate() const {
  return !element().checkedRadioButtonForGroup();
}

HTMLInputElement* RadioInputType::nextRadioButtonInGroup(
    HTMLInputElement* current,
    bool forward) {
  // TODO(tkent): Staying within form() is incorrect.  This code ignore input
  // elements associated by |form| content attribute.
  // TODO(tkent): Comparing name() with == is incorrect.  It should be
  // case-insensitive.
  for (HTMLInputElement* inputElement =
           nextInputElement(*current, current->form(), forward);
       inputElement; inputElement = nextInputElement(
                         *inputElement, current->form(), forward)) {
    if (current->form() == inputElement->form() &&
        inputElement->type() == InputTypeNames::radio &&
        inputElement->name() == current->name())
      return inputElement;
  }
  return nullptr;
}

}  // namespace blink
