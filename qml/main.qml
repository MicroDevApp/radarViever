import QtQuick
import MapGlobeApp 1.0

Window {
    id: window
    width: 1100
    height: 750
    visible: true
    title: "MapGlobe"
    color: "#0c0f12"

    MapView {
        id: mapView
        anchors.fill: parent

        centerLongitude: 24.03   // окрестности Львова — для примера
        centerLatitude: 49.84
        zoomLevel: 6

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            property real lastX: 0
            property real lastY: 0

            onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y }
            onPositionChanged: (mouse) => {
                mapView.panPixels(mouse.x - lastX, mouse.y - lastY)
                lastX = mouse.x
                lastY = mouse.y
            }
            onWheel: (wheel) => {
                mapView.zoomBy(wheel.angleDelta.y > 0 ? 0.3 : -0.3)
            }
        }
    }

    Column {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        spacing: 2

        Text {
            text: "\u00A9 OpenStreetMap contributors \u00B7 elevation: open DEM data"
            color: "white"
            font.pixelSize: 11
            style: Text.Outline
            styleColor: "black"
        }
        Text {
            text: "zoom: " + mapView.zoomLevel.toFixed(1)
                  + "   globe: " + (mapView.globeBlend * 100).toFixed(0) + "%"
            color: "white"
            font.pixelSize: 11
            style: Text.Outline
            styleColor: "black"
        }
    }
}
