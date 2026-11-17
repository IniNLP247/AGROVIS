import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: guideRoot
    required property var theme

    color: theme.bgDeep

    Flickable {
        anchors.fill: parent
        anchors.margins: 24
        contentHeight: guideCol.implicitHeight
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: guideCol
            width: parent.width
            spacing: 16

            // Page Header
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: appCtrl.tr.guideTitle
                    color: theme.textPrimary
                    font.pixelSize: 20
                    font.weight: Font.Bold
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: appCtrl.tr.guideSubtitle
                    color: theme.textMuted
                    font.pixelSize: 13
                }
            }

            // Standard Color Legend Banner
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: keyCol.implicitHeight + 20
                radius: 8
                color: theme.bgPanel
                border.color: theme.border

                ColumnLayout {
                    id: keyCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Label {
                        text: appCtrl.tr.guideLegendTitle
                        color: theme.textMuted
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 18

                        Repeater {
                            model: [
                                { col: "#2ECC71", txt: appCtrl.tr.guideLegendGreen },
                                { col: "#EAB308", txt: appCtrl.tr.guideLegendYellow },
                                { col: "#EF4444", txt: appCtrl.tr.guideLegendRed },
                                { col: "#475569", txt: appCtrl.tr.guideLegendGray }
                            ]
                            delegate: Row {
                                spacing: 7
                                Rectangle {
                                    width: 14
                                    height: 14
                                    radius: 3
                                    color: modelData.col
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    text: modelData.txt
                                    color: theme.textPrimary
                                    font.pixelSize: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }
            }

            // Ordered 12-Method Reference Grid (exact same order as AnalysisInspector)
            GridLayout {
                Layout.fillWidth: true
                columns: guideRoot.width > 1060 ? 2 : 1
                columnSpacing: 14
                rowSpacing: 14

                Repeater {
                    model: appCtrl.indexMetaList
                    delegate: Rectangle {
                        readonly property bool isCurrent: (appCtrl.spectralIndex === modelData.indexPos)
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        implicitHeight: cardCol.implicitHeight + 24
                        radius: 8
                        color: isCurrent ? theme.accentTint : theme.bgPanel
                        border.color: isCurrent ? theme.accent : theme.border
                        border.width: 1

                        ColumnLayout {
                            id: cardCol
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Rectangle {
                                    width: 26
                                    height: 26
                                    radius: 5
                                    color: isCurrent ? theme.accent : theme.bgControl
                                    border.color: isCurrent ? theme.accent : theme.border
                                    Label {
                                        anchors.centerIn: parent
                                        text: modelData.orderNum
                                        color: isCurrent ? "#07130C" : theme.textPrimary
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Label {
                                        text: modelData.shortName
                                        color: isCurrent ? theme.accent : theme.textPrimary
                                        font.pixelSize: 15
                                        font.weight: Font.Bold
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: modelData.fullName
                                        color: theme.textMuted
                                        font.pixelSize: 11
                                    }
                                }

                                Rectangle {
                                    width: useLbl.implicitWidth + 18
                                    height: 28
                                    radius: 5
                                    color: isCurrent ? theme.accentDim : (useMa.containsMouse ? theme.bgHover : theme.bgControl)
                                    border.color: isCurrent ? theme.accent : theme.border

                                    Label {
                                        id: useLbl
                                        anchors.centerIn: parent
                                        text: isCurrent ? appCtrl.tr.guideActiveBadge : appCtrl.tr.guideUseBtn
                                        color: isCurrent ? "#FFFFFF" : theme.textPrimary
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }

                                    MouseArea {
                                        id: useMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            appCtrl.spectralIndex = modelData.indexPos;
                                            appCtrl.activeTab = 0;
                                        }
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label {
                                    text: appCtrl.tr.guideUseCaseLabel
                                    color: theme.accent
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: modelData.useCase
                                    color: theme.textPrimary
                                    font.pixelSize: 12
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label {
                                    text: appCtrl.tr.guideColorLabel
                                    color: theme.warn
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: modelData.colorMeaning
                                    color: theme.textMuted
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
