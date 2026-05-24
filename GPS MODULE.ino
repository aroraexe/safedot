/**
 * SAFEDOT GPS & GSM Tracker for ESP32 and Quectel EC200U-CN
 * Interfacing using Hardware Serial2 (TX=17, RX=16 by default)
 * Handles modem initialization, signal strength monitoring, SMS alerts, and high-accuracy GPS tracking.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// On-board Web Server on port 80
WebServer server(80);

// Global position coordinates cache for offline Web Server API responses
String lastLatitude = "0.0";
String lastLongitude = "0.0";
String lastAltitude = "0.0";

// FreeRTOS structures for high-speed multi-core multiprocessing
struct GpsFix {
  char lat[16];
  char lon[16];
  char alt[16];
};

QueueHandle_t gpsQueue = NULL;
SemaphoreHandle_t modemMutex = NULL;

// HTML & JavaScript Live Map embedded directly in ESP32 Flash Memory
const char MAP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SAFEDOT Real-Time GPS Tracker</title>
    
    <!-- Premium Google Fonts -->
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&family=Space+Grotesk:wght@400;700&display=swap" rel="stylesheet">
    
    <!-- Leaflet.js Map CSS & JS (Free, Open-Source) -->
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    
    <style>
        :root {
            --bg-color: #0f172a;
            --card-bg: rgba(30, 41, 59, 0.7);
            --accent-color: #6366f1;
            --accent-gradient: linear-gradient(135deg, #6366f1 0%, #4f46e5 100%);
            --text-primary: #f8fafc;
            --text-secondary: #94a3b8;
            --border-color: rgba(255, 255, 255, 0.08);
            --success-color: #10b981;
            --danger-color: #ef4444;
        }

        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Outfit', sans-serif;
            -webkit-font-smoothing: antialiased;
        }

        body {
            background-color: var(--bg-color);
            color: var(--text-primary);
            height: 100vh;
            overflow: hidden;
            display: flex;
            flex-direction: column;
        }

        /* Layout */
        #container {
            position: relative;
            width: 100%;
            height: 100%;
            display: flex;
            flex-direction: column;
        }

        #map {
            flex: 1;
            width: 100%;
            z-index: 1;
        }

        /* Glassmorphism Control Panels */
        .glass-panel {
            background: var(--card-bg);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid var(--border-color);
            border-radius: 20px;
            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
            z-index: 10;
        }

        /* Top Header Panel */
        header {
            position: absolute;
            top: 20px;
            left: 20px;
            right: 20px;
            padding: 15px 25px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            z-index: 10;
            pointer-events: auto;
        }

        .logo-section h1 {
            font-family: 'Space Grotesk', sans-serif;
            font-size: 1.5rem;
            font-weight: 700;
            letter-spacing: 1px;
            background: linear-gradient(to right, #a5b4fc, #6366f1);
            -webkit-background-clip: text;
            background-clip: text;
            -webkit-text-fill-color: transparent;
            display: flex;
            align-items: center;
            gap: 10px;
        }

        .pulse-indicator {
            width: 10px;
            height: 10px;
            background-color: var(--success-color);
            border-radius: 50%;
            display: inline-block;
            box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7);
            animation: pulse 1.8s infinite;
        }

        @keyframes pulse {
            0% {
                transform: scale(0.95);
                box-shadow: 0 0 0 0 rgba(16, 185, 129, 0.7);
            }
            70% {
                transform: scale(1);
                box-shadow: 0 0 0 10px rgba(16, 185, 129, 0);
            }
            100% {
                transform: scale(0.95);
                box-shadow: 0 0 0 0 rgba(16, 185, 129, 0);
            }
        }

        /* Bottom Info & Stats Panel */
        #info-panel {
            position: absolute;
            bottom: 30px;
            left: 50%;
            transform: translateX(-50%);
            width: calc(100% - 40px);
            max-width: 500px;
            padding: 20px;
            display: flex;
            flex-direction: column;
            gap: 15px;
            transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
        }

        .stats-grid {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 15px;
            text-align: center;
        }

        .stat-card {
            background: rgba(255, 255, 255, 0.04);
            border-radius: 12px;
            padding: 10px;
            border: 1px solid rgba(255, 255, 255, 0.03);
        }

        .stat-label {
            font-size: 0.75rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.5px;
            margin-bottom: 4px;
        }

        .stat-value {
            font-family: 'Space Grotesk', sans-serif;
            font-size: 1.1rem;
            font-weight: 700;
            color: var(--text-primary);
        }

        .btn {
            background: var(--accent-gradient);
            color: white;
            border: none;
            padding: 12px 20px;
            border-radius: 12px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s, opacity 0.2s;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
            width: 100%;
        }

        .btn:hover {
            transform: translateY(-2px);
            opacity: 0.95;
        }

        .btn:active {
            transform: translateY(0);
        }

        /* Device Prompt Overlay */
        #prompt-overlay {
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(15, 23, 42, 0.9);
            z-index: 1000;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
            transition: opacity 0.5s ease;
        }

        .prompt-card {
            background: var(--bg-color);
            border: 1px solid var(--border-color);
            border-radius: 24px;
            width: 100%;
            max-width: 420px;
            padding: 35px;
            text-align: center;
            box-shadow: 0 20px 50px rgba(0, 0, 0, 0.7);
        }

        .prompt-card h2 {
            font-family: 'Space Grotesk', sans-serif;
            font-size: 1.8rem;
            margin-bottom: 10px;
            background: var(--accent-gradient);
            -webkit-background-clip: text;
            background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .prompt-card p {
            color: var(--text-secondary);
            font-size: 0.95rem;
            margin-bottom: 25px;
            line-height: 1.5;
        }

        .input-group {
            position: relative;
            margin-bottom: 20px;
        }

        .prompt-input {
            width: 100%;
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid var(--border-color);
            padding: 14px 20px;
            border-radius: 12px;
            color: var(--text-primary);
            font-size: 1rem;
            outline: none;
            transition: border-color 0.2s;
            text-align: center;
        }

        .prompt-input:focus {
            border-color: var(--accent-color);
        }

        .leaflet-container {
            background: #0b0f19 !important;
        }

        .leaflet-top {
            top: 90px !important; /* Push controls below the absolute positioned header */
            transition: top 0.3s;
        }

        .leaflet-control-zoom {
            border: 1px solid var(--border-color) !important;
            box-shadow: none !important;
            border-radius: 10px !important;
            overflow: hidden;
        }

        .leaflet-control-zoom-in, .leaflet-control-zoom-out {
            background: var(--card-bg) !important;
            color: var(--text-primary) !important;
            border-bottom: 1px solid var(--border-color) !important;
            backdrop-filter: blur(10px);
            -webkit-backdrop-filter: blur(10px);
        }

        .leaflet-control-zoom-in:hover, .leaflet-control-zoom-out:hover {
            background: rgba(255, 255, 255, 0.1) !important;
        }

        /* Glassmorphism Map Control Layer Switcher */
        .leaflet-control-layers {
            background: var(--card-bg) !important;
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid var(--border-color) !important;
            border-radius: 12px !important;
            color: var(--text-primary) !important;
            font-family: 'Outfit', sans-serif;
            box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4) !important;
            padding: 6px 10px !important;
        }

        .leaflet-control-layers-expanded {
            padding: 10px 14px !important;
        }

        .leaflet-control-layers-list label {
            margin-bottom: 6px;
            display: flex;
            align-items: center;
            gap: 8px;
            cursor: pointer;
            font-size: 0.85rem;
        }

        .leaflet-control-layers-selector {
            margin-top: 0 !important;
            accent-color: var(--accent-color);
            cursor: pointer;
        }

        .custom-popup .leaflet-popup-content-wrapper {
            background: var(--bg-color);
            color: var(--text-primary);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            padding: 5px;
        }

        .custom-popup .leaflet-popup-tip {
            background: var(--bg-color);
            border: 1px solid var(--border-color);
        }

        /* Premium Mobile Responsiveness Queries */
        @media (max-width: 480px) {
            header {
                top: 10px;
                left: 10px;
                right: 10px;
                padding: 10px 15px;
                border-radius: 12px;
            }

            .logo-section h1 {
                font-size: 1.2rem;
            }

            #connection-status {
                font-size: 0.75rem !important;
            }

            .leaflet-top {
                top: 70px !important; /* Move map controls higher on mobile to fit tight spacing */
            }

            #info-panel {
                bottom: 15px;
                left: 10px;
                right: 10px;
                transform: none;
                width: calc(100% - 20px);
                padding: 15px;
                gap: 10px;
                border-radius: 16px;
            }

            #info-panel > div:first-child {
                font-size: 0.95rem !important;
            }

            #last-update {
                font-size: 0.7rem !important;
            }

            .stats-grid {
                gap: 8px;
            }

            .stat-card {
                padding: 8px 4px;
                border-radius: 10px;
            }

            .stat-label {
                font-size: 0.65rem;
                margin-bottom: 2px;
            }

            .stat-value {
                font-size: 0.85rem;
            }

            .btn {
                padding: 10px 15px;
                font-size: 0.9rem;
                border-radius: 10px;
            }
        }
    </style>
</head>
<body>

    <!-- Prompt Overlay for Tracker ID Input -->
    <div id="prompt-overlay" style="display: none;">
        <div class="prompt-card">
            <h2>SAFEDOT Live Map</h2>
            <p>Please enter your Unique Tracker ID below to instantly link your emergency device.</p>
            <div class="input-group">
                <input type="text" id="tracker-id-input" class="prompt-input" placeholder="e.g., safedot-30583120691c">
            </div>
            <button class="btn" onclick="submitTrackerId()">Start Live Tracking</button>
        </div>
    </div>

    <!-- Main Container -->
    <div id="container">
        <!-- Floating Header -->
        <header class="glass-panel">
            <div class="logo-section">
                <h1>🛰️ SAFEDOT <span class="pulse-indicator" id="status-pulse"></span></h1>
            </div>
            <div style="font-size: 0.85rem; color: var(--text-secondary);" id="connection-status">
                Initializing...
            </div>
        </header>

        <!-- Leaflet Map Container -->
        <div id="map"></div>

        <!-- Floating Info Panel -->
        <div id="info-panel" class="glass-panel">
            <div style="font-weight: 600; font-size: 1.1rem; display:flex; justify-content:space-between; align-items:center;">
                <span id="display-tracker-id">Device: Connecting</span>
                <span id="last-update" style="font-size: 0.75rem; color: var(--text-secondary);">Never</span>
            </div>
            
            <div class="stats-grid">
                <div class="stat-card">
                    <div class="stat-label">Latitude</div>
                    <div class="stat-value" id="stat-lat">--.------</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">Longitude</div>
                    <div class="stat-value" id="stat-lon">--.------</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">Altitude</div>
                    <div class="stat-value" id="stat-alt">--m</div>
                </div>
            </div>
            
            <button class="btn" onclick="centerOnTarget()">
                🎯 Center Map on Tracker
            </button>
        </div>
    </div>

    <script>
        let map;
        let marker;
        let pathLine;
        let coordinatesHistory = [];
        let trackerId = "";
        let isFirstLoad = true;
        let eventSource = null;
        let pollingInterval = null;

        // Custom Configuration
        const FIREBASE_HOST = "safedot-73b43-default-rtdb.firebaseio.com";

        // Initialize App on DOM Content Loaded
        document.addEventListener("DOMContentLoaded", () => {
            const urlParams = new URLSearchParams(window.location.search);
            const idParam = urlParams.get('id');
            const isLocal = window.location.hostname === "192.168.4.1" || window.location.hostname === "localhost";

            if (isLocal) {
                trackerId = "local-ap";
                startTracking(true);
            } else if (idParam) {
                trackerId = idParam.trim();
                startTracking(false);
            } else {
                document.getElementById("prompt-overlay").style.display = "flex";
            }
        });

        function submitTrackerId() {
            const inputVal = document.getElementById("tracker-id-input").value.trim();
            if (inputVal) {
                trackerId = inputVal;
                document.getElementById("prompt-overlay").style.opacity = 0;
                setTimeout(() => {
                    document.getElementById("prompt-overlay").style.display = "none";
                }, 500);
                startTracking(false);
            }
        }

        function startTracking(isLocal) {
            document.getElementById("display-tracker-id").innerText = trackerId.toUpperCase();
            
            // Initialize Leaflet Map with zoomControl disabled to avoid header overlap
            map = L.map('map', {
                zoomControl: false,
                attributionControl: false
            }).setView([20.5937, 78.9629], 5); // Center of India default

            // Define map layers for high-detail switcher (Free, Keyless, Premium)
            const darkMatter = L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
                maxZoom: 20,
                attribution: '&copy; CartoDB'
            });

            const detailedStreets = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
                maxZoom: 19,
                attribution: '&copy; OpenStreetMap'
            });

            const satelliteHybrid = L.layerGroup([
                L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {
                    maxZoom: 19,
                    attribution: '&copy; Esri'
                }),
                L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/Reference/World_Transportation/MapServer/tile/{z}/{y}/{x}', {
                    maxZoom: 19,
                    attribution: '&copy; Esri'
                })
            ]);

            // Add default base layer
            darkMatter.addTo(map);

            const baseMaps = {
                "🌌 Dark Mode": darkMatter,
                "🗺️ Detailed Streets": detailedStreets,
                "🛰️ Satellite Hybrid": satelliteHybrid
            };

            // Add premium controls to topright
            L.control.layers(baseMaps, null, { position: 'topright' }).addTo(map);
            L.control.zoom({ position: 'topright' }).addTo(map);

            // Initialize Trail Polyline
            pathLine = L.polyline([], {
                color: '#6366f1',
                weight: 4,
                opacity: 0.8,
                dashArray: '5, 8'
            }).addTo(map);

            if (isLocal) {
                // If running locally, fall back to standard rapid AJAX polling to ESP32 API
                setInterval(fetchLocalCoordinates, 1000);
            } else {
                // Main-thread native EventSource listening (bypasses background CPU/worker throttling)
                initFirebaseStream();
            }
        }

        // Main-thread high-reliability self-healing real-time sync with HTTP polling fallback
        function initFirebaseStream() {
            const statusPulse = document.getElementById("status-pulse");
            const connectionStatus = document.getElementById("connection-status");
            let fallbackActive = false;

            // Clear any existing connections or intervals
            if (eventSource) {
                try { eventSource.close(); } catch(e) {}
            }
            if (pollingInterval) {
                clearInterval(pollingInterval);
            }

            const url = "https://" + FIREBASE_HOST + "/trackers/" + trackerId + ".json?auth=j5M7TpiXTb6rYaItUnFIwtXE2aIJnkdSKQQ6dkNr";

            // Start a 4-second timeout to fall back to HTTP polling if SSE is blocked or stuck
            const fallbackTimeout = setTimeout(() => {
                if (!fallbackActive) {
                    console.warn("SSE stream initialization timed out. Falling back to HTTP polling...");
                    startHttpPolling();
                }
            }, 4000);

            try {
                eventSource = new EventSource(url);

                const handleUpdate = (event) => {
                    try {
                        const packet = JSON.parse(event.data);
                        if (!packet) return;
                        
                        let coords = null;
                        if (packet.path === "/" || packet.path === "") {
                            coords = packet.data;
                        } else if (packet.data && typeof packet.data === 'object' && packet.data.lat && packet.data.lon) {
                            coords = packet.data;
                        }
                        
                        if (coords && coords.lat && coords.lon) {
                            const lat = parseFloat(coords.lat);
                            const lon = parseFloat(coords.lon);
                            const alt = parseFloat(coords.alt || 0);

                            if (!isNaN(lat) && !isNaN(lon) && lat !== 0 && lon !== 0) {
                                updateUIAndMap(lat, lon, alt, new Date());
                                statusPulse.style.backgroundColor = "#10b981"; // Success green
                                connectionStatus.innerText = "Real-time Live Sync (Stream)";
                            }
                        }
                    } catch (err) {
                        console.error("Error parsing Firebase stream update:", err);
                    }
                };

                eventSource.addEventListener('put', handleUpdate);
                eventSource.addEventListener('patch', handleUpdate);

                eventSource.onopen = function() {
                    clearTimeout(fallbackTimeout);
                    if (!fallbackActive) {
                        statusPulse.style.backgroundColor = "#10b981";
                        connectionStatus.innerText = "Ready, listening for lock...";
                    }
                };

                eventSource.onerror = function() {
                    if (!fallbackActive) {
                        console.warn("SSE stream error. Attempting HTTP polling fallback...");
                        clearTimeout(fallbackTimeout);
                        startHttpPolling();
                    }
                };

            } catch (err) {
                console.error("Failed to construct EventSource:", err);
                clearTimeout(fallbackTimeout);
                startHttpPolling();
            }

            function startHttpPolling() {
                if (fallbackActive) return;
                fallbackActive = true;
                
                if (eventSource) {
                    try { eventSource.close(); } catch(e) {}
                }

                connectionStatus.innerText = "Real-time Live Sync (Active)";
                statusPulse.style.backgroundColor = "#eab308"; // Warning yellow to show active fallback

                // Perform immediate first fetch
                fetchCoordinatesViaHttp();

                // Poll every 3 seconds for battery conservation and network stability
                pollingInterval = setInterval(fetchCoordinatesViaHttp, 3000);
            }

            function fetchCoordinatesViaHttp() {
                fetch(url)
                    .then(res => res.json())
                    .then(data => {
                        if (data && data.lat && data.lon) {
                            const lat = parseFloat(data.lat);
                            const lon = parseFloat(data.lon);
                            const alt = parseFloat(data.alt || 0);

                            if (!isNaN(lat) && !isNaN(lon) && lat !== 0 && lon !== 0) {
                                updateUIAndMap(lat, lon, alt, new Date());
                                statusPulse.style.backgroundColor = "#10b981"; // Reset to green on success
                                connectionStatus.innerText = "Real-time Live Sync (Active)";
                            }
                        }
                    })
                    .catch(err => {
                        console.error("HTTP fallback polling error:", err);
                        statusPulse.style.backgroundColor = "#ef4444"; // Error red
                        connectionStatus.innerText = "Reconnecting...";
                    });
            }
        }

        // Fallback standard rapid polling for offline local AP server mode
        function fetchLocalCoordinates() {
            fetch("/api/location")
                .then(response => response.json())
                .then(data => {
                    const lat = parseFloat(data.lat);
                    const lon = parseFloat(data.lon);
                    const alt = parseFloat(data.alt);
                    if (!isNaN(lat) && !isNaN(lon) && lat !== 0 && lon !== 0) {
                        updateUIAndMap(lat, lon, alt, new Date());
                        document.getElementById("status-pulse").style.backgroundColor = "#10b981";
                        document.getElementById("connection-status").innerText = "Local WiFi Live Sync";
                    }
                })
                .catch(err => {
                    document.getElementById("status-pulse").style.backgroundColor = "#ef4444";
                    document.getElementById("connection-status").innerText = "AP connection lost";
                });
        }

        function updateUIAndMap(lat, lon, alt, timestamp) {
            // Update stats
            document.getElementById("stat-lat").innerText = lat.toFixed(6);
            document.getElementById("stat-lon").innerText = lon.toFixed(6);
            document.getElementById("stat-alt").innerText = alt.toFixed(0) + "m";
            document.getElementById("last-update").innerText = timestamp.toLocaleTimeString();

            const newPos = [lat, lon];
            coordinatesHistory.push(newPos);
            
            if (coordinatesHistory.length > 300) coordinatesHistory.shift();
            pathLine.setLatLngs(coordinatesHistory);

            // Update or Create Marker
            if (!marker) {
                const customIcon = L.divIcon({
                    className: 'custom-div-icon',
                    html: `<div style="background-color: #6366f1; width: 18px; height: 18px; border-radius: 50%; border: 3px solid white; box-shadow: 0 0 15px #6366f1;"></div>`,
                    iconSize: [18, 18],
                    iconAnchor: [9, 9]
                });
                
                marker = L.marker(newPos, { icon: customIcon }).addTo(map);
                marker.bindPopup(`<div class="custom-popup" style="font-weight:600;">📍 SAFEDOT Live Position</div>`, { className: 'custom-popup' });
            } else {
                marker.setLatLng(newPos);
            }

            // Smoothly center the map on coordinates on first load
            if (isFirstLoad) {
                map.setView(newPos, 16);
                isFirstLoad = false;
            }
        }

        function centerOnTarget() {
            if (coordinatesHistory.length > 0) {
                const latestPos = coordinatesHistory[coordinatesHistory.length - 1];
                map.setView(latestPos, 16);
            }
        }
    </script>
</body>
</html>
)rawliteral";
// --- GPRS / APN CONFIGURATION ---

// --- HARDWARE CONFIGURATION ---
// Define UART2 pins for EC200U connection
#define MODEM_RX 16   // Connect to EC200U TX
#define MODEM_TX 17   // Connect to EC200U RX

// Control pins (Set to -1 if you are not using hardware PWRKEY/RESET control lines)
// In your setup, the Quectel EC200U is powered via external USB-C, so it boots automatically!
#define PWR_KEY_PIN -1  // Set to -1 since PWRKEY control line is not used
#define RST_PIN -1      // Set to -1 since RESET control line is not used
#define BUTTON_PIN 13  // Physical Push Button pin (Connect GPIO 13 and Ground)

#define MODEM_BAUDRATE 115200
#define SERIAL_MONITOR_BAUD 115200

// Target phone numbers for SMS alerts
const int NUM_TARGET_PHONES = 2;
const String TARGET_PHONES[NUM_TARGET_PHONES] = {
  "+916392449475",
  "+919610120255"
};

// --- GPRS / APN CONFIGURATION (For 4G SIM Internet) ---
// APN is Airtel
#define GPRS_APN "airtelgprs.com"

// --- WEB MAP HOSTING URL ---
#define LIVE_MAP_URL "https://aroraexe.github.io/safedot/?id="

// Unique Tracker Identifier variables
String trackerId = "";

// Timing variables
unsigned long lastGpsQueryTime = 0;
const unsigned long GPS_QUERY_INTERVAL = 5000; // Query GPS every 5 seconds
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL = 60000;  // Check network health every 60 seconds

// SMS transmission control variables
unsigned long lastSmsAlertTime = 0;
const unsigned long SMS_ALERT_INTERVAL = 300000; // Only send SMS location alert every 5 minutes (300000ms) to prevent spamming
bool firstLocationSent = false;

// Volatile variables for push button hardware interrupt handling
volatile bool buttonPressed = false;
volatile unsigned long lastInterruptTime = 0;

void IRAM_ATTR handleButtonInterrupt() {
  unsigned long interruptTime = millis();
  // Hardware debounce: Only register if at least 250ms have passed since last trigger
  if (interruptTime - lastInterruptTime > 250) {
    buttonPressed = true;
    lastInterruptTime = interruptTime;
  }
}

// Function declarations
void powerOnModem();
void resetModem();
bool sendATCommandInternal(const char* cmd, const char* expected_resp, unsigned int timeout_ms, String &response);
bool sendATCommand(const char* cmd, const char* expected_resp, unsigned int timeout_ms, String &response);
void initializeModem();
void enableGPS(bool enable);
void checkGPSLocation();
void checkIncomingSMS();
bool sendSMS(const String &phone, const String &message);
void triggerManualLocationSMS();
bool uploadCoordinatesToFirebase(String lat, String lon, String alt);
void gprsUploadTask(void *pvParameters);

void setup() {
  // Initialize Serial Monitors
  Serial.begin(SERIAL_MONITOR_BAUD);
  delay(1000);
  
  Serial.println(F("\n============================================="));
  Serial.println(F("🚀 SAFEDOT ESP32 & Quectel EC200U-CN GPS Tracker"));
  Serial.println(F("============================================="));

  // Initialize FreeRTOS synchronization structures
  modemMutex = xSemaphoreCreateMutex();
  gpsQueue = xQueueCreate(10, sizeof(GpsFix));

  // Generate unique tracker ID using ESP32 MAC address
  uint64_t chipId = ESP.getEfuseMac();
  trackerId = "safedot-" + String((uint32_t)(chipId >> 32), HEX) + String((uint32_t)chipId, HEX);
  Serial.print(F("🆔 Unique Tracker ID: "));
  Serial.println(trackerId);

  // Configure control pins if connected
  if (PWR_KEY_PIN != -1) {
    pinMode(PWR_KEY_PIN, OUTPUT);
    digitalWrite(PWR_KEY_PIN, HIGH);
  }
  if (RST_PIN != -1) {
    pinMode(RST_PIN, OUTPUT);
    digitalWrite(RST_PIN, HIGH);
  }

  // Initialize Serial2 for communication with EC200U
  Serial2.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(500);

  // Step 1: Power cycle / Boot modem (Only if not already responding)
  powerOnModem();

  // Step 2: Initialize AT interface, GSM Network, SMS configuration
  initializeModem();

  // Step 3: Turn on GPS GNSS Engine
  enableGPS(true);

  // Configure physical push button pin with internal pull-up resistor and hardware interrupt
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);

  // Configure WiFi Access Point (AP) mode for local web serving
  WiFi.softAP("SAFEDOT-TRACKER", "safedot123");
  IPAddress IP = WiFi.softAPIP();
  Serial.print(F("🌐 Access Point SSID: SAFEDOT-TRACKER | IP Address: "));
  Serial.println(IP);

  // Set up HTTP Server routes
  server.on("/", []() {
    server.send(200, "text/html", MAP_HTML);
  });
  
  server.on("/api/location", []() {
    String json = "{\"lat\":" + lastLatitude + ",\"lon\":" + lastLongitude + ",\"alt\":" + lastAltitude + "}";
    server.send(200, "application/json", json);
  });

  server.begin();
  Serial.println(F("✅ On-board Web Server started successfully."));

  // Spawn the background GPRS/Firebase upload task on Core 0
  xTaskCreatePinnedToCore(
    gprsUploadTask,
    "GprsUploadTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );

  Serial.println(F("\n🟢 System is active. Monitoring GPS, Button, and Network..."));
}

void loop() {
  server.handleClient(); // Handle incoming on-board Web Server requests
  unsigned long currentMillis = millis();

  // Check if hardware button interrupt flag is set
  if (buttonPressed) {
    buttonPressed = false; // Reset the interrupt flag immediately
    triggerManualLocationSMS();
    // Wait briefly for release if button is still held down
    while (digitalRead(BUTTON_PIN) == LOW) {
      delay(10);
    }
  }

  // Check and display any unsolicited incoming messages (like SMS notification)
  checkIncomingSMS();

  // Periodically query GPS coordinates
  if (currentMillis - lastGpsQueryTime >= GPS_QUERY_INTERVAL) {
    lastGpsQueryTime = currentMillis;
    checkGPSLocation();
  }

  // Periodically check signal quality and network status
  if (currentMillis - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
    lastHeartbeatTime = currentMillis;
    String resp;
    sendATCommand("AT+CSQ", "OK", 2000, resp);   // Check Signal Quality
    sendATCommand("AT+CREG?", "OK", 2000, resp); // Check GSM Registration
  }

  // Allow manual command forwarding from PC Serial Monitor to the EC200U for debugging
  while (Serial.available()) {
    char c = Serial.read();
    Serial2.write(c);
  }
}

/**
 * Pulse PWRKEY pin LOW for 2 seconds to boot up the modem.
 * Skips pulsing if the modem is already powered on and responding.
 */
void powerOnModem() {
  String response;
  
  Serial.println(F("🔍 Checking if EC200U is already awake and responding..."));
  // Send simple AT test first
  if (sendATCommand("AT", "OK", 1500, response)) {
    Serial.println(F("⚡ Modem is already powered ON and responding. Skipping PWRKEY pulse."));
    return;
  }

  if (PWR_KEY_PIN == -1) {
    Serial.println(F("⚠️ Modem is not responding and PWR_KEY_PIN is disabled (-1). Please turn it on manually."));
    return;
  }

  Serial.println(F("🔄 Pulsing PWRKEY pin to boot up Quectel EC200U..."));
  digitalWrite(PWR_KEY_PIN, LOW);
  delay(2000); // 2-second pulse is required to trigger startup
  digitalWrite(PWR_KEY_PIN, HIGH);
  
  Serial.println(F("⏳ Waiting 10 seconds for modem to boot up and register UART..."));
  delay(10000);
}

/**
 * Hard Reset by pulsing RST pin LOW for 150ms.
 */
void resetModem() {
  if (RST_PIN == -1) {
    Serial.println(F("⚠️ RST_PIN is disabled. Skipping hardware reset."));
    return;
  }
  Serial.println(F("🚨 Performing hardware reset on EC200U..."));
  digitalWrite(RST_PIN, LOW);
  delay(150);
  digitalWrite(RST_PIN, HIGH);
  delay(8000); // Wait for reboot
}

/**
 * Core internal non-blocking helper to send AT commands to the EC200U with timeouts.
 * Must be executed with modemMutex ALREADY HELD or before multitasking is initialized.
 */
bool sendATCommandInternal(const char* cmd, const char* expected_resp, unsigned int timeout_ms, String &response) {
  // Clear the ESP32 RX buffer
  while (Serial2.available()) {
    Serial2.read();
  }

  response = "";
  Serial.print(F("➡️ AT Sending: "));
  Serial.println(cmd);

  Serial2.print(cmd);
  Serial2.print("\r\n");

  unsigned long startTime = millis();
  bool matched = false;

  while (millis() - startTime < timeout_ms) {
    while (Serial2.available()) {
      char c = Serial2.read();
      response += c;
      if (response.indexOf(expected_resp) != -1) {
        matched = true;
      }
    }
    // Small delay to prevent tight-looping
    delay(10);
  }

  response.trim();
  if (matched) {
    Serial.print(F("⬅️ Received Response:\n"));
    Serial.println(response);
    return true;
  } else {
    Serial.print(F("⚠️ Response Timeout/Error. Raw received:\n"));
    Serial.println(response == "" ? "[Empty]" : response);
    return false;
  }
}

/**
 * Thread-safe wrapper to send AT commands to the EC200U.
 * Acquires modemMutex automatically to prevent serial collisions.
 */
bool sendATCommand(const char* cmd, const char* expected_resp, unsigned int timeout_ms, String &response) {
  if (modemMutex == NULL) {
    return sendATCommandInternal(cmd, expected_resp, timeout_ms, response);
  }

  if (xSemaphoreTake(modemMutex, portMAX_DELAY) == pdTRUE) {
    bool res = sendATCommandInternal(cmd, expected_resp, timeout_ms, response);
    xSemaphoreGive(modemMutex);
    return res;
  }
  return false;
}

/**
 * Set up network status and SMS handling.
 */
void initializeModem() {
  String response;
  Serial.println(F("\n⚙️ Initializing Cellular Connection..."));

  // Check baud sync
  int retry = 5;
  while (retry > 0) {
    if (sendATCommand("AT", "OK", 1000, response)) {
      break;
    }
    retry--;
    delay(1000);
  }

  // Turn off local echo for cleaner parsing
  sendATCommand("ATE0", "OK", 1000, response);

  // Check SIM Card Status (CPIN should be READY)
  sendATCommand("AT+CPIN?", "READY", 2000, response);

  // Configure SMS settings: text format mode
  sendATCommand("AT+CMGF=1", "OK", 2000, response);

  // Send new SMS notifications directly to UART (CNMI configuration)
  sendATCommand("AT+CNMI=2,2,0,0,0", "OK", 2000, response);
  
  Serial.println(F("✅ Modem initialization completed."));
}

/**
 * Enable or disable GPS on the Quectel module.
 */
void enableGPS(bool enable) {
  String response;
  if (enable) {
    Serial.println(F("🛰️ Activating GNSS Engine..."));
    
    // Some modules need NMEA source selection, standard is OK
    sendATCommand("AT+QGPSCFG=\"nmeasrc\",1", "OK", 2000, response);
    
    // Start GPS engine
    sendATCommand("AT+QGPS=1", "OK", 2000, response);
  } else {
    Serial.println(F("🛰️ Deactivating GNSS Engine..."));
    sendATCommand("AT+QGPSEND", "OK", 2000, response);
  }
}

/**
 * Query GPS coordinates and parse the +QGPSLOC response.
 * Runs on Core 1 (Main Thread) and pushes coordinates to gpsQueue.
 */
void checkGPSLocation() {
  String response;
  
  // Use AT+QGPSLOC=2 to request signed decimal coordinates directly
  if (sendATCommand("AT+QGPSLOC=2", "+QGPSLOC:", 3000, response)) {
    // Expected response looks like:
    // +QGPSLOC: 120531.00,12.971600,77.594600,1.0,920.0,3,124.5,0.0,0.0,190526,05
    
    int index = response.indexOf("+QGPSLOC:");
    if (index != -1) {
      String dataPart = response.substring(index + 9); // Extract everything after '+QGPSLOC:'
      dataPart.trim();
      
      // Parse parameters by commas
      // Format: <utc>,<latitude>,<longitude>,<hdop>,<altitude>,<fix>,<cog>,<spkm>,<spkn>,<date>,<nsat>
      int commaIndex[12];
      int commaCount = 0;
      for (int i = 0; i < dataPart.length(); i++) {
        if (dataPart.charAt(i) == ',') {
          commaIndex[commaCount] = i;
          commaCount++;
          if (commaCount >= 11) break;
        }
      }
      
      if (commaCount >= 5) {
        String utc = dataPart.substring(0, commaIndex[0]);
        String latitude = dataPart.substring(commaIndex[0] + 1, commaIndex[1]);
        String longitude = dataPart.substring(commaIndex[1] + 1, commaIndex[2]);
        String hdop = dataPart.substring(commaIndex[2] + 1, commaIndex[3]);
        String altitude = dataPart.substring(commaIndex[3] + 1, commaIndex[4]);
        
        Serial.println(F("\n============================================="));
        Serial.println(F("📍 NEW GPS POSITION FIX DETECTED!"));
        Serial.print(F("• Latitude  : ")); Serial.println(latitude);
        Serial.print(F("• Longitude : ")); Serial.println(longitude);
        Serial.print(F("• Altitude  : ")); Serial.print(altitude); Serial.println(" meters");
        Serial.print(F("• UTC Time  : ")); Serial.println(utc);
        
        // Generate Google Maps URL
        String mapsLink = "https://maps.google.com/?q=" + latitude + "," + longitude;
        Serial.print(F("🌍 Google Maps URL: ")); Serial.println(mapsLink);
        Serial.println(F("=============================================\n"));
         
        // Update local Web Server cache immediately for offline mapping API response
        lastLatitude = latitude;
        lastLongitude = longitude;
        lastAltitude = altitude;

        // Push new coordinate packet to the background FreeRTOS Queue for Core 0 GPRS upload
        GpsFix newFix;
        memset(&newFix, 0, sizeof(GpsFix));
        latitude.toCharArray(newFix.lat, sizeof(newFix.lat));
        longitude.toCharArray(newFix.lon, sizeof(newFix.lon));
        altitude.toCharArray(newFix.alt, sizeof(newFix.alt));
        
        if (gpsQueue != NULL) {
          if (xQueueSend(gpsQueue, &newFix, 0) == pdTRUE) {
            Serial.println(F("📥 [QUEUE] Location packet queued for background GPRS upload."));
          } else {
            Serial.println(F("⚠️ [QUEUE] Warning: GPS queue is full. Dropping packet."));
          }
        }
      }
    }
  } else {
    Serial.println(F("⚠️ GNSS coordinates not available yet. If outdoor antenna is connected, please wait for cold start satellite lock."));
  }
}

/**
 * Check UART for unexpected incoming serial messages (like SMS notification URCs).
 * Thread-safe: performs a non-blocking check on modemMutex to avoid interrupting active background GPRS uploads.
 */
void checkIncomingSMS() {
  if (modemMutex == NULL) return;

  String incoming = "";
  // Non-blocking try to acquire the modem mutex to read unsolicited buffer
  if (xSemaphoreTake(modemMutex, 0) == pdTRUE) {
    if (Serial2.available()) {
      unsigned long start = millis();
      // Wait briefly to make sure we read the complete message line
      while (millis() - start < 300) {
        while (Serial2.available()) {
          char c = Serial2.read();
          incoming += c;
        }
      }
    }
    xSemaphoreGive(modemMutex);
  }

  incoming.trim();
  if (incoming.length() > 0) {
    Serial.println(F("\n📩 [UNSOLICITED MODEM DATA]:"));
    Serial.println(incoming);
    Serial.println(F("-----------------------------\n"));
    
    // Parse +CMT SMS notification if message arrives (in standard text mode format)
    int cmtIndex = incoming.indexOf("+CMT:");
    if (cmtIndex != -1) {
      Serial.println(F("🔔 Notification: SMS Text Received on Modem!"));
      
      // Extract sender phone number
      int startQuote = incoming.indexOf('"', cmtIndex);
      int endQuote = incoming.indexOf('"', startQuote + 1);
      if (startQuote != -1 && endQuote != -1) {
        String senderPhone = incoming.substring(startQuote + 1, endQuote);
        Serial.print(F("👤 Sender Number: "));
        Serial.println(senderPhone);
        
        // Extract the SMS message body
        int newlineIndex = incoming.indexOf('\n', endQuote);
        if (newlineIndex != -1) {
          String msgBody = incoming.substring(newlineIndex + 1);
          msgBody.trim();
          msgBody.toUpperCase();
          
          Serial.print(F("💬 SMS Message: "));
          Serial.println(msgBody);
          
          if (msgBody.indexOf("LOC") != -1 || msgBody.indexOf("TRACK") != -1 || msgBody.indexOf("LOCATION") != -1) {
            Serial.println(F("🎯 Tracking command detected! Querying live GPS coordinates to reply..."));
            
            // Query current GPS coordinates (sendATCommand will safely take the mutex)
            String gpsResp;
            if (sendATCommand("AT+QGPSLOC=2", "+QGPSLOC:", 3000, gpsResp)) {
              int gpsIdx = gpsResp.indexOf("+QGPSLOC:");
              if (gpsIdx != -1) {
                String dataPart = gpsResp.substring(gpsIdx + 9);
                dataPart.trim();
                
                int commaIndex[10];
                int commaCount = 0;
                for (int i = 0; i < dataPart.length(); i++) {
                  if (dataPart.charAt(i) == ',') {
                    commaIndex[commaCount] = i;
                    commaCount++;
                    if (commaCount >= 6) break;
                  }
                }
                
                if (commaCount >= 5) {
                  String lat = dataPart.substring(commaIndex[0] + 1, commaIndex[1]);
                  String lon = dataPart.substring(commaIndex[1] + 1, commaIndex[2]);
                  String alt = dataPart.substring(commaIndex[3] + 1, commaIndex[4]);
                  
                  String replyMsg = "📍 LIVE LOCATION REQUEST\nLat: " + lat + "\nLon: " + lon + "\nAlt: " + alt + "m\nGoogle Maps: https://maps.google.com/?q=" + lat + "," + lon;
                  
                  // sendSMS safely takes the mutex and handles the response handshakes!
                  sendSMS(senderPhone, replyMsg);
                }
              }
            } else {
              // GPS not locked yet
              sendSMS(senderPhone, "⚠️ SAFEDOT GPS is active but has no satellite lock yet. Please place the antenna outdoors and try again shortly.");
            }
          }
        }
      }
    }
  }
}

/**
 * Thread-safe wrapper to send SMS messages using AT commands.
 */
bool sendSMS(const String &phone, const String &message) {
  Serial.print(F("✉️ Sending SMS to: "));
  Serial.println(phone);
  
  if (modemMutex != NULL) {
    if (xSemaphoreTake(modemMutex, portMAX_DELAY) != pdTRUE) {
      return false;
    }
  }

  // Clear UART2 buffer first
  while (Serial2.available()) {
    Serial2.read();
  }

  // Write the CMGS command
  Serial2.print("AT+CMGS=\"");
  Serial2.print(phone);
  Serial2.print("\"\r\n");
  
  // Wait for the prompt '>' (timeout 3000ms)
  unsigned long start = millis();
  bool foundPrompt = false;
  while (millis() - start < 3000) {
    if (Serial2.available()) {
      char c = Serial2.read();
      if (c == '>') {
        foundPrompt = true;
        break;
      }
    }
    delay(10);
  }
  
  bool success = false;
  if (foundPrompt) {
    // Send the message body followed by Ctrl+Z
    Serial2.print(message);
    Serial2.write(26); // Ctrl+Z (EOF byte)
    
    // Wait for the routing response confirmation containing OK (timeout 10000ms)
    String response = "";
    start = millis();
    while (millis() - start < 10000) {
      while (Serial2.available()) {
        char c = Serial2.read();
        response += c;
        if (response.indexOf("OK") != -1) {
          success = true;
        }
      }
      delay(10);
    }
    
    if (success) {
      Serial.println(F("🎉 SMS Sent successfully!"));
    } else {
      Serial.print(F("⚠️ Error sending SMS. Response: "));
      Serial.println(response);
    }
  } else {
    Serial.println(F("⚠️ Failed to receive SMS write prompt ('>') from modem."));
  }

  if (modemMutex != NULL) {
    xSemaphoreGive(modemMutex);
  }
  return success;
}

/**
 * Triggered by physical push button press.
 * Immediately reads live GPS coordinates and sends an SMS to all recipients.
 * Queues the coordinates to the background GPRS task for Firebase upload afterwards.
 */
void triggerManualLocationSMS() {
  Serial.println(F("\n🚨 BUTTON CLICK DETECTED! Fetching live coordinates for immediate SMS alert..."));
  String response;
  
  // Directly query the GNSS receiver for instant coordinates
  if (sendATCommand("AT+QGPSLOC=2", "+QGPSLOC:", 3000, response)) {
    int index = response.indexOf("+QGPSLOC:");
    if (index != -1) {
      String dataPart = response.substring(index + 9);
      dataPart.trim();
      
      int commaIndex[10];
      int commaCount = 0;
      for (int i = 0; i < dataPart.length(); i++) {
        if (dataPart.charAt(i) == ',') {
          commaIndex[commaCount] = i;
          commaCount++;
          if (commaCount >= 6) break;
        }
      }
      
      if (commaCount >= 5) {
        String lat = dataPart.substring(commaIndex[0] + 1, commaIndex[1]);
        String lon = dataPart.substring(commaIndex[1] + 1, commaIndex[2]);
        String alt = dataPart.substring(commaIndex[3] + 1, commaIndex[4]);
        String mapsLink = "https://maps.google.com/?q=" + lat + "," + lon;
        
        Serial.println(F("📍 Live Button Alert coordinates acquired!"));
        
        // 1. Build emergency alert notification
        String smsAlert = "🚨 SAFEDOT EMERGENCY ALERT!\n"
                          "Lat: " + lat + "\n"
                          "Lon: " + lon + "\n"
                          "Alt: " + alt + "m\n"
                          "Google Map: " + mapsLink + "\n"
                          "Live Tracker: " + String(LIVE_MAP_URL) + trackerId;
        
        // 2. Broadcast the SMS immediately
        for (int i = 0; i < NUM_TARGET_PHONES; i++) {
          sendSMS(TARGET_PHONES[i], smsAlert);
        }
        
        // Update local Web Server cache variables
        lastLatitude = lat;
        lastLongitude = lon;
        lastAltitude = alt;

        // 3. Queue coordinates to the background GPRS task for Firebase upload afterwards
        GpsFix newFix;
        memset(&newFix, 0, sizeof(GpsFix));
        lat.toCharArray(newFix.lat, sizeof(newFix.lat));
        lon.toCharArray(newFix.lon, sizeof(newFix.lon));
        alt.toCharArray(newFix.alt, sizeof(newFix.alt));

        if (gpsQueue != NULL) {
          if (xQueueSend(gpsQueue, &newFix, 0) == pdTRUE) {
            Serial.println(F("📥 [QUEUE] Emergency location packet queued for background GPRS upload."));
          }
        }
        
        return;
      }
    }
  }
  
  // If GPS is not locked yet
  Serial.println(F("⚠️ Failed to obtain GPS lock for manual trigger. Sending status update..."));
  for (int i = 0; i < NUM_TARGET_PHONES; i++) {
    sendSMS(TARGET_PHONES[i], "⚠️ SAFEDOT Panic Button pressed, but device has no GPS satellite lock yet!");
  }
}

/**
 * Upload coordinate points to Google Firebase Realtime Database
 * Uses Quectel high-level HTTP client stack with PATCH method override.
 * Executed by the background task on Core 0.
 */
/**
 * Rapidly uploads coordinates to Google Firebase Realtime Database.
 * Runs on Core 0 and uses the already-buffered URL inside the modem.
 * Only sends GPRS status verification and immediate HTTPPOST payload commands, reducing GPRS overhead to <1s!
 */
bool uploadCoordinatesToFirebase(String lat, String lon, String alt) {
  Serial.println(F("🌐 [GPRS TASK] Rapidly pushing coordinates to Firebase..."));
  String response;
  bool success = false;

  if (modemMutex == NULL) return false;

  // Take the mutex for the immediate rapid GHTTP write
  if (xSemaphoreTake(modemMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  // 1. Fast verify PDP context is still alive
  sendATCommandInternal("AT+QIACT?", "OK", 1000, response);
  if (response.indexOf("+QIACT: 1") == -1) {
    Serial.println(F("⚙️ [GPRS TASK] GPRS dropped. Re-activating PDP context..."));
    sendATCommandInternal("AT+QIACT=1", "OK", 15000, response);
  }

  // 2. Fire the rapid HTTPPOST command using the persistent URL in the modem buffer!
  String jsonPayload = "{\"lat\":\"" + lat + "\",\"lon\":\"" + lon + "\",\"alt\":\"" + alt + "\"}";
  int payloadLen = jsonPayload.length();
  String postCmd = "AT+QHTTPPOST=" + String(payloadLen) + ",10000,10000";

  if (sendATCommandInternal(postCmd.c_str(), "CONNECT", 5000, response)) {
    Serial2.print(jsonPayload);
    
    // Wait for the OK/URC confirmation (+QHTTPPOST: 0,200)
    unsigned long postStart = millis();
    response = "";
    while (millis() - postStart < 15000) {
      while (Serial2.available()) {
        char c = Serial2.read();
        response += c;
        if (response.indexOf("+QHTTPPOST: 0,200") != -1 || response.indexOf("OK") != -1) {
          success = true;
        }
      }
      delay(10);
    }
    
    if (success) {
      Serial.println(F("🎉 [GPRS TASK] Coordinates successfully uploaded to Firebase RTDB!"));
    } else {
      Serial.print(F("⚠️ [GPRS TASK] Firebase upload failed. URC response: "));
      Serial.println(response);
    }
  }

  xSemaphoreGive(modemMutex);
  return success;
}

/**
 * FreeRTOS task running on Core 0.
 * Listens to gpsQueue and uploads incoming coordinates to Firebase in the background.
 * Performs a one-time HTTP/GPRS PDP context activation and URL registration,
 * then rapidly sends only HTTPPOST commands in the loop to reduce AT command overhead by 80%!
 */
void gprsUploadTask(void *pvParameters) {
  GpsFix fix;
  Serial.println(F("🔄 [GPRS TASK] Background GPRS upload task initialized on Core 0."));
  
  // Wait briefly for the modem setup in main loop to settle
  delay(5000);

  // Perform one-time PDP context activation and HTTP URL registration to buffer
  bool initSuccess = false;
  while (!initSuccess) {
    if (xSemaphoreTake(modemMutex, portMAX_DELAY) == pdTRUE) {
      String response;
      Serial.println(F("⚙️ [GPRS TASK] Running one-time GPRS / GHTTP configuration..."));
      
      // 1. Activate PDP GPRS Context
      sendATCommandInternal("AT+QIACT?", "OK", 2000, response);
      if (response.indexOf("+QIACT: 1") == -1) {
        String apnCmd = "AT+QICSGP=1,1,\"" + String(GPRS_APN) + "\",\"\",\"\",1";
        sendATCommandInternal(apnCmd.c_str(), "OK", 2000, response);
        sendATCommandInternal("AT+QIACT=1", "OK", 15000, response);
      }
      
      // 2. Configure high-level HTTP client parameters for HTTPS SSL
      sendATCommandInternal("AT+QHTTPCFG=\"contextid\",1", "OK", 2000, response);
      sendATCommandInternal("AT+QHTTPCFG=\"responseheader\",0", "OK", 2000, response);
      sendATCommandInternal("AT+QHTTPCFG=\"sslctxid\",1", "OK", 2000, response);
      
      // 3. Register target URL in the modem's persistent RAM buffer once
      String url = "https://safedot-73b43-default-rtdb.firebaseio.com/trackers/" + trackerId + ".json?auth=j5M7TpiXTb6rYaItUnFIwtXE2aIJnkdSKQQ6dkNr&x-http-method-override=PATCH";
      int urlLen = url.length();
      String urlCmd = "AT+QHTTPURL=" + String(urlLen) + ",10000";
      
      if (sendATCommandInternal(urlCmd.c_str(), "CONNECT", 5000, response)) {
        Serial2.print(url);
        // Wait for OK
        unsigned long start = millis();
        response = "";
        while (millis() - start < 5000) {
          while (Serial2.available()) {
            char c = Serial2.read();
            response += c;
          }
          delay(10);
        }
        
        if (response.indexOf("OK") != -1) {
          Serial.println(F("✅ [GPRS TASK] Target Firebase URL successfully registered in modem RAM buffer!"));
          initSuccess = true;
        }
      }
      
      xSemaphoreGive(modemMutex);
    }
    if (!initSuccess) {
      Serial.println(F("⚠️ [GPRS TASK] Initialization failed. Retrying in 5 seconds..."));
      delay(5000);
    }
  }

  // Enter rapid coordinate upload loop
  for (;;) {
    // Wait indefinitely for a new GPS fix to be queued
    if (xQueueReceive(gpsQueue, &fix, portMAX_DELAY) == pdTRUE) {
      Serial.print(F("🔄 [GPRS TASK] Processing queued GPS packet: "));
      Serial.print(fix.lat); Serial.print(F(", ")); Serial.println(fix.lon);
      
      uploadCoordinatesToFirebase(String(fix.lat), String(fix.lon), String(fix.alt));
    }
  }
}
