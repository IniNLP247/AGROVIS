import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: viewportRoot
    required property var theme

    color: theme.bgDeep

    property real zoomScale: 1.0
    property real panX: 0.0
    property real panY: 0.0
    property var probeData: ({ valid: false })

    function fitToView() {
        if (canvasArea.width <= 10 || canvasArea.height <= 10 || appCtrl.imageWidth <= 0) return;
        var availW = (appCtrl.viewMode === 1) ? (canvasArea.width - 24) / 2 : (canvasArea.width - 24);
        var availH = canvasArea.height - 24;
        var sx = availW / appCtrl.imageWidth;
        var sy = availH / appCtrl.imageHeight;
        zoomScale = Math.max(0.1, Math.min(sx, sy));
        panX = 0.0;
        panY = 0.0;
    }

    function updateProbeFromItemCoords(px, py) {
        var ix = Math.floor(px);
        var iy = Math.floor(py);
        if (ix >= 0 && iy >= 0 && ix < appCtrl.imageWidth && iy < appCtrl.imageHeight) {
            probeData = appCtrl.probeAtPixel(ix, iy);
        } else {
            probeData = ({ valid: false });
        }
    }

    Connections {
        target: appCtrl
        function onOutputUpdated() {
            if (Math.abs(panX) < 1 && Math.abs(panY) < 1 && zoomScale === 1.0) {
                viewportRoot.fitToView();
            }
        }
        function onUiStateChanged() {
            if (appCtrl.viewMode === 1) {
                viewportRoot.fitToView();
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Viewport Top Toolbar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: theme.bgPanel
            border.color: theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                // View Comparison Modes
                Row {
                    spacing: 4
                    Repeater {
                        model: ["Split Curtain", "Side-by-Side", "Overlay", "Index Only"]
                        delegate: Rectangle {
                            height: 28
                            width: vmLbl.implicitWidth + 16
                            radius: 4
                            color: appCtrl.viewMode === index ? theme.accentTint : (vmMa.containsMouse ? theme.bgHover : theme.bgCard)
                            border.color: appCtrl.viewMode === index ? theme.accent : theme.border
                            border.width: 1

                            Label {
                                id: vmLbl
                                anchors.centerIn: parent
                                text: modelData
                                color: appCtrl.viewMode === index ? theme.accent : theme.textMuted
                                font.pixelSize: 11
                                font.weight: appCtrl.viewMode === index ? Font.DemiBold : Font.Normal
                            }

                            MouseArea {
                                id: vmMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: appCtrl.viewMode = index
                            }
                        }
                    }
                }

                // Overlay Opacity Slider (visible in Overlay mode)
                RowLayout {
                    visible: appCtrl.viewMode === 2
                    spacing: 6
                    Label {
                        text: "Opacity " + Math.round(appCtrl.overlayOpacity * 100) + "%"
                        color: theme.textMuted
                        font.pixelSize: 11
                    }
                    Slider {
                        id: opacitySlider
                        Layout.preferredWidth: 100
                        from: 0.0
                        to: 1.0
                        value: appCtrl.overlayOpacity
                        onMoved: appCtrl.overlayOpacity = value
                        background: Rectangle {
                            x: opacitySlider.leftPadding
                            y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - height / 2
                            width: opacitySlider.availableWidth
                            height: 4
                            radius: 2
                            color: theme.bgControl
                            Rectangle {
                                width: opacitySlider.visualPosition * parent.width
                                height: parent.height
                                radius: 2
                                color: theme.accent
                            }
                        }
                        handle: Rectangle {
                            x: opacitySlider.leftPadding + opacitySlider.visualPosition * (opacitySlider.availableWidth - width)
                            y: opacitySlider.topPadding + opacitySlider.availableHeight / 2 - height / 2
                            width: 12
                            height: 12
                            radius: 6
                            color: theme.accent
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Colormap Picker
                Label {
                    text: "Palette"
                    color: theme.textMuted
                    font.pixelSize: 11
                }
                ComboBox {
                    id: cmapCombo
                    Layout.preferredWidth: 110
                    Layout.preferredHeight: 28
                    model: ["RdYlGn", "Viridis", "Turbo", "Grayscale"]
                    currentIndex: appCtrl.colormapKind
                    onActivated: appCtrl.colormapKind = currentIndex
                    contentItem: Label {
                        leftPadding: 8
                        text: cmapCombo.displayText
                        color: theme.textPrimary
                        font.pixelSize: 11
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: theme.bgControl
                        border.color: theme.border
                        border.width: 1
                    }
                }

                Rectangle { width: 1; height: 20; color: theme.border }

                // Zoom Controls
                Row {
                    spacing: 4
                    Repeater {
                        model: [
                            { txt: "Fit", act: "fit" },
                            { txt: "1:1", act: "one" },
                            { txt: "-", act: "out" },
                            { txt: "+", act: "in" }
                        ]
                        delegate: Rectangle {
                            width: modelData.txt.length > 1 ? 34 : 26
                            height: 28
                            radius: 4
                            color: zMa.containsMouse ? theme.bgHover : theme.bgControl
                            border.color: theme.border
                            border.width: 1

                            Label {
                                anchors.centerIn: parent
                                text: modelData.txt
                                color: theme.textPrimary
                                font.pixelSize: 11
                            }
                            MouseArea {
                                id: zMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (modelData.act === "fit") viewportRoot.fitToView();
                                    else if (modelData.act === "one") { viewportRoot.zoomScale = 1.0; viewportRoot.panX = 0; viewportRoot.panY = 0; }
                                    else if (modelData.act === "in") viewportRoot.zoomScale = Math.min(8.0, viewportRoot.zoomScale * 1.25);
                                    else if (modelData.act === "out") viewportRoot.zoomScale = Math.max(0.15, viewportRoot.zoomScale * 0.8);
                                }
                            }
                        }
                    }
                }

                Label {
                    text: Math.round(viewportRoot.zoomScale * 100) + "%"
                    color: theme.textDim
                    font.pixelSize: 11
                    Layout.preferredWidth: 38
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        // Interactive Image Canvas
        Item {
            id: canvasArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            onWidthChanged: if (Math.abs(viewportRoot.panX) < 1 && Math.abs(viewportRoot.panY) < 1) viewportRoot.fitToView()
            onHeightChanged: if (Math.abs(viewportRoot.panX) < 1 && Math.abs(viewportRoot.panY) < 1) viewportRoot.fitToView()

            // Background subtle grid
            Rectangle {
                anchors.fill: parent
                color: theme.bgDeep
            }

            // MODES 0 (Split Curtain), 2 (Overlay), 3 (Index Only): Single Synchronized Canvas
            Item {
                id: singleStage
                visible: appCtrl.viewMode !== 1
                width: appCtrl.imageWidth * viewportRoot.zoomScale
                height: appCtrl.imageHeight * viewportRoot.zoomScale
                x: (canvasArea.width - width) / 2 + viewportRoot.panX
                y: (canvasArea.height - height) / 2 + viewportRoot.panY

                Image {
                    id: baseImageSingle
                    anchors.fill: parent
                    visible: appCtrl.viewMode === 0 || appCtrl.viewMode === 2
                    source: "image://viewport/base?rev=" + appCtrl.imageRevision
                    smooth: viewportRoot.zoomScale < 2.0
                    cache: false
                }

                // Split Curtain clipped right pane
                Item {
                    visible: appCtrl.viewMode === 0
                    x: singleStage.width * appCtrl.splitRatio
                    y: 0
                    width: singleStage.width * (1.0 - appCtrl.splitRatio)
                    height: singleStage.height
                    clip: true

                    Image {
                        x: -parent.x
                        y: 0
                        width: singleStage.width
                        height: singleStage.height
                        source: "image://viewport/overlay?rev=" + appCtrl.imageRevision
                        smooth: viewportRoot.zoomScale < 2.0
                        cache: false
                    }
                }

                // Overlay / Index Only full layer
                Image {
                    anchors.fill: parent
                    visible: appCtrl.viewMode === 2 || appCtrl.viewMode === 3
                    opacity: appCtrl.viewMode === 2 ? appCtrl.overlayOpacity : 1.0
                    source: "image://viewport/overlay?rev=" + appCtrl.imageRevision
                    smooth: viewportRoot.zoomScale < 2.0
                    cache: false
                }

                // Pan + Wheel + Hover Pixel Probe MouseArea
                MouseArea {
                    id: stageMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    property real lastX: 0
                    property real lastY: 0
                    property bool panning: false

                    onPressed: function(mouse) {
                        lastX = mouse.x;
                        lastY = mouse.y;
                        panning = true;
                    }
                    onReleased: panning = false
                    onExited: viewportRoot.probeData = ({ valid: false })
                    onPositionChanged: function(mouse) {
                        if (panning) {
                            viewportRoot.panX += (mouse.x - lastX);
                            viewportRoot.panY += (mouse.y - lastY);
                        } else {
                            viewportRoot.updateProbeFromItemCoords(
                                mouse.x / viewportRoot.zoomScale,
                                mouse.y / viewportRoot.zoomScale);
                        }
                    }
                    onWheel: function(wheel) {
                        var factor = wheel.angleDelta.y > 0 ? 1.15 : 0.87;
                        viewportRoot.zoomScale = Math.max(0.15, Math.min(8.0, viewportRoot.zoomScale * factor));
                    }
                }

                // Draggable Split Curtain Handle
                Rectangle {
                    id: curtainLine
                    visible: appCtrl.viewMode === 0
                    x: singleStage.width * appCtrl.splitRatio - 1
                    y: 0
                    width: 2
                    height: singleStage.height
                    color: theme.accent

                    Rectangle {
                        width: 26
                        height: 26
                        radius: 13
                        anchors.centerIn: parent
                        color: theme.bgPanel
                        border.color: theme.accent
                        border.width: 2

                        Label {
                            anchors.centerIn: parent
                            text: "↔"
                            color: theme.accent
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }

                    MouseArea {
                        width: 24
                        height: parent.height
                        anchors.horizontalCenter: parent.horizontalCenter
                        cursorShape: Qt.SplitHCursor
                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                var globalPos = mapToItem(singleStage, mouse.x, 0);
                                appCtrl.splitRatio = globalPos.x / Math.max(1, singleStage.width);
                            }
                        }
                    }
                }

                // Corner Labels on Split Curtain
                Rectangle {
                    visible: appCtrl.viewMode === 0
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.margins: 10
                    width: lblLeft.implicitWidth + 14
                    height: 22
                    radius: 4
                    color: "#CC0B0F0E"
                    border.color: theme.border
                    Label {
                        id: lblLeft
                        anchors.centerIn: parent
                        text: "RGB BASE"
                        color: theme.textPrimary
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
                Rectangle {
                    visible: appCtrl.viewMode === 0
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 10
                    width: lblRight.implicitWidth + 14
                    height: 22
                    radius: 4
                    color: "#CC0B0F0E"
                    border.color: theme.accent
                    Label {
                        id: lblRight
                        anchors.centerIn: parent
                        text: appCtrl.activeIndexName
                        color: theme.accent
                        font.pixelSize: 10
                        font.bold: true
                    }
                }
            }

            // MODE 1: Synchronized Side-by-Side Comparison
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                visible: appCtrl.viewMode === 1

                Repeater {
                    model: [
                        { title: "TRUE COLOR / BASE", key: "base" },
                        { title: appCtrl.activeIndexName + " SPECTRAL MAP", key: "overlay" }
                    ]
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#080B0A"
                        border.color: theme.border
                        border.width: 1
                        clip: true

                        Item {
                            id: paneStage
                            width: appCtrl.imageWidth * viewportRoot.zoomScale
                            height: appCtrl.imageHeight * viewportRoot.zoomScale
                            x: (parent.width - width) / 2 + viewportRoot.panX
                            y: (parent.height - height) / 2 + viewportRoot.panY

                            Image {
                                anchors.fill: parent
                                source: "image://viewport/" + modelData.key + "?rev=" + appCtrl.imageRevision
                                smooth: viewportRoot.zoomScale < 2.0
                                cache: false
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                property real lastX: 0
                                property real lastY: 0
                                property bool panning: false
                                onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y; panning = true; }
                                onReleased: panning = false
                                onExited: viewportRoot.probeData = ({ valid: false })
                                onPositionChanged: function(mouse) {
                                    if (panning) {
                                        viewportRoot.panX += (mouse.x - lastX);
                                        viewportRoot.panY += (mouse.y - lastY);
                                    } else {
                                        viewportRoot.updateProbeFromItemCoords(
                                            mouse.x / viewportRoot.zoomScale,
                                            mouse.y / viewportRoot.zoomScale);
                                    }
                                }
                                onWheel: function(wheel) {
                                    var factor = wheel.angleDelta.y > 0 ? 1.15 : 0.87;
                                    viewportRoot.zoomScale = Math.max(0.15, Math.min(8.0, viewportRoot.zoomScale * factor));
                                }
                            }
                        }

                        Rectangle {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: 8
                            width: paneLbl.implicitWidth + 14
                            height: 22
                            radius: 4
                            color: "#CC0B0F0E"
                            border.color: theme.border
                            Label {
                                id: paneLbl
                                anchors.centerIn: parent
                                text: modelData.title
                                color: modelData.key === "overlay" ? theme.accent : theme.textPrimary
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                    }
                }
            }

            // Loading screen covering viewport while calibrating & aligning capture
            Rectangle {
                anchors.fill: parent
                visible: appCtrl.isLoading
                color: theme.bgDeep
                z: 20

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                }

                Label {
                    anchors.centerIn: parent
                    text: "Loading..."
                    color: theme.accent
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.0
                }
            }
        }

        // Bottom Pixel Inspector & Colorbar Legend HUD
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: theme.bgPanel
            border.color: theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 14

                // Live Pixel Probe Readout
                Label {
                    color: viewportRoot.probeData.valid ? theme.textPrimary : theme.textDim
                    font.pixelSize: 11
                    font.family: "monospace"
                    Layout.fillWidth: true
                    text: {
                        if (!viewportRoot.probeData.valid) {
                            return "Hover canvas to inspect per-band reflectance & index value";
                        }
                        var p = viewportRoot.probeData;
                        var tag = p.masked ? " [MASKED]" : "";
                        return "(" + p.x + "," + p.y + ")  " +
                               "B:" + p.blue.toFixed(2) + " G:" + p.green.toFixed(2) +
                               " R:" + p.red.toFixed(2) + " RE:" + p.redEdge.toFixed(2) +
                               " NIR:" + p.nir.toFixed(2) + "  |  " +
                               appCtrl.activeIndexName + ": " + p.indexValue.toFixed(3) + tag;
                    }
                }

                // Dynamic Colorbar Legend + Clamp Range
                RowLayout {
                    spacing: 8

                    TextField {
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 26
                        text: appCtrl.clampMin.toFixed(2)
                        color: theme.textPrimary
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            radius: 4
                            color: theme.bgControl
                            border.color: theme.border
                        }
                        onEditingFinished: {
                            var v = parseFloat(text);
                            if (!isNaN(v)) appCtrl.clampMin = v;
                        }
                    }

                    Rectangle {
                        width: 130
                        height: 12
                        radius: 3
                        border.color: theme.border
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop {
                                position: 0.0
                                color: appCtrl.colormapKind === 0 ? "#A51926"
                                     : appCtrl.colormapKind === 1 ? "#440154"
                                     : appCtrl.colormapKind === 2 ? "#30123B" : "#111111"
                            }
                            GradientStop {
                                position: 0.5
                                color: appCtrl.colormapKind === 0 ? "#F8E06E"
                                     : appCtrl.colormapKind === 1 ? "#21918C"
                                     : appCtrl.colormapKind === 2 ? "#A4FC3C" : "#888888"
                            }
                            GradientStop {
                                position: 1.0
                                color: appCtrl.colormapKind === 0 ? "#127C3A"
                                     : appCtrl.colormapKind === 1 ? "#FDE725"
                                     : appCtrl.colormapKind === 2 ? "#7A0403" : "#FFFFFF"
                            }
                        }
                    }

                    TextField {
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 26
                        text: appCtrl.clampMax.toFixed(2)
                        color: theme.textPrimary
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            radius: 4
                            color: theme.bgControl
                            border.color: theme.border
                        }
                        onEditingFinished: {
                            var v = parseFloat(text);
                            if (!isNaN(v)) appCtrl.clampMax = v;
                        }
                    }

                    Rectangle {
                        width: 44
                        height: 26
                        radius: 4
                        color: rstMa.containsMouse ? theme.bgHover : theme.bgControl
                        border.color: theme.border
                        Label {
                            anchors.centerIn: parent
                            text: "Auto"
                            color: theme.textMuted
                            font.pixelSize: 10
                        }
                        MouseArea {
                            id: rstMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: appCtrl.resetIndexClampDefaults()
                        }
                    }
                }
            }
        }
    }
}
