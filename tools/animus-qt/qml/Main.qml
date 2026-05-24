import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Animus

ApplicationWindow {
    id: root
    width: 1280
    height: 820
    visible: true
    title: "Animus Qt"
    color: animusTheme.window
    palette.window: animusTheme.window
    palette.windowText: animusTheme.text
    palette.base: animusTheme.surface
    palette.alternateBase: animusTheme.window
    palette.text: animusTheme.text
    palette.button: animusTheme.surface
    palette.buttonText: animusTheme.text
    palette.highlight: animusTheme.accent
    palette.highlightedText: "white"

    function selectWorkspace(workspaceId) {
        return shell.selectWorkspace(workspaceId)
    }

    function workspaceChromeDiagnostics() {
        return shell.workspaceChromeDiagnostics()
    }

    function workspaceChromeDiagnosticsJson() {
        return shell.workspaceChromeDiagnosticsJson()
    }

    WorkspaceShell {
        id: shell
        anchors.fill: parent
    }
}
