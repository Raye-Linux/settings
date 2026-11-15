// SPDX-License-Identifier: GPL-3.0-or-later
// The right-hand side of the window: the page the router points at, on
// the layer card (8 px top-left radius against the navigation pane).
// A page opened from its parent drills in (it rises 40 px and fades in,
// 250 ms); going back fades (167 ms); switching category cross-fades
// (167 ms). All of it follows the user's animation speed.
import QtQuick
import org.raye.settings

Item {
    id: host

    // the text of "Search results for …", empty when not searching
    property string searchQuery: ""
    property var model: typeof categories !== "undefined" ? categories : null
    readonly property Item currentItem: _current ? _current.item : null

    // what is on screen now, to tell drill-in from back
    property string _shownKey: ""
    property Loader _current: null

    Accessible.role: Accessible.Pane

    function prefix(file) { return "qrc:/qt/qml/org/raye/settings/" + file }

    function categoryName(id) {
        if (!model)
            return ""
        const row = model.categoryOf(id)
        return row < 0 ? "" : model.data(model.index(row, 0), Qt.UserRole + 2)
    }

    // What to load for the route: { key, url, properties }
    function specFor(route, query) {
        if (query !== "")
            return { key: "search:" + query, url: prefix("search/SearchResultsPage.qml"), properties: { query: query } }
        if (route === "" || route === "home")
            return null
        const page = model ? model.pageOf(route) : ({})
        if (!page || !page.id) {
            // a category
            const row = model ? model.categoryOf(route) : -1
            const landing = row >= 0 ? model.data(model.index(row, 0), Qt.UserRole + 5) : ""
            if (landing && FrameSettings.hasComponent(landing))
                return { key: route, url: prefix(landing), properties: {} }
            return { key: route, url: prefix("frame/CategoryLandingPage.qml"), properties: { categoryId: route } }
        }
        const categoryId = route.split("/")[0]
        const crumbs = [{ text: categoryName(categoryId), target: categoryId }, { text: page.name, target: "" }]
        if (page.type === "kcm" && page.module)
            return { key: route, url: prefix("frame/KcmHost.qml"), properties: { kcm: page.module, crumbs: crumbs } }
        const component = Router.componentUrlOf(route)
        if (component === "" || component === "PlaceholderPage.qml")
            return { key: route, url: prefix("frame/PlaceholderHost.qml"),
                     properties: { crumbs: crumbs, pageName: page.name, summary: page.summary, iconName: page.icon } }
        return { key: route, url: prefix(component), properties: {} }
    }

    // "drill" (deeper), "back" (shallower) or "switch"
    function directionOf(from, to) {
        if (from === "" || from.indexOf("search:") === 0)
            return "drill"
        if (to.indexOf("search:") === 0)
            return "switch"
        if (to.indexOf(from + "/") === 0)
            return "drill"
        if (from.indexOf(to + "/") === 0)
            return "back"
        return from.split("/")[0] === to.split("/")[0] ? "drill" : "switch"
    }

    function refresh() {
        const spec = specFor(Router.route, searchQuery)
        if (!spec) {
            // the launch screen: the window hides this side
            if (_current) {
                _current.source = ""
                _current = null
            }
            _shownKey = ""
            return
        }
        if (spec.key === _shownKey)
            return
        const direction = directionOf(_shownKey, spec.key)
        const next = _current === loaderA ? loaderB : loaderA
        const previous = _current
        next.direction = direction
        next.setSource(spec.url, spec.properties)
        next.z = 1
        if (previous) {
            previous.z = 0
            previous.leave()
        }
        next.enter()
        _current = next
        _shownKey = spec.key
    }

    Connections {
        target: Router
        function onRouteChanged() {
            // a new place clears the search results
            if (host.searchQuery !== "")
                host.searchQuery = ""
            host.refresh()
        }
    }
    onSearchQueryChanged: refresh()
    Component.onCompleted: refresh()

    PageLoader { id: loaderA }
    PageLoader { id: loaderB }

    component PageLoader: Loader {
        id: loader
        property string direction: "drill"
        width: host.width
        height: host.height
        asynchronous: false
        focus: host._current === loader
        opacity: 0
        visible: opacity > 0

        function enter() {
            leaveAnimation.stop()
            opacity = 0
            y = direction === "drill" ? 40 : 0
            enterAnimation.restart()
        }
        function leave() {
            enterAnimation.stop()
            leaveAnimation.restart()
        }

        onLoaded: {
            // a link to "page#row": the page is new, so it did not hear the router
            if (Router.anchor !== "" && item && typeof item.scrollToAnchor === "function")
                Qt.callLater(item.scrollToAnchor, Router.anchor)
        }

        ParallelAnimation {
            id: enterAnimation
            NumberAnimation {
                target: loader; property: "opacity"; to: 1
                duration: loader.direction === "drill" ? Theme.durationNormal : Theme.durationFast
                easing.type: Theme.easingEnter
            }
            NumberAnimation {
                target: loader; property: "y"; to: 0
                duration: Theme.durationNormal
                easing.type: Theme.easingEnter
            }
        }
        SequentialAnimation {
            id: leaveAnimation
            NumberAnimation { target: loader; property: "opacity"; to: 0; duration: Theme.durationFaster; easing.type: Theme.easingExit }
            ScriptAction { script: loader.source = "" }
        }
    }
}
