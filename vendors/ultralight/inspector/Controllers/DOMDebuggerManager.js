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

WI.DOMDebuggerManager = class DOMDebuggerManager extends WI.Object
{
    constructor()
    {
        super();

        this._domBreakpointURLMap = new Multimap;
        this._domBreakpointFrameIdentifierMap = new Map;

        this._eventBreakpoints = [];

        this._urlBreakpoints = [];
        this._allRequestsBreakpointEnabledSetting = new WI.Setting("break-on-all-requests", false);

        this._allRequestsBreakpoint = new WI.URLBreakpoint(WI.URLBreakpoint.Type.Text, "", {
            disabled: !this._allRequestsBreakpointEnabledSetting.value,
        });

        WI.DOMBreakpoint.addEventListener(WI.DOMBreakpoint.Event.DisabledStateChanged, this._handleDOMBreakpointDisabledStateChanged, this);
        WI.EventBreakpoint.addEventListener(WI.EventBreakpoint.Event.DisabledStateChanged, this._handleEventBreakpointDisabledStateChanged, this);
        WI.URLBreakpoint.addEventListener(WI.URLBreakpoint.Event.DisabledStateChanged, this._handleURLBreakpointDisabledStateChanged, this);

        WI.domManager.addEventListener(WI.DOMManager.Event.NodeRemoved, this._nodeRemoved, this);
        WI.domManager.addEventListener(WI.DOMManager.Event.NodeInserted, this._nodeInserted, this);

        WI.networkManager.addEventListener(WI.NetworkManager.Event.MainFrameDidChange, this._mainFrameDidChange, this);

        WI.Frame.addEventListener(WI.Frame.Event.ChildFrameWasRemoved, this._childFrameWasRemoved, this);
        WI.Frame.addEventListener(WI.Frame.Event.MainResourceDidChange, this._mainResourceDidChange, this);

        let loadBreakpoints = async (constructor, objectStore, oldSettings, callback) => {
            for (let key of oldSettings) {
                let existingSerializedBreakpoints = WI.Setting.migrateValue(key);
                if (existingSerializedBreakpoints) {
                    for (let existingSerializedBreakpoint of existingSerializedBreakpoints)
                        await objectStore.putObject(constructor.deserialize(existingSerializedBreakpoint));
                }
            }

            let serializedBreakpoints = await objectStore.getAll();

            this._restoringBreakpoints = true;
            for (let serializedBreakpoint of serializedBreakpoints) {
                let breakpoint = constructor.deserialize(serializedBreakpoint);

                const key = null;
                objectStore.associateObject(breakpoint, key, serializedBreakpoint);

                callback(breakpoint);
            }
            this._restoringBreakpoints = false;
        };

        if (this.supported) {
            loadBreakpoints(WI.DOMBreakpoint, WI.objectStores.domBreakpoints, ["dom-breakpoints"], (breakpoint) => {
                this.addDOMBreakpoint(breakpoint);
            });

            loadBreakpoints(WI.EventBreakpoint, WI.objectStores.eventBreakpoints, ["event-breakpoints"], (breakpoint) => {
                this.addEventBreakpoint(breakpoint);
            });

            loadBreakpoints(WI.URLBreakpoint, WI.objectStores.urlBreakpoints, ["xhr-breakpoints", "url-breakpoints"], (breakpoint) => {
                this.addURLBreakpoint(breakpoint);
            });
        }
    }

    // Target

    initializeTarget(target)
    {
        if (target.DOMDebuggerAgent) {
            if (target === WI.assumingMainTarget() && target.mainResource)
                this._speculativelyResolveDOMBreakpointsForURL(target.mainResource.url);

            for (let breakpoint of this._eventBreakpoints) {
                if (!breakpoint.disabled)
                    this._updateEventBreakpoint(breakpoint, target);
            }

            for (let breakpoint of this._urlBreakpoints) {
                if (!breakpoint.disabled)
                    this._updateURLBreakpoint(breakpoint, target);
            }

            if (!this._allRequestsBreakpoint.disabled)
                this._updateURLBreakpoint(this._allRequestsBreakpoint, target);
        }
    }

    // Static

    static supportsEventBreakpoints()
    {
        return InspectorBackend.domains.DOMDebugger.setEventBreakpoint && InspectorBackend.domains.DOMDebugger.removeEventBreakpoint;
    }

    static supportsURLBreakpoints()
    {
        return InspectorBackend.domains.DOMDebugger.setURLBreakpoint && InspectorBackend.domains.DOMDebugger.removeURLBreakpoint;
    }

    // Public

    get supported()
    {
        return !!InspectorBackend.domains.DOMDebugger;
    }

    get allRequestsBreakpoint() { return this._allRequestsBreakpoint; }

    get domBreakpoints()
    {
        let mainFrame = WI.networkManager.mainFrame;
        if (!mainFrame)
            return [];

        let resolvedBreakpoints = [];
        let frames = [mainFrame];
        while (frames.length) {
            let frame = frames.shift();
            let domBreakpointNodeIdentifierMap = this._domBreakpointFrameIdentifierMap.get(frame.id);
            if (domBreakpointNodeIdentifierMap)
                resolvedBreakpoints = resolvedBreakpoints.concat(Array.from(domBreakpointNodeIdentifierMap.values()));

            frames.push(...frame.childFrameCollection);
        }

        return resolvedBreakpoints;
    }

    get eventBreakpoints() { return this._eventBreakpoints; }

    get urlBreakpoints() { return this._urlBreakpoints; }

    isBreakpointSpecial(breakpoint)
    {
        return breakpoint === this._allRequestsBreakpoint;
    }

    domBreakpointsForNode(node)
    {
        console.assert(node instanceof WI.DOMNode);

        if (!node || !node.frame)
            return [];

        let domBreakpointNodeIdentifierMap = this._domBreakpointFrameIdentifierMap.get(node.frame.id);
        if (!domBreakpointNodeIdentifierMap)
            return [];

        let breakpoints = domBreakpointNodeIdentifierMap.get(node.id);
        return breakpoints ? Array.from(breakpoints) : [];
    }

    domBreakpointsInSubtree(node)
    {
        console.assert(node instanceof WI.DOMNode);

        let breakpoints = [];

        if (node.children) {
            let children = Array.from(node.children);
            while (children.length) {
                let child = children.pop();
                if (child.children)
                    children = children.concat(child.children);
                breakpoints = breakpoints.concat(this.domBreakpointsForNode(child));
            }
        }

        return breakpoints;
    }

    addDOMBreakpoint(breakpoint)
    {
        console.assert(breakpoint instanceof WI.DOMBreakpoint);
        if (!breakpoint || !breakpoint.url)
            return;

        if (this.isBreakpointSpecial(breakpoint)) {
            this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.DOMBreakpointAdded, {breakpoint});
            return;
        }

        this._domBreakpointURLMap.add(breakpoint.url, breakpoint);

        if (breakpoint.domNodeIdentifier)
            this._resolveDOMBreakpoint(breakpoint, breakpoint.domNodeIdentifier);

        this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.DOMBreakpointAdded, {breakpoint});

        if (!this._restoringBreakpoints)
            WI.objectStores.domBreakpoints.putObject(breakpoint);
    }

    removeDOMBreakpoint(breakpoint)
    {
        console.assert(breakpoint instanceof WI.DOMBreakpoint);
        if (!breakpoint)
            return;

        if (this.isBreakpointSpecial(breakpoint)) {
            breakpoint.disabled = true;
            this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.DOMBreakpointRemoved, {breakpoint});
            return;
        }

        this._detachDOMBreakpoint(breakpoint);

        this._domBreakpointURLMap.delete(breakpoint.url);

        if (!breakpoint.disabled) {
            // We should get the target associated with the nodeIdentifier of this breakpoint.
            let target = WI.assumingMainTarget();
            target.DOMDebuggerAgent.removeDOMBreakpoint(breakpoint.domNodeIdentifier, breakpoint.type);
        }

        this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.DOMBreakpointRemoved, {breakpoint});

        breakpoint.domNodeIdentifier = null;

        if (!this._restoringBreakpoints)
            WI.objectStores.domBreakpoints.deleteObject(breakpoint);
    }

    removeDOMBreakpointsForNode(node)
    {
        this.domBreakpointsForNode(node).forEach(this.removeDOMBreakpoint, this);
    }

    eventBreakpointForTypeAndEventName(type, eventName)
    {
        return this._eventBreakpoints.find((breakpoint) => breakpoint.type === type && breakpoint.eventName === eventName) || null;
    }

    addEventBreakpoint(breakpoint)
    {
        console.assert(breakpoint instanceof WI.EventBreakpoint);
        if (!breakpoint)
            return;

        if (this.isBreakpointSpecial(breakpoint)) {
            this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.EventBreakpointAdded, {breakpoint});
            return;
        }

        if (this.eventBreakpointForTypeAndEventName(breakpoint.type, breakpoint.eventName))
            return;

        this._eventBreakpoints.push(breakpoint);

        this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.EventBreakpointAdded, {breakpoint});

        if (!breakpoint.disabled) {
            for (let target of WI.targets) {
                if (target.DOMDebuggerAgent)
                    this._updateEventBreakpoint(breakpoint, target);
            }
        }

        if (!this._restoringBreakpoints)
            WI.objectStores.eventBreakpoints.putObject(breakpoint);
    }

    removeEventBreakpoint(breakpoint)
    {
        console.assert(breakpoint instanceof WI.EventBreakpoint);
        if (!breakpoint)
            return;

        if (this.isBreakpointSpecial(breakpoint)) {
            breakpoint.disabled = true;
            this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.EventBreakpointRemoved, {breakpoint});
            return;
        }

        if (!this._eventBreakpoints.includes(breakpoint))
            return;

        this._eventBreakpoints.remove(breakpoint);

        if (!this._restoringBreakpoints)
            WI.objectStores.eventBreakpoints.deleteObject(breakpoint);

        this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.EventBreakpointRemoved, {breakpoint});

        if (breakpoint.disabled)
            return;

        for (let target of WI.targets) {
            if (target.DOMDebuggerAgent) {
                // Compatibility (iOS 12): DOMDebuggerAgent.removeEventBreakpoint did not exist.
                if (!WI.DOMDebuggerManager.supportsEventBreakpoints()) {
                    console.assert(breakpoint.type === WI.EventBreakpoint.Type.Listener);
                    target.DOMDebuggerAgent.removeEventListenerBreakpoint(breakpoint.eventName);
                    continue;
                }

                target.DOMDebuggerAgent.removeEventBreakpoint(breakpoint.type, breakpoint.eventName);
            }
        }
    }

    urlBreakpointForURL(url)
    {
        return this._urlBreakpoints.find((breakpoint) => breakpoint.url === url) || null;
    }

    addURLBreakpoint(breakpoint)
    {
        console.assert(breakpoint instanceof WI.URLBreakpoint);
        if (!breakpoint)
            return;

        if (this.isBreakpointSpecial(breakpoint)) {
            this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.URLBreakpointAdded, {breakpoint});
            return;
        }

        console.assert(!this._urlBreakpoints.includes(breakpoint), "Already added URL breakpoint.", breakpoint);
        if (this._urlBreakpoints.includes(breakpoint))
            return;

        if (this._urlBreakpoints.some((entry) => entry.type === breakpoint.type && entry.url === breakpoint.url))
            return;

        this._urlBreakpoints.push(breakpoint);

        this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.URLBreakpointAdded, {breakpoint});

        if (!breakpoint.disabled) {
            for (let target of WI.targets) {
                if (target.DOMDebuggerAgent)
                    this._updateURLBreakpoint(breakpoint, target);
            }
        }

        if (!this._restoringBreakpoints)
            WI.objectStores.urlBreakpoints.putObject(breakpoint);
    }

    removeURLBreakpoint(breakpoint)
    {
        console.assert(breakpoint instanceof WI.URLBreakpoint);
        if (!breakpoint)
            return;

        if (this.isBreakpointSpecial(breakpoint)) {
            breakpoint.disabled = true;
            this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.URLBreakpointRemoved, {breakpoint});
            return;
        }

        if (!this._urlBreakpoints.includes(breakpoint))
            return;

        this._urlBreakpoints.remove(breakpoint, true);

        if (!this._restoringBreakpoints)
            WI.objectStores.urlBreakpoints.deleteObject(breakpoint);

        this.dispatchEventToListeners(WI.DOMDebuggerManager.Event.URLBreakpointRemoved, {breakpoint});

        if (breakpoint.disabled)
            return;

        for (let target of WI.targets) {
            if (target.DOMDebuggerAgent) {
                // Compatibility (iOS 12.1): DOMDebuggerAgent.removeURLBreakpoint did not exist.
                if (WI.DOMDebuggerManager.supportsURLBreakpoints())
                    target.DOMDebuggerAgent.removeURLBreakpoint(breakpoint.url);
                else
                    target.DOMDebuggerAgent.removeXHRBreakpoint(breakpoint.url);
            }
        }
    }

    // Private

    _detachDOMBreakpoint(breakpoint)
    {
        let nodeIdentifier = breakpoint.domNodeIdentifier;
        let node = WI.domManager.nodeForId(nodeIdentifier);
        console.assert(node, "Missing DOM node for breakpoint.", breakpoint);
        if (!node || !node.frame)
            return;

        let frameIdentifier = node.frame.id;
        let domBreakpointNodeIdentifierMap = this._domBreakpointFrameIdentifierMap.get(frameIdentifier);
        console.assert(domBreakpointNodeIdentifierMap, "Missing DOM breakpoints for node parent frame.", node);
        if (!domBreakpointNodeIdentifierMap)
            return;

        domBreakpointNodeIdentifierMap.delete(nodeIdentifier, breakpoint);

        if (!domBreakpointNodeIdentifierMap.size)
            this._domBreakpointFrameIdentifierMap.delete(frameIdentifier);
    }

    _detachBreakpointsForFrame(frame)
    {
        let domBreakpointNodeIdentifierMap = this._domBreakpointFrameIdentifierMap.get(frame.id);
        if (!domBreakpointNodeIdentifierMap)
            return;

        this._domBreakpointFrameIdentifierMap.delete(frame.id);

        for (let breakpoint of domBreakpointNodeIdentifierMap.values())
            breakpoint.domNodeIdentifier = null;
    }

    _speculativelyResolveDOMBreakpointsForURL(url)
    {
        let domBreakpoints = this._domBreakpointURLMap.get(url);
        if (!domBreakpoints)
            return;

        for (let breakpoint of domBreakpoints) {
            if (breakpoint.domNodeIdentifier)
                continue;

            WI.domManager.pushNodeByPathToFrontend(breakpoint.path, (nodeIdentifier) => {
                if (breakpoint.domNodeIdentifier) {
                    // This breakpoint may have been resolved by a node being inserted before this
                    // callback is invoked.  If so, the `nodeIdentifier` should match, so don't try
                    // to resolve it again as it would've already been resolved.
                    console.assert(breakpoint.domNodeIdentifier === nodeIdentifier);
                    return;
                }

                if (nodeIdentifier)
                    this._resolveDOMBreakpoint(breakpoint, nodeIdentifier);
            });
        }
    }

    _resolveDOMBreakpoint(breakpoint, nodeIdentifier)
    {
        let node = WI.domManager.nodeForId(nodeIdentifier);
        console.assert(node, "Missing DOM node for nodeIdentifier.", nodeIdentifier);
        if (!node || !node.frame)
            return;

        let frameIdentifier = node.frame.id;
        let domBreakpointNodeIdentifierMap = this._domBreakpointFrameIdentifierMap.get(frameIdentifier);
        if (!domBreakpointNodeIdentifierMap) {
            domBreakpointNodeIdentifierMap = new Multimap;
            this._domBreakpointFrameIdentifierMap.set(frameIdentifier, domBreakpointNodeIdentifierMap);
        }

        domBreakpointNodeIdentifierMap.add(nodeIdentifier, breakpoint);

        breakpoint.domNodeIdentifier = nodeIdentifier;

        if (!breakpoint.disabled) {
            // We should get the target associated with the nodeIdentifier of this breakpoint.
            let target = WI.assumingMainTarget();
            if (target && target.DOMDebuggerAgent)
                this._updateDOMBreakpoint(breakpoint, target);
        }
    }

    _updateDOMBreakpoint(breakpoint, target)
    {
        console.assert(target.DOMDebuggerAgent);

        if (!breakpoint.domNodeIdentifier)
            return;

        if (breakpoint.disabled)
            target.DOMDebuggerAgent.removeDOMBreakpoint(breakpoint.domNodeIdentifier, breakpoint.type);
        else {
            if (!this._restoringBreakpoints && !WI.debuggerManager.breakpointsDisabledTemporarily)
                WI.debuggerManager.breakpointsEnabled = true;

            target.DOMDebuggerAgent.setDOMBreakpoint(breakpoint.domNodeIdentifier, breakpoint.type);
        }
    }

    _updateEventBreakpoint(breakpoint, target)
    {
        console.assert(target.DOMDebuggerAgent);

        // Compatibility (iOS 12): DOMDebuggerAgent.removeEventBreakpoint did not exist.
        if (!WI.DOMDebuggerManager.supportsEventBreakpoints()) {
            console.assert(breakpoint.type === WI.EventBreakpoint.Type.Listener);
            if (breakpoint.disabled)
                target.DOMDebuggerAgent.removeEventListenerBreakpoint(breakpoint.eventName);
            else {
                if (!this._restoringBreakpoints && !WI.debuggerManager.breakpointsDisabledTemporarily)
                    WI.debuggerManager.breakpointsEnabled = true;

                target.DOMDebuggerAgent.setEventListenerBreakpoint(breakpoint.eventName);
            }
            return;
        }

        if (breakpoint.disabled)
            target.DOMDebuggerAgent.removeEventBreakpoint(breakpoint.type, breakpoint.eventName);
        else {
            if (!this._restoringBreakpoints && !WI.debuggerManager.breakpointsDisabledTemporarily)
                WI.debuggerManager.breakpointsEnabled = true;

            target.DOMDebuggerAgent.setEventBreakpoint(breakpoint.type, breakpoint.eventName);
        }
    }

    _updateURLBreakpoint(breakpoint, target)
    {
        console.assert(target.DOMDebuggerAgent);

        // Compatibility (iOS 12.1): DOMDebuggerAgent.removeURLBreakpoint did not exist.
        if (!WI.DOMDebuggerManager.supportsURLBreakpoints()) {
            if (breakpoint.disabled)
                target.DOMDebuggerAgent.removeXHRBreakpoint(breakpoint.url);
            else {
                if (!this._restoringBreakpoints && !WI.debuggerManager.breakpointsDisabledTemporarily)
                    WI.debuggerManager.breakpointsEnabled = true;

                let isRegex = breakpoint.type === WI.URLBreakpoint.Type.RegularExpression;
                target.DOMDebuggerAgent.setXHRBreakpoint(breakpoint.url, isRegex);
            }
            return;
        }

        if (breakpoint.disabled)
            target.DOMDebuggerAgent.removeURLBreakpoint(breakpoint.url);
        else {
            if (!this._restoringBreakpoints && !WI.debuggerManager.breakpointsDisabledTemporarily)
                WI.debuggerManager.breakpointsEnabled = true;

            let isRegex = breakpoint.type === WI.URLBreakpoint.Type.RegularExpression;
            target.DOMDebuggerAgent.setURLBreakpoint(breakpoint.url, isRegex);
        }
    }

    _handleDOMBreakpointDisabledStateChanged(event)
    {
        let breakpoint = event.target;
        let target = WI.assumingMainTarget();
        if (target && target.DOMDebuggerAgent)
            this._updateDOMBreakpoint(breakpoint, target);

        if (!this._restoringBreakpoints)
            WI.objectStores.domBreakpoints.putObject(breakpoint);
    }

    _handleEventBreakpointDisabledStateChanged(event)
    {
        let breakpoint = event.target;

        // Specific event listener breakpoints are handled by `DOMManager`.
        if (breakpoint.eventListener)
            return;

        for (let target of WI.targets) {
            if (target.DOMDebuggerAgent)
                this._updateEventBreakpoint(breakpoint, target);
        }

        if (!this._restoringBreakpoints)
            WI.objectStores.eventBreakpoints.putObject(breakpoint);
    }

    _handleURLBreakpointDisabledStateChanged(event)
    {
        let breakpoint = event.target;

        if (breakpoint === this._allRequestsBreakpoint)
            this._allRequestsBreakpointEnabledSetting.value = !breakpoint.disabled;

        for (let target of WI.targets) {
            if (target.DOMDebuggerAgent)
                this._updateURLBreakpoint(breakpoint, target);
        }

        if (!this._restoringBreakpoints)
            WI.objectStores.urlBreakpoints.putObject(breakpoint);
    }

    _childFrameWasRemoved(event)
    {
        let frame = event.data.childFrame;
        this._detachBreakpointsForFrame(frame);
    }

    _mainFrameDidChange(event)
    {
        this._speculativelyResolveDOMBreakpointsForURL(WI.networkManager.mainFrame.url);
    }

    _mainResourceDidChange(event)
    {
        let frame = event.target;
        if (frame.isMainFrame()) {
            for (let breakpoint of this._domBreakpointURLMap.values())
                breakpoint.domNodeIdentifier = null;

            this._domBreakpointFrameIdentifierMap.clear();
        } else
            this._detachBreakpointsForFrame(frame);

        this._speculativelyResolveDOMBreakpointsForURL(frame.url);
    }

    _nodeInserted(event)
    {
        let node = event.data.node;
        if (node.nodeType() !== Node.ELEMENT_NODE || !node.frame)
            return;

        let url = node.frame.url;
        let breakpoints = this._domBreakpointURLMap.get(url);
        if (!breakpoints)
            return;

        for (let breakpoint of breakpoints) {
            if (breakpoint.domNodeIdentifier)
                continue;

            if (breakpoint.path !== node.path())
                continue;

            this._resolveDOMBreakpoint(breakpoint, node.id);
        }
    }

    _nodeRemoved(event)
    {
        let node = event.data.node;
        if (node.nodeType() !== Node.ELEMENT_NODE || !node.frame)
            return;

        let domBreakpointNodeIdentifierMap = this._domBreakpointFrameIdentifierMap.get(node.frame.id);
        if (!domBreakpointNodeIdentifierMap)
            return;

        let breakpoints = domBreakpointNodeIdentifierMap.get(node.id);
        if (!breakpoints)
            return;

        domBreakpointNodeIdentifierMap.delete(node.id);

        if (!domBreakpointNodeIdentifierMap.size)
            this._domBreakpointFrameIdentifierMap.delete(node.frame.id);

        for (let breakpoint of breakpoints)
            breakpoint.domNodeIdentifier = null;
    }
};

WI.DOMDebuggerManager.Event = {
    DOMBreakpointAdded: "dom-debugger-manager-dom-breakpoint-added",
    DOMBreakpointRemoved: "dom-debugger-manager-dom-breakpoint-removed",
    EventBreakpointAdded: "dom-debugger-manager-event-breakpoint-added",
    EventBreakpointRemoved: "dom-debugger-manager-event-breakpoint-removed",
    URLBreakpointAdded: "dom-debugger-manager-url-breakpoint-added",
    URLBreakpointRemoved: "dom-debugger-manager-url-breakpoint-removed",
};
