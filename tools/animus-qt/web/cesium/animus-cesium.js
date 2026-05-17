(function () {
  const vehicleEl = document.getElementById('vehicle');

  function render(vehicle) {
    if (!vehicle || !vehicleEl) return;
    vehicleEl.textContent = `${vehicle.id}: ${Number(vehicle.latDeg).toFixed(5)}, ${Number(vehicle.lonDeg).toFixed(5)} heading ${Number(vehicle.headingDeg).toFixed(0)} deg`;
  }

  if (typeof QWebChannel === 'undefined') {
    vehicleEl.textContent = 'Qt WebChannel unavailable';
    return;
  }

  new QWebChannel(qt.webChannelTransport, function (channel) {
    const bridge = channel.objects.cesiumBridge;
    if (!bridge) {
      vehicleEl.textContent = 'cesiumBridge object unavailable';
      return;
    }
    bridge.snapshot(render);
    bridge.latestVehicleChanged.connect(render);
  });
})();
