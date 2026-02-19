let socket;
let isConnected = false;
const RECONNECT_INTERVAL = 5000;

function initWebSocket() {
    if (socket) {
        socket.close();
        socket = null;
    }

    try {
        socket = new WebSocket('ws://' + window.location.host + '/ws');
        
        socket.onopen = function() {
            isConnected = true;
            console.log("WebSocket connected");
        };
        
        socket.onclose = function() {
            isConnected = false;
            setTimeout(initWebSocket, RECONNECT_INTERVAL);
        };
        
        socket.onmessage = function(event) {
            const data = JSON.parse(event.data);
            if (data.type === 'nfcData') {
                updateNfcDisplay(data.payload);
            } else if (data.type === 'heartbeat') {
                updateStatusDots(data);
            } else if (data.type === 'weight') {
                document.getElementById('weightDisplay').textContent = data.value + 'g';
            }
        };
    } catch (error) {
        isConnected = false;
        setTimeout(initWebSocket, RECONNECT_INTERVAL);
    }
}

function updateStatusDots(data) {
    const filamanDot = document.getElementById('filamanDot');
    const ramStatus = document.getElementById('ramStatus');

    if (filamanDot) {
        filamanDot.style.backgroundColor = data.filaman_connected ? '#4caf50' : '#f44336';
    }
    if (ramStatus) {
        ramStatus.textContent = `${data.freeHeap}k free`;
    }
}

function updateNfcDisplay(data) {
    const nfcInfo = document.getElementById('nfcInfo');
    if (data.error || data.info || !data || Object.keys(data).length === 0) {
        nfcInfo.textContent = data.info || data.error || "No tag detected";
        return;
    }

    let html = "<strong>Tag detected:</strong><br>";
    if (data.sm_id) {
        html += `Spool ID: ${data.sm_id}<br>`;
        if (data.brand) html += `Brand: ${data.brand}<br>`;
        if (data.type) html += `Material: ${data.type}<br>`;
    } else if (data.location_id) {
        html += `Location ID: ${data.location_id}<br>`;
    } else {
        html += "Unknown Tag Format";
    }
    nfcInfo.innerHTML = html;
}

document.addEventListener("DOMContentLoaded", function() {
    initWebSocket();
    // Heartbeat loop
    setInterval(() => {
        if (socket && socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify({ type: 'heartbeat' }));
        }
    }, 5000);
});
