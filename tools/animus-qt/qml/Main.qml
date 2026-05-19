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
