# Dokumentation: RFID/NFC Schreibvorgang (Technischer Guide für KI)

Diese Dokumentation beschreibt den Prozess des Schreibens von RFID/NFC-Tags im FilaMan-System. Sie dient als Referenz für KI-Systeme, um zu verstehen, wie Befehle delegiert, verarbeitet und zurückgemeldet werden.

## 1. Architektur-Übersicht (Asynchrones Modell)

Der Schreibvorgang folgt einem **asynchronen Modell**, um robuster für IoT-Geräte mit unzuverlässigen Netzwerkverbindungen zu sein.

```
┌─────────┐    1. Trigger    ┌─────────┐    2. Fire&Forget  ┌─────────┐
│ Frontend│ ────────────────→│ Backend │ ─────────────────→ │ Device  │
└─────────┘                   └─────────┘                    └─────────┘
       ↑                                                       │
       │                          4. DB-Update                  │
       │    5. Polling ←───────────────────────────────────────┘
       │    (Page Reload)         3. Result-Request
       │
    6. User Feedback
```

### Beteiligte Komponenten:
*   **Frontend:** Initiiert Schreibvorgang und pollt auf Ergebnis (via Page-Reload).
*   **Backend:** Delegiert Befehl an Device und aktualisiert DB, wenn Device Result sendet.
*   **Device:** Führt physischen Schreibvorgang aus und sendet eigenständig Ergebnis zurück.

---

## 2. Der Ablauf (Step-by-Step)

### Schritt 1: Initiierung (Frontend → Backend)
Das Frontend sendet eine Anfrage an das Backend.
- **Endpunkt:** `POST /api/v1/devices/{device_id}/write-tag`
- **Payload:**
  ```json
  {
    "spool_id": 123      // ODER
    "location_id": 45
  }
  ```

### Schritt 2: Fire & Forget (Backend → Device)
Das Backend sendet einen Trigger an das Device:
- **Endpunkt am Gerät:** `POST http://<DEVICE_IP>/api/v1/rfid/write`
- **Timeout:** Das Backend wartet nur kurz (5 Sekunden) auf eine Bestätigung, dass der Trigger empfangen wurde. Es wartet NICHT auf das RFID-Ergebnis.
- **Response an Frontend:** Das Backend antwortet sofort mit:
  ```json
  {
    "success": true,
    "message": "Schreibvorgang wurde gestartet. Bitte Tag bereit halten..."
  }
  ```

### Schritt 3: Physische Verarbeitung (Device)
Das Device:
1. Empfängt den Trigger.
2. Startet den RFID-Schreibvorgang.
3. Sendet **eigenständig** das Ergebnis an das Backend.

### Schritt 4: Ergebnis an Backend (Device → Backend)
Das Device sendet das Ergebnis nach Abschluss des Schreibvorgangs:
- **Endpunkt:** `POST /api/v1/devices/rfid-result`
- **Authentifizierung:** `Authorization: Device <TOKEN>`
- **Payload:**
  ```json
  {
    "success": true,
    "tag_uuid": "E2801234...",
    "spool_id": 123,    // Optional: ID der Spule
    "location_id": 45   // Optional: ID des Ortes
  }
  ```

### Schritt 5: Datenbank-Update (Backend)
Das Backend empfängt das Ergebnis:
- Bei `spool_id`: Update von `spools.rfid_uid`.
- Bei `location_id`: Update von `locations.identifier`.

### Schritt 6: Frontend-Polling
Das Frontend pollt periodisch (alle 2 Sekunden) durch erneutes Laden der Spulen/Locations-Liste:
- Sobald die `rfid_uid` oder `identifier` gesetzt ist → Erfolgsmeldung.
- Nach max. 30 Versuchen (60 Sekunden) → Timeout-Meldung.

---

## 3. API-Referenz

### 3.1 Trigger Endpoint (Frontend → Backend)

**`POST /api/v1/devices/{device_id}/write-tag`**

Startet den Schreibvorgang auf dem Device. Antwortet sofort (Fire & Forget).

**Request:**
```json
{
  "spool_id": 123,      // Optional
  "location_id": 45     // Optional (eins von beiden muss gesetzt sein)
}
```

**Response (sofort):**
```json
{
  "success": true,
  "message": "Schreibvorgang wurde gestartet. Bitte Tag bereit halten..."
}
```

### 3.2 Result Endpoint (Device → Backend)

**`POST /api/v1/devices/rfid-result`**

Das Device ruft diesen Endpoint auf, nachdem der Schreibvorgang abgeschlossen ist.

**Header:**
- `Authorization: Device <TOKEN>`

**Request:**
```json
{
  "success": true,
  "tag_uuid": "E2801234...",
  "spool_id": 123,      // Optional
  "location_id": 45,    // Optional
  "error_message": null  // Optional: Fehlermeldung falls success=false
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Spool updated" // oder "Location updated"
}
```

---

## 4. Anforderungen an das Device

### 4.1 Endpoint für Trigger
Das Device muss folgenden Endpoint unterstützen:
- **URL:** `http://<DEVICE_IP>/api/v1/rfid/write`
- **Method:** POST
- **Content-Type:** application/json

**Verhalten:**
- Empfängt den Trigger (spool_id oder location_id)
- Startet den RFID-Schreibvorgang
- Sendet NICHT sofort die tag_uuid zurück (Fire & Forget)

### 4.2 Eigenständiges Senden des Ergebnisses
Nach dem Schreibvorgang muss das Device eigenständig das Ergebnis an das Backend senden:

```cpp
// Pseudocode für ESP32
void sendResultToBackend(String tagUuid, int locationId) {
    HTTPClient http;
    http.begin("http://<BACKEND_IP>/api/v1/devices/rfid-result");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Device " + deviceToken);
    
    String json = "{\"success\": true, \"tag_uuid\": \"" + tagUuid + "\", \"location_id\": " + locationId + "}";
    http.POST(json);
    http.end();
}
```

### 4.3 Authentifizierung
Das Device muss sich mit dem Token authentifizieren:
- Header: `Authorization: Device <TOKEN>`
- Der Token ist der gleiche, der auch für Heartbeat verwendet wird.

---

## 5. Zusammenfassung für die KI-Logik

1.  **Fire & Forget:** Der Trigger-Request vom Backend zum Device ist asynchron. Das Backend antwortet sofort.
2.  **Push-Modell:** Das Device ist für das Senden des Ergebnisses verantwortlich (nicht das Warten auf eine Antwort).
3.  **Polling:** Das Frontend erkennt den Erfolg durch das Pollen/Neuladen der Daten (prüfen auf gesetzte `rfid_uid` oder `identifier`).
4.  **Keine offene Verbindung:** Es gibt keine 180-Sekunden-Wartezeit mehr auf eine offene HTTP-Verbindung.
