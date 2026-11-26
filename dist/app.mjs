import { parseGpsLine } from './gps.mjs';
import { insideFence, trackFenceCrossings, validFence } from './geofence.mjs';

const ui = Object.fromEntries([...document.querySelectorAll('[id]')].map(element => [element.id, element]));
const leaflet = window.L;
const overview = [39, -96];
const map = leaflet?.map(ui.map).setView(overview, 4);
const mapNotice = ui['map-notice'];
if (map) {
  const basemaps = {
    street: leaflet.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
      maxZoom: 19,
      attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    }),
    satellite: leaflet.tileLayer('https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/tile/{z}/{y}/{x}', {
      maxZoom: 19,
      maxNativeZoom: 16,
      attribution: 'USDA, <a href="https://www.usgs.gov/programs/national-geospatial-program/national-map">USGS The National Map</a>',
    }),
  };
  const viewControl = ui['map-view'];
  const viewButtons = [...viewControl.querySelectorAll('button')];
  let activeView;

  function switchView(view) {
    if (view === activeView) return;
    if (activeView) basemaps[activeView].remove();
    activeView = view;
    mapNotice.hidden = true;
    basemaps[view].addTo(map);
    for (const button of viewButtons) {
      button.setAttribute('aria-pressed', String(button.value === view));
    }
  }

  for (const [view, tiles] of Object.entries(basemaps)) {
    tiles.on('load', () => {
      if (view !== activeView) return;
      mapNotice.hidden = Boolean(tiles.getContainer().querySelector('.leaflet-tile-loaded'));
      mapNotice.textContent = view === 'satellite'
        ? 'Satellite imagery is unavailable here. Switch to Street to try the street map.'
        : 'Map tiles are unavailable. GPS readings still work.';
    });
  }
  for (const button of viewButtons) {
    button.addEventListener('click', () => switchView(button.value));
  }
  switchView('street');
  viewControl.hidden = false;
} else {
  mapNotice.textContent = 'Map library could not load. GPS readings still work.';
  mapNotice.hidden = false;
}

const demonstration = [
  [41.91358, -87.63453], [41.91416, -87.63398], [41.91475, -87.63359],
  [41.91542, -87.63355], [41.91575, -87.63304], [41.91623, -87.63276],
];
const serialSupported = 'serial' in navigator;
const lines = [];
const trail = [];
let marker;
let polyline;
let lastFixAt = null;
let fixLost = false;
let demoTimer;
let active;
let setupNoticeAt = 0;
const fenceStorageKey = 'dog-tracker:geofence';
const fenceCrossings = trackFenceCrossings();
let fence = readFence();
let draftFence;
let fenceLayer;

function readFence() {
  try {
    const saved = JSON.parse(localStorage.getItem(fenceStorageKey));
    return validFence(saved) ? saved : undefined;
  } catch {
    return undefined;
  }
}

function fenceStatus(message, state = '') {
  const status = ui['fence-status'];
  if (status.textContent !== message) status.textContent = message;
  status.dataset.state = state;
}

function resetFenceMonitor() {
  fenceCrossings.reset();
  if (draftFence) return;
  if (!map) fenceStatus('The map needs to load before you can draw a geofence.');
  else if (fence) fenceStatus('Waiting for a fresh GPS position.');
  else fenceStatus('Draw a boundary on the map for on-screen alerts.');
}

function evaluateFence(point) {
  if (!fence || draftFence) return;
  const inside = insideFence(point, fence);
  if (demoTimer) {
    fenceStatus(inside ? 'Demo location is inside the fence.' : 'Demo location is outside the fence.');
    return;
  }
  const state = fenceCrossings.sample(inside, performance.now());
  const messages = {
    inside: 'Tracker is inside the fence.',
    checking: 'Checking a possible boundary crossing…',
    outside: 'Geofence alert: tracker is outside the boundary.',
  };
  fenceStatus(messages[state], state);
}

function renderFence() {
  const drawing = Boolean(draftFence);
  ui['draw-fence'].hidden = drawing;
  ui['draw-fence'].disabled = !map;
  ui['draw-fence'].textContent = fence ? 'Redraw fence' : 'Draw fence';
  for (const key of ['save-fence', 'undo-corner', 'cancel-fence']) ui[key].hidden = !drawing;
  ui['save-fence'].disabled = !draftFence || draftFence.length < 3;
  ui['undo-corner'].disabled = !draftFence?.length;
  for (const key of ['show-fence', 'remove-fence']) ui[key].hidden = drawing || !fence || !map;
  ui.map.dataset.drawing = drawing;
  fenceLayer?.remove();
  fenceLayer = undefined;

  const points = draftFence ?? fence;
  if (!map || !points?.length) return;
  const color = drawing ? '#265da6' : '#0a7453';
  const options = { color, fillOpacity: .12, weight: 3, interactive: false };
  fenceLayer = leaflet.layerGroup().addTo(map);
  if (points.length >= 3) leaflet.polygon(points, options).addTo(fenceLayer);
  else if (points.length === 2) leaflet.polyline(points, options).addTo(fenceLayer);
  if (drawing) {
    for (const point of points) leaflet.circleMarker(point, { ...options, radius: 4, fillOpacity: 1 }).addTo(fenceLayer);
  }
}

function finishFenceEdit() {
  draftFence = undefined;
  renderFence();
  resetFenceMonitor();
  const fresh = !fixLost && lastFixAt !== null && Date.now() - lastFixAt <= 10000;
  if ((demoTimer || (active && fresh)) && trail.length) evaluateFence(trail.at(-1));
}

ui['draw-fence'].addEventListener('click', () => {
  draftFence = [];
  fenceCrossings.reset();
  fenceStatus('Tap the map to add corners in boundary order. Add at least three.');
  renderFence();
});
map?.on('click', ({ latlng }) => {
  if (!draftFence) return;
  if (draftFence.length === 40) {
    fenceStatus('A fence can have up to 40 corners.');
    return;
  }
  const { lat, lng } = latlng.wrap();
  draftFence.push([lat, lng]);
  fenceStatus(`${draftFence.length} ${draftFence.length === 1 ? 'corner' : 'corners'}. Tap another corner or save the fence.`);
  renderFence();
});
ui['undo-corner'].addEventListener('click', () => {
  draftFence.pop();
  fenceStatus('Tap the map to add corners in boundary order. Add at least three.');
  renderFence();
});
ui['cancel-fence'].addEventListener('click', finishFenceEdit);
ui['save-fence'].addEventListener('click', () => {
  if (!validFence(draftFence)) {
    fenceStatus('Use distinct corners around a single boundary with no crossing edges.');
    return;
  }
  try {
    localStorage.setItem(fenceStorageKey, JSON.stringify(draftFence));
  } catch {
    fenceStatus('This browser could not save the fence. Check its site storage settings.');
    return;
  }
  fence = draftFence;
  finishFenceEdit();
});
ui['show-fence'].addEventListener('click', () => map.fitBounds(fence, { padding: [24, 24], maxZoom: 19 }));
ui['remove-fence'].addEventListener('click', () => {
  try {
    localStorage.removeItem(fenceStorageKey);
  } catch {
    fenceStatus('This browser could not remove the saved fence.');
    return;
  }
  fence = undefined;
  finishFenceEdit();
});
renderFence();
resetFenceMonitor();

function showConnection(source, message) {
  ui.source.textContent = source;
  ui.source.dataset.active = source !== 'Disconnected';
  ui.connection.textContent = message;
}

function clearTrack() {
  marker?.remove();
  polyline?.remove();
  marker = polyline = undefined;
  trail.length = lines.length = 0;
  lastFixAt = null;
  fixLost = false;
  resetFenceMonitor();
  for (const key of ['latitude', 'longitude', 'satellites', 'updated']) ui[key].textContent = '—';
  ui.fix.textContent = 'Waiting for a position.';
  ui.log.textContent = 'No data received.';
  ui.center.disabled = true;
  map?.setView(overview, 4);
}

function updateFixStatus() {
  if (setupNoticeAt && Date.now() - setupNoticeAt > 25000) clearWifiSetup();
  const stale = lastFixAt !== null && Date.now() - lastFixAt > 10000;
  const old = !demoTimer && (fixLost || stale);
  if (!demoTimer && (!active || old || lastFixAt === null)) resetFenceMonitor();
  marker?.setStyle({ fillColor: old || (!active && !demoTimer) ? '#a06a1b' : '#127550' });
  if (lastFixAt === null) {
    ui.fix.textContent = fixLost ? 'GPS is searching for satellites. Try a clear view of the sky.' : 'Waiting for a position.';
  } else if (demoTimer) {
    ui.fix.textContent = 'Simulated GPS fix';
  } else if (!active) {
    ui.fix.textContent = 'Last known position · USB disconnected';
  } else {
    ui.fix.textContent = old ? 'Last known position · awaiting a fresh fix' : 'GPS position received';
  }
}

function showPosition(reading) {
  if (reading.satellites !== null) ui.satellites.textContent = reading.satellites;
  fixLost = !reading.hasFix;
  if (reading.hasFix) {
    const { latitude, longitude } = reading;
    const point = [latitude, longitude];
    const lastPoint = trail.at(-1);
    if (!lastPoint || lastPoint[0] !== latitude || lastPoint[1] !== longitude) {
      trail.push(point);
      if (trail.length > 300) trail.shift();
      if (map && !marker) {
        polyline = leaflet.polyline(trail, { color: '#127550', weight: 4 }).addTo(map);
        marker = leaflet.circleMarker(point, { radius: 9, color: '#fff', weight: 3, fillOpacity: 1 }).addTo(map);
        map.setView(point, 16);
        ui.center.disabled = false;
      }
      polyline?.setLatLngs(trail);
      marker?.setLatLng(point);
    }
    lastFixAt = Date.now();
    ui.latitude.textContent = latitude.toFixed(6);
    ui.longitude.textContent = longitude.toFixed(6);
    ui.updated.textContent = new Date(lastFixAt).toLocaleTimeString();
    evaluateFence(point);
  }
  updateFixStatus();
}

function clearWifiSetup() {
  setupNoticeAt = 0;
  ui['wifi-setup'].hidden = true;
  ui['setup-network'].textContent = '';
  ui['setup-password'].textContent = '';
}

function handleLine(line) {
  const text = line.trim().slice(0, 512);
  if (!text) return;
  const setup = text.match(/SETUP Wi-Fi: (DogTracker-[0-9A-F]{6}) \| Password: ([0-9A-F]{16}) \| http:\/\/192\.168\.4\.1\/$/);
  if (setup) {
    setupNoticeAt = Date.now();
    ui['wifi-setup'].hidden = false;
    ui['setup-network'].textContent = setup[1];
    ui['setup-password'].textContent = setup[2];
    ui['device-status'].hidden = true;
  } else if (/^(?:Wi-Fi connected\.|OTA |SETUP (?:closed|could not start))/.test(text)) {
    if (text.startsWith('Wi-Fi connected.') || text.startsWith('SETUP ')) clearWifiSetup();
    ui['device-status'].textContent = text;
    ui['device-status'].hidden = false;
  }
  lines.push(setup ? 'Tracker Wi-Fi setup is available.' : text);
  if (lines.length > 40) lines.shift();
  ui.log.textContent = lines.join('\n');
  ui.log.scrollTop = ui.log.scrollHeight;
  const reading = parseGpsLine(text);
  if (reading) showPosition(reading);
}

function stopDemo() {
  clearInterval(demoTimer);
  demoTimer = undefined;
  ui.demo.textContent = 'Try demo';
}

ui.demo.addEventListener('click', () => {
  if (demoTimer) {
    stopDemo();
    clearTrack();
    showConnection('Disconnected', 'Plug in the tracker with a USB data cable or try the demo.');
    return;
  }
  clearTrack();
  showConnection('Demo', 'Showing a sample walk.');
  ui.demo.textContent = 'Stop demo';
  let step = 0;
  function advance() {
    const [latitude, longitude] = demonstration[step];
    showPosition({ hasFix: true, latitude, longitude, satellites: 9 });
    if (step === demonstration.length - 1) {
      clearInterval(demoTimer);
      ui.connection.textContent = 'Sample walk complete. Stop the demo to clear it.';
    } else {
      step += 1;
    }
  }
  demoTimer = setInterval(advance, 1700);
  advance();
});

ui.center.addEventListener('click', () => {
  if (marker) map.setView(marker.getLatLng(), Math.max(map.getZoom(), 16));
});

ui.connect.addEventListener('click', async () => {
  if (active) {
    active.stopping = true;
    ui.connect.disabled = true;
    await active.reader.cancel().catch(() => {});
    return;
  }
  stopDemo();
  clearTrack();
  ui.connect.disabled = ui.demo.disabled = true;
  let port;
  let reader;
  let opened = false;
  let message = 'USB disconnected. Connect again to resume.';
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    opened = true;
    await port.setSignals({ dataTerminalReady: true });
    reader = port.readable.getReader();
    active = { reader, stopping: false };
    ui.connect.textContent = 'Disconnect USB';
    ui.connect.disabled = false;
    showConnection('USB connected', 'Listening to the tracker at 115200 baud.');
    const decoder = new TextDecoder();
    let pending = '';
    while (true) {
      const { value, done } = await reader.read();
      if (done) break;
      const complete = (pending + decoder.decode(value, { stream: true })).split('\n');
      pending = complete.pop().slice(-1024);
      for (const line of complete) handleLine(line);
    }
  } catch (error) {
    if (!active?.stopping) {
      if (error.name === 'NotFoundError') message = 'No serial port selected. Connect USB to try again.';
      else if (opened) message = 'USB connection ended. Check the cable and reconnect.';
      else message = `Could not open the port. Close other serial tools and try again. (${error.message})`;
    }
  } finally {
    reader?.releaseLock();
    if (opened) await port.close().catch(() => {});
    active = undefined;
    clearWifiSetup();
    ui['device-status'].hidden = true;
    ui.connect.textContent = 'Connect USB';
    ui.connect.disabled = ui.demo.disabled = false;
    showConnection('Disconnected', message);
    updateFixStatus();
  }
});

if (!serialSupported) {
  ui.connect.disabled = true;
  showConnection('Disconnected', 'Open this page in desktop Chrome or Edge to connect over USB.');
}
setInterval(updateFixStatus, 1000);
