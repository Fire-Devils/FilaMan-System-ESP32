# Dokumentation: RFID/NFC Schreibvorgang (Technischer Guide für KI)

Diese Dokumentation beschreibt den Prozess des Schreibens von RFID/NFC-Tags im FilaMan-System. Sie dient als Referenz für KI-Systeme, um zu verstehen, wie Befehle delegiert, verarbeitet und zurückgemeldet werden.

## 1. Übersicht der Architektur

Der Schreibvorgang folgt einem **Delegations-Modell**. Das System (Server) agiert als Orchestrator zwischen der Benutzeroberfläche (Frontend) und der physischen Hardware (Device).

### Beteiligte Komponenten:
*   **Frontend (Astro/Web):** Initiiert den Schreibvorgang durch Benutzerinteraktion.
*   **Backend (Python/FastAPI):** Validiert die Anfrage, findet das Zielgerät und delegiert den Befehl.
*   **Device (Hardware/ESP32):** Führt den physischen Schreibvorgang aus und meldet die Tag-UID zurück.

---

## 2. Der Ablauf (Step-by-Step)

### Schritt 1: Initiierung (Frontend -> Backend)
Wenn ein Benutzer eine Spule oder einen Ort mit einem Tag verknüpfen möchte, sendet das Frontend eine Anfrage an das Backend.
- **Endpunkt:** `POST /api/v1/devices/{device_id}/write-tag`
- **Payload:**
  ```json
  {
    "spool_id": 123,      // ODER
    "location_id": 45
  }
  ```

### Schritt 2: Delegation (Backend -> Device)
Das Backend prüft, ob das Gerät online ist (letzter Heartbeat < 180s) und sendet den Befehl an die lokale IP des Geräts.
- **Endpunkt am Gerät:** `POST http://<DEVICE_IP>/api/v1/rfid/write`
- **Wartezeit (Timeout):** Das Backend wartet bis zu **180 Sekunden** auf eine Antwort vom Gerät. Dies gibt dem Benutzer genug Zeit, den Tag physisch an den Leser zu halten.

### Schritt 3: Physische Verarbeitung (Device)
Das Gerät muss:
1. Den Befehl empfangen.
2. In einen "Schreib-Modus" wechseln (meist signalisiert durch LED/Display).
3. Auf die Annäherung eines beschreibbaren Tags warten.
4. Die eindeutige **`tag_uuid`** (UID) des Tags auslesen.

### Schritt 4: Rückmeldung (Device -> Backend)
Nach erfolgreichem Scan/Schreibvorgang antwortet das Gerät dem Backend innerhalb der bestehenden HTTP-Verbindung.
- **Response vom Gerät:**
  ```json
  {
    "tag_uuid": "E28011052000700D2171010A" 
  }
  ```

### Schritt 5: Abschluss (Backend -> DB & Frontend)
Das Backend empfängt die `tag_uuid` und führt die Datenbank-Update aus:
- Bei `spool_id`: Update von `spools.rfid_uid`.
- Bei `location_id`: Update von `locations.identifier`.
Anschließend wird der Erfolg an das Frontend gemeldet, welches den Ladezustand beendet.

---

## 3. Wichtige Anforderungen an das Device

### HTTP Server
Das Gerät muss einen HTTP-Server auf Port 80 (Standard) betreiben, der auf Anfragen unter `/api/v1/rfid/write` reagiert.

### Heartbeat & IP-Tracking
Damit das Backend das Gerät erreichen kann, **muss** das Gerät regelmäßig seine IP-Adresse melden:
- **Endpunkt:** `POST /api/v1/devices/heartbeat`
- **Payload:** `{"ip_address": "192.168.x.y"}`
- **Frequenz:** Alle 60-120 Sekunden.

### Fehlerbehandlung
Falls das Gerät den Vorgang nicht abschließen kann (z.B. Timeout am Gerät selbst oder Hardwarefehler), sollte es einen entsprechenden HTTP-Statuscode zurückgeben:
- `408 Request Timeout`: Wenn kein Tag innerhalb der internen Zeit des Geräts gefunden wurde.
- `500 Internal Server Error`: Bei Hardwareproblemen.

---

## 4. Zusammenfassung für die KI-Logik

1.  **Zustandsprüfung:** Das Gerät wird nur als "Online" markiert, wenn der Heartbeat aktuell ist.
2.  **Synchronität:** Der HTTP-Call vom Server zum Gerät ist blockierend. Die KI muss diesen Zustand (Loading State) im UI beibehalten, bis die Antwort eintrifft oder der 180s-Timeout greift.
3.  **Identifikation:** Das Ergebnis des Schreibvorgangs ist immer die physische UID des Tags (`tag_uuid`), welche als globaler Identifier im System für diese Ressource dient.
