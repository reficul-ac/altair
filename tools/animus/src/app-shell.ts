export function renderAppShell(root: HTMLElement): void {
  root.innerHTML = `
  <main class="shell theme-grid">
    <nav class="workspace-tabs" aria-label="Workspace">
      <div class="workspace-brand" aria-label="Altair Animus">
        <span>Altair</span>
      </div>
      <button class="active" data-workspace="flight" type="button">Flight</button>
      <button data-workspace="dashboard" type="button">Dashboard</button>
      <button data-workspace="map" type="button">Map</button>
      <button data-workspace="inspector" type="button">Inspector</button>
      <button data-workspace="video" type="button">Video</button>
      <button data-workspace="plan" type="button">Plan</button>
      <button data-workspace="setup" type="button">Setup</button>
    </nav>
    <section class="viewport workspace-panel active" data-panel="flight">
      <canvas id="scene"></canvas>
      <div class="status-strip" id="status-strip"></div>
      <div class="status-detail hidden" id="status-detail"></div>
      <div class="topbar">
        <div class="segmented" aria-label="Camera mode">
          <button class="active" data-camera="chase" type="button">Chase</button>
          <button data-camera="orbit" type="button">Orbit</button>
          <button data-camera="top" type="button">Top</button>
          <button data-camera="side" type="button">Side</button>
          <button data-camera="fpv" type="button">FPV</button>
          <button data-camera="free" type="button">Free</button>
        </div>
        <div class="segmented" aria-label="HUD mode">
          <button class="active" data-hud="console" type="button">Console</button>
          <button data-hud="tactical" type="button">Tactical</button>
          <button data-hud="off" type="button">Off</button>
        </div>
        <button id="ortho-toggle" type="button">Ortho</button>
        <button id="camera-lock" type="button">Lock Camera</button>
        <button id="debug-toggle" type="button">Debug</button>
        <button id="theme-toggle" type="button">Grid</button>
        <button id="marker" type="button">Marker</button>
      </div>
      <div class="status" id="status">Disconnected</div>
      <div class="hud hud-console" id="hud-console">
        <div class="attitude" aria-hidden="true">
          <div class="attitude-sky"></div>
          <div class="attitude-ground"></div>
          <div class="attitude-bank" id="attitude-bank"></div>
          <div class="attitude-cross"></div>
        </div>
        <div class="heading-tape" id="heading-tape"></div>
        <div class="telemetry-row">
          <div><span id="heading-label">HDG</span><strong id="heading">--</strong></div>
          <div><span>ROLL</span><strong id="roll">--</strong></div>
          <div><span>PITCH</span><strong id="pitch">--</strong></div>
          <div><span>ALT</span><strong id="altitude">--</strong></div>
          <div><span>GS</span><strong id="groundspeed">--</strong></div>
          <div><span>AS</span><strong id="airspeed">--</strong></div>
          <div><span>VS</span><strong id="climb">--</strong></div>
        </div>
      </div>
      <div class="hud hud-tactical hidden" id="hud-tactical">
        <div class="tactical-tag left"><span>GS</span><strong id="tactical-gs">--</strong></div>
        <div class="tactical-tag right"><span>ALT</span><strong id="tactical-alt">--</strong></div>
        <canvas id="radar" width="180" height="180"></canvas>
      </div>
      <canvas class="ortho hidden" id="ortho" width="220" height="220"></canvas>
      <div class="debug hidden" id="debug">
        <h2>Debug</h2>
        <dl>
          <div><dt>FPS</dt><dd id="debug-fps">--</dd></div>
          <div><dt>Frame</dt><dd id="debug-frame">--</dd></div>
          <div><dt>Trail</dt><dd id="debug-trail">--</dd></div>
          <div><dt>Camera</dt><dd id="debug-camera">--</dd></div>
          <div><dt>Position</dt><dd id="debug-position">--</dd></div>
        </dl>
      </div>
      <div class="axis axis-east">E</div>
      <div class="axis axis-north">N</div>
    </section>
    <section class="workspace-panel map-workspace" data-panel="map">
      <div class="map-toolbar" aria-label="Map controls">
        <div class="segmented map-mode-controls" aria-label="Map mode">
          <button class="active" data-map-mode="2d" type="button">2D</button>
          <button data-map-mode="terrain-2d" type="button">Terrain 2D</button>
          <button data-map-mode="terrain-3d" type="button">Terrain 3D</button>
        </div>
        <button id="map-focus" type="button">Focus</button>
        <button id="map-zoom-in" type="button">+</button>
        <button id="map-zoom-out" type="button">-</button>
      </div>
      <canvas id="map-canvas" width="1200" height="760"></canvas>
      <canvas id="terrain-canvas" class="hidden" width="1200" height="760"></canvas>
    </section>
    <section class="workspace-panel dashboard-workspace" data-panel="dashboard">
      <div class="dashboard-toolbar">
        <div>
          <h1>Dashboard</h1>
          <p>Operator-selected telemetry and guarded controls</p>
        </div>
        <div class="dashboard-actions">
          <button id="dashboard-add" type="button">Add Widget</button>
          <button id="dashboard-import" type="button">Import</button>
          <button id="dashboard-export" type="button">Export</button>
          <button id="dashboard-reset" type="button">Reset Layout</button>
        </div>
      </div>
      <div id="dashboard-grid" class="dashboard-grid"></div>
      <aside id="dashboard-drawer" class="dashboard-drawer" aria-label="Add dashboard widget"></aside>
    </section>
    <section class="workspace-panel inspector-workspace" data-panel="inspector">
      <div class="inspector-grid">
        <section><h2>Messages</h2><input id="inspector-filter" type="search" placeholder="Filter messages" /><div class="inspector-header"><span>Name</span><span>Src</span><span>Rate</span><span>Count</span></div><div id="inspector-table"></div></section>
        <section><h2>Fields</h2><dl id="message-detail"></dl><div class="inspector-actions"><button id="inspector-log-export" type="button">Export Log</button><button id="inspector-export" type="button">Export CSV</button></div><div id="chart-fields" class="chart-fields"></div><canvas id="field-chart" width="520" height="180"></canvas></section>
      </div>
    </section>
    <section class="workspace-panel inspector-workspace" data-panel="video">
      <div class="analysis-grid">
        <section><h2>Camera Streams</h2><div id="camera-streams" class="tool-list"></div></section>
        <section><h2>Capture</h2><div class="command-grid"><button data-camera-action="capture" type="button">Capture</button><button data-camera-action="record-start" type="button">Record</button><button data-camera-action="record-stop" type="button">Stop</button><button data-camera-action="zoom" type="button">Zoom</button><button data-camera-action="focus" type="button">Focus</button></div><div id="camera-detail" class="tool-list"></div></section>
      </div>
    </section>
    <section class="workspace-panel inspector-workspace" data-panel="plan">
      <div class="analysis-grid">
        <section><h2>Mission</h2><div id="mission-list" class="tool-list"></div><div id="mission-validation" class="tool-list"></div><div class="inspector-actions"><button id="plan-add" type="button">Add WP</button><button id="plan-save" type="button">Save</button><button id="plan-restore" type="button">Load</button><button id="plan-upload" type="button">Upload</button><button id="plan-download" type="button">Download</button><button id="plan-clear" type="button">Clear</button></div><h2>Operations</h2><div id="mission-operations" class="tool-list"></div></section>
        <section><h2>Geofence / Rally / Terrain</h2><div id="fence-list" class="tool-list"></div><div class="command-grid"><button id="terrain-request" type="button">Terrain Check</button><button data-plan-tool="geofence" type="button">Fetch Fence</button><button data-plan-tool="rally" type="button">Fetch Rally</button></div></section>
      </div>
    </section>
    <section class="workspace-panel inspector-workspace" data-panel="setup">
      <div class="analysis-grid">
        <section><h2>Readiness</h2><div id="readiness-list" class="tool-list"></div><div class="command-grid" id="guarded-commands"></div><h2>Command History</h2><div id="command-history" class="tool-list command-history"></div></section>
        <section><h2>Parameters / Diagnostics</h2><div class="inspector-actions"><input id="parameter-filter" type="search" placeholder="Filter parameters" /><button id="parameter-refresh" type="button">Refresh</button></div><div id="parameter-list" class="tool-list"></div><h2>Onboard Logs</h2><div class="inspector-actions"><button id="onboard-log-list" type="button">List</button><button id="onboard-log-erase" type="button">Erase</button></div><div id="onboard-log-listing" class="tool-list"></div><div id="diagnostics-list" class="tool-list"></div></section>
      </div>
    </section>
    <aside class="metrics workspace-panel active" data-panel="session">
      <h1>Altair Live Debugger</h1>
      <section class="controls" aria-label="MAVLink controls">
        <label><span>MAVLink port</span><input id="listen-port" type="number" min="1" max="65535" value="14551" /></label>
        <label class="toggle"><input id="qgc" type="checkbox" checked /><span>Forward QGC</span></label>
        <p id="config">UDP 127.0.0.1:14551</p>
      </section>
      <section class="replay-controls" aria-label="Replay controls">
        <div class="replay-buttons">
          <button id="replay-open" type="button">Open</button>
          <button id="log-import" type="button">Import</button>
          <button id="log-download" type="button">Download</button>
          <button id="replay-play" type="button" disabled>Play</button>
          <button id="replay-reset" type="button">Reset</button>
        </div>
        <input id="replay-timeline" type="range" min="0" max="0.001" step="0.001" value="0" disabled />
        <div class="replay-secondary">
          <button id="replay-prev-marker" type="button">Prev</button>
          <select id="replay-speed" aria-label="Playback speed">
            <option value="0.25">0.25x</option>
            <option value="0.5">0.5x</option>
            <option value="1" selected>1x</option>
            <option value="2">2x</option>
            <option value="4">4x</option>
            <option value="8">8x</option>
          </select>
          <button id="replay-next-marker" type="button">Next</button>
        </div>
        <label class="toggle"><input id="sync-inspection" type="checkbox" checked /><span>Lock timestamp</span></label>
        <p id="replay-time">0:00 / 0:00 @ 1x</p>
        <p id="replay-meta">No replay loaded</p>
      </section>
      <div class="command-strip">
        <button id="pause" type="button">Pause</button>
        <button id="clear" type="button">Clear Trail</button>
        <button id="heading-mode" type="button">HDG</button>
      </div>
      <section class="vehicle-card">
        <span id="vehicle-type">Unknown vehicle</span>
        <strong id="vehicle-id">sys --</strong>
      </section>
      <section class="vehicle-switcher"><h2>Vehicles</h2><div id="vehicle-list"></div></section>
      <section class="vehicle-switcher"><h2>Compare</h2><div id="vehicle-comparison" class="vehicle-comparison"></div></section>
      <section class="vehicle-switcher"><h2>Analysis</h2><div id="analysis-summary" class="vehicle-comparison"></div></section>
      <section class="vehicle-switcher"><h2>Mock Link</h2><div class="replay-secondary"><input id="mock-count" type="number" min="1" max="32" value="3" /><button id="mock-start" type="button">Start</button></div><p id="mock-status">Offline maps and mock links idle</p></section>
      <dl>
        <div><dt>Heartbeat</dt><dd id="heartbeat">--</dd></div>
        <div><dt>Packet Age</dt><dd id="packet">--</dd></div>
        <div><dt>Arming</dt><dd id="armed">--</dd></div>
        <div><dt>Mode</dt><dd id="mode">--</dd></div>
        <div><dt>GPS</dt><dd id="gps">--</dd></div>
        <div><dt>Battery</dt><dd id="battery">--</dd></div>
        <div><dt>Mission</dt><dd id="mission">--</dd></div>
        <div><dt>Throttle</dt><dd id="throttle">--</dd></div>
        <div><dt>Lat / Lon</dt><dd id="latlon">--</dd></div>
        <div><dt>Position</dt><dd id="position">--</dd></div>
        <div><dt>Velocity</dt><dd id="velocity">--</dd></div>
        <div><dt>Status Text</dt><dd id="statustext">--</dd></div>
      </dl>
      <section class="event-section"><h2>Events</h2><ul id="event-log"></ul></section>
    </aside>
  </main>
`;
}
