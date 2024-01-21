/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

WI.SpreadsheetCSSStyleDeclarationSection = class SpreadsheetCSSStyleDeclarationSection extends WI.View
{
    constructor(delegate, style)
    {
        console.assert(style instanceof WI.CSSStyleDeclaration, style);

        let element = document.createElement("section");
        element.classList.add("spreadsheet-css-declaration");

        super(element);

        this._delegate = delegate || null;
        this._style = style;
        this._propertiesEditor = null;
        this._selectorElements = [];
        this._mediaElements = [];
        this._filterText = null;
        this._shouldFocusSelectorElement = false;
        this._wasEditing = false;

        this._isMousePressed = false;
        this._mouseDownIndex = NaN;
        this._mouseDownPoint = null;
        this._boundHandleWindowMouseMove = null;
    }

    // Public

    get style() { return this._style; }

    get editable()
    {
        return this._style.editable;
    }

    initialLayout()
    {
        super.initialLayout();

        this._headerElement = document.createElement("div");
        this._headerElement.classList.add("header");

        this._styleOriginView = new WI.StyleOriginView();
        this._headerElement.append(this._styleOriginView.element);

        this._selectorElement = document.createElement("span");
        this._selectorElement.classList.add("selector");
        this._selectorElement.addEventListener("mouseenter", this._highlightNodesWithSelector.bind(this));
        this._selectorElement.addEventListener("mouseleave", this._hideDOMNodeHighlight.bind(this));
        this._headerElement.append(this._selectorElement);

        this._openBrace = document.createElement("span");
        this._openBrace.classList.add("open-brace");
        this._openBrace.textContent = " {";
        this._headerElement.append(this._openBrace);

        if (this._style.selectorEditable) {
            this._selectorTextField = new WI.SpreadsheetSelectorField(this, this._selectorElement);
            this._selectorTextField.addEventListener(WI.SpreadsheetSelectorField.Event.StartedEditing, (event) => {
                this._headerElement.classList.add("editing-selector");
            });
            this._selectorTextField.addEventListener(WI.SpreadsheetSelectorField.Event.StoppedEditing, (event) => {
                this._headerElement.classList.remove("editing-selector");
            });

            this._selectorElement.tabIndex = 0;
        }

        this._propertiesEditor = new WI.SpreadsheetCSSStyleDeclarationEditor(this, this._style);
        this._propertiesEditor.element.classList.add("properties");
        this._propertiesEditor.addEventListener(WI.SpreadsheetCSSStyleDeclarationEditor.Event.FilterApplied, this._handleEditorFilterApplied, this);

        this._closeBrace = document.createElement("span");
        this._closeBrace.classList.add("close-brace");
        this._closeBrace.textContent = "}";

        this._element.append(this._createMediaHeader(), this._headerElement);
        this.addSubview(this._propertiesEditor);
        this._propertiesEditor.needsLayout();
        this._element.append(this._closeBrace);

        if (!this._style.editable)
            this._element.classList.add("locked");
        else if (!this._style.ownerRule)
            this._element.classList.add("selector-locked");

        this.element.addEventListener("mousedown", this._handleMouseDown.bind(this));

        if (this._style.editable) {
            this.element.addEventListener("click", this._handleClick.bind(this));

            new WI.KeyboardShortcut(WI.KeyboardShortcut.Modifier.CommandOrControl, "S", this._save.bind(this), this._element);
            new WI.KeyboardShortcut(WI.KeyboardShortcut.Modifier.CommandOrControl | WI.KeyboardShortcut.Modifier.Shift, "S", this._save.bind(this), this._element);
        }
    }

    layout()
    {
        super.layout();

        this._styleOriginView.update(this._style);
        this._renderSelector();

        if (this._shouldFocusSelectorElement)
            this.startEditingRuleSelector();
    }

    hidden()
    {
        this._propertiesEditor.hidden();
    }

    startEditingRuleSelector()
    {
        if (!this._selectorElement) {
            this._shouldFocusSelectorElement = true;
            return;
        }

        this._shouldFocusSelectorElement = false;

        if (this._style.selectorEditable)
            this._selectorTextField.startEditing();
        else
            this._propertiesEditor.startEditingFirstProperty();
    }

    highlightProperty(property)
    {
        // When navigating from the Computed panel to the Styles panel, the latter
        // could be empty. Layout all properties so they can be highlighted.
        if (!this.didInitialLayout)
            this.updateLayout();

        if (this._propertiesEditor.highlightProperty(property)) {
            this._element.scrollIntoView();
            return true;
        }

        return false;
    }

    // SpreadsheetSelectorField delegate

    spreadsheetSelectorFieldDidChange(direction)
    {
        let selectorText = this._selectorElement.textContent.trim();

        if (!selectorText || selectorText === this._style.ownerRule.selectorText)
            this._discardSelectorChange();
        else {
            this.dispatchEventToListeners(WI.SpreadsheetCSSStyleDeclarationSection.Event.SelectorWillChange);
            this._style.ownerRule.singleFireEventListener(WI.CSSRule.Event.SelectorChanged, this._renderSelector, this);
            this._style.ownerRule.selectorText = selectorText;
        }

        if (!direction) {
            // Don't do anything when it's a blur event.
            return;
        }

        if (direction === "forward")
            this._propertiesEditor.startEditingFirstProperty();
        else if (direction === "backward") {
            if (this._delegate.spreadsheetCSSStyleDeclarationSectionStartEditingAdjacentRule) {
                const delta = -1;
                this._delegate.spreadsheetCSSStyleDeclarationSectionStartEditingAdjacentRule(this, delta);
            } else
                this._propertiesEditor.startEditingLastProperty();
        }
    }

    spreadsheetSelectorFieldDidDiscard()
    {
        this._discardSelectorChange();
    }

    // SpreadsheetCSSStyleDeclarationEditor delegate

    spreadsheetCSSStyleDeclarationEditorStartEditingRuleSelector()
    {
        this.startEditingRuleSelector();
    }

    spreadsheetCSSStyleDeclarationEditorStartEditingAdjacentRule(propertiesEditor, delta)
    {
        if (!this._delegate)
            return;

        if (this._delegate.spreadsheetCSSStyleDeclarationSectionStartEditingAdjacentRule)
            this._delegate.spreadsheetCSSStyleDeclarationSectionStartEditingAdjacentRule(this, delta);
    }

    spreadsheetCSSStyleDeclarationEditorPropertyBlur(event, property)
    {
        if (!this._isMousePressed)
            this._propertiesEditor.deselectProperties();
    }

    spreadsheetCSSStyleDeclarationEditorPropertyMouseEnter(event, property)
    {
        if (this._isMousePressed) {
            let index = parseInt(property.element.dataset.propertyIndex);
            this._propertiesEditor.selectProperties(this._mouseDownIndex, index);
        }
    }

    spreadsheetCSSStyleDeclarationEditorSelectProperty(property)
    {
        if (this._delegate && this._delegate.spreadsheetCSSStyleDeclarationSectionSelectProperty)
            this._delegate.spreadsheetCSSStyleDeclarationSectionSelectProperty(property);
    }

    applyFilter(filterText)
    {
        this._filterText = filterText;

        if (!this.didInitialLayout)
            return;

        this._element.classList.remove(WI.GeneralStyleDetailsSidebarPanel.NoFilterMatchInSectionClassName);

        this._propertiesEditor.applyFilter(this._filterText);
    }

    // Private

    _discardSelectorChange()
    {
        // Re-render selector for syntax highlighting.
        this._renderSelector();
    }

    _renderSelector()
    {
        this._selectorElement.removeChildren();
        this._selectorElements = [];

        let appendSelector = (selector, matched) => {
            console.assert(selector instanceof WI.CSSSelector);

            let selectorElement = this._selectorElement.appendChild(document.createElement("span"));
            selectorElement.textContent = selector.text;

            if (matched)
                selectorElement.classList.add(WI.SpreadsheetCSSStyleDeclarationSection.MatchedSelectorElementStyleClassName);

            if (selector.specificity) {
                let specificity = selector.specificity.map((number) => number.toLocaleString());
                let tooltip = WI.UIString("Specificity: (%d, %d, %d)").format(...specificity);
                if (selector.dynamic) {
                    tooltip += "\n";
                    if (this._style.inherited)
                        tooltip += WI.UIString("Dynamically calculated for the parent element");
                    else
                        tooltip += WI.UIString("Dynamically calculated for the selected element");
                }
                selectorElement.title = tooltip;
            } else if (selector.dynamic) {
                let tooltip = WI.UIString("Specificity: No value for selected element");
                tooltip += "\n";
                tooltip += WI.UIString("Dynamically calculated for the selected element and did not match");
                selectorElement.title = tooltip;
            }

            this._selectorElements.push(selectorElement);
        };

        let appendSelectorTextKnownToMatch = (selectorText) => {
            let selectorElement = this._selectorElement.appendChild(document.createElement("span"));
            selectorElement.textContent = selectorText;
            selectorElement.classList.add(WI.SpreadsheetCSSStyleDeclarationSection.MatchedSelectorElementStyleClassName);
        };

        switch (this._style.type) {
        case WI.CSSStyleDeclaration.Type.Rule:
            console.assert(this._style.ownerRule);

            var selectors = this._style.ownerRule.selectors;
            var matchedSelectorIndices = this._style.ownerRule.matchedSelectorIndices;
            if (selectors.length) {
                for (let i = 0; i < selectors.length; ++i) {
                    appendSelector(selectors[i], matchedSelectorIndices.includes(i));
                    if (i < selectors.length - 1)
                        this._selectorElement.append(", ");
                }
            } else
                appendSelectorTextKnownToMatch(this._style.ownerRule.selectorText);

            break;

        case WI.CSSStyleDeclaration.Type.Inline:
            this._selectorElement.textContent = WI.UIString("Style Attribute", "CSS properties defined via HTML style attribute");
            this._selectorElement.classList.add("style-attribute");
            break;

        case WI.CSSStyleDeclaration.Type.Attribute:
            appendSelectorTextKnownToMatch(this._style.node.displayName);
            break;
        }

        if (this._filterText)
            this.applyFilter(this._filterText);
    }

    _createMediaHeader()
    {
        let mediaList = this._style.mediaList;
        if (!mediaList.length || (mediaList.length === 1 && (mediaList[0].text === "all" || mediaList[0].text === "screen")))
            return "";

        let mediaElement = document.createElement("div");
        mediaElement.classList.add("header-media");

        let mediaLabel = mediaElement.appendChild(document.createElement("div"));
        mediaLabel.className = "media-label";
        mediaLabel.append("@media ");

        this._mediaElements = mediaList.map((media, i) => {
            if (i)
                mediaLabel.append(", ");

            let span = mediaLabel.appendChild(document.createElement("span"));
            span.textContent = media.text;
            return span;
        });

        return mediaElement;
    }

    _save(event)
    {
        event.stop();

        if (this._style.type !== WI.CSSStyleDeclaration.Type.Rule) {
            // FIXME: Can't save CSS inside <style></style> <https://webkit.org/b/150357>
            InspectorFrontendHost.beep();
            return;
        }

        console.assert(this._style.ownerRule instanceof WI.CSSRule);
        console.assert(this._style.ownerRule.sourceCodeLocation instanceof WI.SourceCodeLocation);

        let sourceCode = this._style.ownerRule.sourceCodeLocation.sourceCode;
        if (sourceCode.type !== WI.Resource.Type.Stylesheet) {
            // FIXME: Can't save CSS inside style="" <https://webkit.org/b/150357>
            InspectorFrontendHost.beep();
            return;
        }

        let url;
        if (sourceCode.urlComponents.scheme === "data") {
            let mainResource = WI.networkManager.mainFrame.mainResource;
            if (mainResource.urlComponents.lastPathComponent.endsWith(".html"))
                url = mainResource.url.replace(/\.html$/, "-data.css");
            else {
                let pathDirectory = mainResource.url.slice(0, -mainResource.urlComponents.lastPathComponent.length);
                url = pathDirectory + "data.css";
            }
        } else
            url = sourceCode.url;

        const saveAs = event.shiftKey;
        WI.FileUtilities.save({url: url, content: sourceCode.content}, saveAs);
    }

    _handleMouseDown(event)
    {
        if (event.button !== 0)
            return;

        this._wasEditing = this._propertiesEditor.editing || document.activeElement === this._selectorElement;

        let propertyElement = event.target.closest(".property");
        if (!propertyElement)
            return;

        this._isMousePressed = true;

        // Disable text selection on mousemove.
        event.preventDefault();

        // Canceling mousedown event prevents blur event from firing on the previously focused element.
        if (this._wasEditing && document.activeElement)
            document.activeElement.blur();

        // Prevent name/value fields from editing when properties selected.
        window.addEventListener("click", this._handleWindowClick.bind(this), {capture: true, once: true});

        let propertyIndex = parseInt(propertyElement.dataset.propertyIndex);
        if (event.shiftKey && this._propertiesEditor.hasSelectedProperties())
            this._propertiesEditor.extendSelectedProperties(propertyIndex);
        else {
            this._propertiesEditor.deselectProperties();
            this._mouseDownPoint = WI.Point.fromEvent(event);
            if (!this._boundHandleWindowMouseMove)
                this._boundHandleWindowMouseMove = this._handleWindowMouseMove.bind(this);
            window.addEventListener("mousemove", this._boundHandleWindowMouseMove);
        }

        if (propertyElement.parentNode) {
            this._mouseDownIndex = propertyIndex;
            this._element.classList.add("selecting");
        } else
            this._stopSelection();
    }

    _handleWindowClick(event)
    {
        if (this._propertiesEditor.hasSelectedProperties()) {
            // Don't start editing name/value if there's selection.
            event.stop();
        }
        this._stopSelection();
    }

    _handleWindowMouseMove(event)
    {
        console.assert(this._mouseDownPoint);

        if (this._mouseDownPoint.distance(WI.Point.fromEvent(event)) < 8)
            return;

        if (!this._propertiesEditor.hasSelectedProperties()) {
            console.assert(!isNaN(this._mouseDownIndex));
            this._propertiesEditor.selectProperties(this._mouseDownIndex, this._mouseDownIndex);
        }

        window.removeEventListener("mousemove", this._boundHandleWindowMouseMove);
        this._mouseDownPoint = null;
    }

    _handleClick(event)
    {
        this._stopSelection();

        if (this._wasEditing || this._propertiesEditor.hasSelectedProperties())
            return;

        if (window.getSelection().type === "Range")
            return;

        event.stop();

        if (event.target.classList.contains(WI.SpreadsheetStyleProperty.StyleClassName)) {
            let propertyIndex = parseInt(event.target.dataset.propertyIndex);
            this._propertiesEditor.addBlankProperty(propertyIndex + 1);
            return;
        }

        if (event.target === this._headerElement || event.target === this._openBrace) {
            this._propertiesEditor.addBlankProperty(0);
            return;
        }

        if (event.target === this._element || event.target === this._closeBrace) {
            const appendAfterLast = -1;
            this._propertiesEditor.addBlankProperty(appendAfterLast);
        }
    }

    _stopSelection()
    {
        this._isMousePressed = false;
        this._mouseDownIndex = NaN;
        this._element.classList.remove("selecting");

        window.removeEventListener("mousemove", this._boundHandleWindowMouseMove);
        this._mouseDownPoint = null;
    }

    _highlightNodesWithSelector()
    {
        let node = this._style.node;

        if (!this._style.ownerRule) {
            WI.domManager.highlightDOMNode(node.id);
            return;
        }

        let selectorText = this._selectorElement.textContent.trim();
        if (node.frame)
            WI.domManager.highlightSelector(selectorText, node.frame.id);
        else
            WI.domManager.highlightSelector(selectorText);
    }

    _hideDOMNodeHighlight()
    {
        WI.domManager.hideDOMNodeHighlight();
    }

    _handleEditorFilterApplied(event)
    {
        let matchesMedia = false;
        for (let mediaElement of this._mediaElements) {
            mediaElement.classList.remove(WI.GeneralStyleDetailsSidebarPanel.FilterMatchSectionClassName);

            if (mediaElement.textContent.includes(this._filterText)) {
                mediaElement.classList.add(WI.GeneralStyleDetailsSidebarPanel.FilterMatchSectionClassName);
                matchesMedia = true;
            }
        }

        let matchesSelector = false;
        for (let selectorElement of this._selectorElements) {
            selectorElement.classList.remove(WI.GeneralStyleDetailsSidebarPanel.FilterMatchSectionClassName);

            if (selectorElement.textContent.includes(this._filterText)) {
                selectorElement.classList.add(WI.GeneralStyleDetailsSidebarPanel.FilterMatchSectionClassName);
                matchesSelector = true;
            }
        }

        let matches = event.data.matches || matchesMedia || matchesSelector;
        if (!matches)
            this._element.classList.add(WI.GeneralStyleDetailsSidebarPanel.NoFilterMatchInSectionClassName);

        this.dispatchEventToListeners(WI.SpreadsheetCSSStyleDeclarationSection.Event.FilterApplied, {matches});
    }
};

WI.SpreadsheetCSSStyleDeclarationSection.Event = {
    FilterApplied: "spreadsheet-css-style-declaration-section-filter-applied",
    SelectorWillChange: "spreadsheet-css-style-declaration-section-selector-will-change",
};

WI.SpreadsheetCSSStyleDeclarationSection.MatchedSelectorElementStyleClassName = "matched";
