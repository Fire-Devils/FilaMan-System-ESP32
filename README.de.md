# FilaMan - Filament Management System

⚠️ **Wichtig: Ab Version 3.0.0 erfordert dieses System das [FilaMan-System](https://github.com/Fire-Devils/filaman-system) Backend. Frühere direkte Integrationen (Spoolman, MQTT, Bambu Lab) wurden in das zentrale FilaMan-System verschoben.**

FilaMan ist ein Werkzeug zur Filamentverwaltung für den 3D-Druck. Es verwendet ESP32-Hardware zur Gewichtsmessung und Verwaltung von NFC-Tags.
Benutzer können Filamentspulen verwalten und das Gerät über eine Weboberfläche konfigurieren.
Das System integriert sich nahtlos in das [FilaMan-System](https://github.com/Fire-Devils/filaman-system).

**NEU ab 3.3.1:** Eine abgespeckte Version ohne Waage ist ebenfalls möglich. Details finden sich unter [NFC-only Modus](docs/NFC-only_mode_de.md).

![Waage](./img/scale_trans.png)


Weitere Bilder finden Sie im [img Ordner](/img/)
oder auf meiner Website: [FilaMan Website](https://www.filaman.app)
Deutsches Erklärvideo: [Youtube](https://youtu.be/uNDe2wh9SS8?si=b-jYx4I1w62zaOHU)
Discord Server: [https://discord.gg/my7Gvaxj2v](https://discord.gg/my7Gvaxj2v)

## NEU: Recycling Fabrik

<a href="https://www.recyclingfabrik.com" target="_blank">
    <img src="img/rf-logo.png" alt="Recycling Fabrik" width="200">
</a>

FilaMan wird von der [Recycling Fabrik](https://www.recyclingfabrik.com) unterstützt.
Die Recycling Fabrik wird in Kürze FilaMan-kompatible NFC-Tags auf ihren Spulen anbieten. Dies hat den Vorteil,
dass die Spulen automatisch erkannt und über die FilaMan-Waage direkt in das FilaMan-System importiert werden können.

**Was ist die Recycling Fabrik?**

Die Recycling Fabrik ist ein deutsches Unternehmen, das sich der Entwicklung und Herstellung von nachhaltigem 3D-Druck-Filament verschrieben hat.
Ihre Filamente bestehen zu 100 % aus recyceltem Material von Endkunden und aus der Industrie – für eine umweltbewusste und ressourcenschonende Zukunft.

Weitere Informationen und Produkte finden Sie hier: [www.recyclingfabrik.com](https://www.recyclingfabrik.com)

---

### Detailliertere Informationen zur Nutzung: [Wiki](https://github.com/ManuelW77/Filaman/wiki)

### ESP32 Hardware-Features
- **Gewichtsmessung:** Verwendung einer Wägezelle mit HX711-Verstärker für präzises Gewichts-Tracking.
- **NFC-Tag Lesen/Schreiben:** PN532-Modul zum Lesen und Schreiben von Filamentdaten auf NFC-Tags.
- **OLED-Display:** Zeigt aktuelles Gewicht und Verbindungsstatus (WiFi, FilaMan-System) an.
- **WiFi-Konnektivität:** WiFiManager für einfache Netzwerkkonfiguration.
- **NFC-Tag NTAG213 NTAG215:** Verwendung von NTAG213, besser NTAG215 wegen ausreichendem Platz auf dem Tag.

### Weboberflächen-Features
- **Echtzeit-Updates:** WebSocket-Verbindung für Live-Datenaktualisierungen.
- **NFC-Tag-Verwaltung:**
	- Schreiben von Filamentdaten auf NFC-Tags.
	- Unterstützt automatische Spulenerkennung in kompatiblen Systemen.
- **FilaMan-System Integration:**
  - Synchronisierung der Spulendaten mit dem zentralen Backend.
  - Automatische Aktualisierung der Spulengewichte.
  - Verfolgung von NFC-Tag-Zuweisungen.

### Wenn Sie meine Arbeit unterstützen möchten, freue ich mich über einen Kaffee

<a href="https://www.buymeacoffee.com/manuelw" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 30px !important;width: 108px !important;" ></a>

## Unterstützung von Hersteller-Tags

🎉 **Spannende Neuigkeiten!** FilaMan unterstützt jetzt **Hersteller-Tags** - NFC-Tags, die direkt von Filamentherstellern vorprogrammiert geliefert werden!

### Erster Hersteller-Partner: RecyclingFabrik

Wir freuen uns bekannt zu geben, dass [**RecyclingFabrik**](https://www.recyclingfabrik.de) der **erste Filamenthersteller** sein wird, der FilaMan unterstützt, indem er NFC-Tags im FilaMan-Format auf seinen Spulen anbietet!

**In Kürze:** RecyclingFabrik-Spulen werden NFC-Tags enthalten, die sich automatisch in Ihr FilaMan-System integrieren, wodurch eine manuelle Einrichtung entfällt und perfekte Kompatibilität gewährleistet wird.

### Funktionsweise von Hersteller-Tags

Wenn Sie einen Hersteller-NFC-Tag zum ersten Mal scannen:
1. **Automatische Markenerkennung:** FilaMan erkennt den Hersteller und erstellt die Marke im FilaMan-System.
2. **Erstellung des Filamenttyps:** Alle Materialspezifikationen werden automatisch hinzugefügt.
3. **Spulenregistrierung:** Ihre spezifische Spule wird mit korrektem Gewicht und Spezifikationen registriert.
4. **Zukünftige Schnellerkennung:** Nachfolgende Scans nutzen die Schnellerkennung für sofortige Gewichtsmessung.

**Für detaillierte technische Informationen:** [Dokumentation zu Hersteller-Tags](README_ManufacturerTags_EN.md) (Englisch)

### Vorteile für Benutzer
- ✅ **Keine manuelle Einrichtung** - Einfach scannen und wiegen.
- ✅ **Perfekte Datengenauigkeit** - Vom Hersteller verifizierte Spezifikationen.
- ✅ **Sofortige Integration** - Nahtlose Kompatibilität mit dem FilaMan-System.
- ✅ **Zukunftssicher** - Tags funktionieren mit jedem FilaMan-kompatiblen System.

## Detaillierte Funktionalität

### ESP32-Funktionalität
- **Benutzerinteraktionen:** Das OLED-Display bietet sofortiges Feedback zum Systemstatus, einschließlich Gewichtsmessungen und Verbindungsstatus.

### Weboberflächen-Funktionalität
- **Benutzerinteraktionen:** Die Weboberfläche ermöglicht es Benutzern, mit dem System zu interagieren, das Gerät zu konfigurieren und den Status zu überwachen.
- **UI-Elemente:** Enthält Formulare zur Registrierung, Schaltflächen für Waagenaktionen und Echtzeit-Statusanzeigen.

## Hardware-Anforderungen

### Komponenten (Affiliate-Links)
- **ESP32 Development Board:** Jede ESP32-Variante.
[Amazon Link](https://amzn.to/3FHea6D)
- **HX711 5kg Wägezellenverstärker:** Für die Gewichtsmessung.
[Amazon Link](https://amzn.to/4ja1KTe)
- **OLED 0,96 Zoll I2C weiß/gelbes Display:** 128x64 SSD1306.
[Amazon Link](https://amzn.to/445aaa9)
- **PN532 NFC NXP RFID-Modul V3:** Für NFC-Tag-Operationen.
[Amazon Link](https://amzn.eu/d/gy9vaBX)
- **NFC-Tags NTAG213 NTAG215:** RFID-Tag.
[Amazon Link](https://amzn.to/3E071xO)
- **TTP223 Touch-Sensor (optional):** Für TARA per Knopfdruck/Berührung.
[Amazon Link](https://amzn.to/4hTChMK)


### Pin-Konfiguration
| Komponente         | ESP32 Pin | ESP32-S3 Pin |
|-------------------|-----------|--------------|
| HX711 DOUT        | 16        | 5            |
| HX711 SCK         | 17        | 6            |
| OLED SDA          | 21        | 8            |
| OLED SCL          | 22        | 9            |
| PN532 IRQ         | 32        | 15           |
| PN532 RESET       | 33        | 16           |
| PN532 SDA         | 21        | 8            |
| PN532 SCL         | 22        | 9            |
| TTP223 I/O        | 25        | 7            |

Die I2C-Pins sind die Arduino-Standardwerte des jeweiligen Chips und werden im Code nicht explizit gesetzt.

**!! ESP32-S3: GPIO19/GPIO20 nicht für den HX711 verwenden.** Das sind die nativen USB-Datenleitungen (D-/D+), die auf dem DevKitC-1 an der "USB"-Buchse liegen. Der HX711 wird dort nur unzuverlässig erkannt.

**!! Stellen Sie sicher, dass die DIP-Schalter am PN532 auf I2C eingestellt sind.**
**Verwenden Sie den 3V-Pin des ESP für den Touch-Sensor.**

![Verkabelung](./img/Schaltplan.png)

![meineVerkabelung](./img/IMG_2589.jpeg)
![meineVerkabelung](./img/IMG_2590.jpeg)

*Die Wägezelle wird an die meisten HX711-Module wie folgt angeschlossen:
E+ rot
E- schwarz
A- weiß
A+ grün*

## Software-Abhängigkeiten

### ESP32-Bibliotheken
- `WiFiManager`: Netzwerkkonfiguration
- `ESPAsyncWebServer`: Webserver-Funktionalität
- `ArduinoJson`: JSON-Parsing und -Erstellung
- `Adafruit_PN532`: NFC-Funktionalität
- `Adafruit_SSD1306`: OLED-Display-Steuerung
- `HX711`: Kommunikation mit der Wägezelle

### Installation

## Voraussetzungen
- **Software:**
  - [PlatformIO](https://platformio.org/) in VS Code
  - [FilaMan-System](https://github.com/Fire-Devils/filaman-system) Instanz
- **Hardware:**
  - ESP32 Development Board
  - HX711 Wägezellenverstärker
  - Wägezelle (Gewichtssensor)
  - OLED-Display (128x64 SSD1306)
  - PN532 NFC-Modul
  - Verbindungskabel


### Schritt-für-Schritt-Installation
### Einfache Installation
1. **Gehen Sie zum [FilaMan Installer](https://www.filaman.app/installer.html)**

2. **Schließen Sie Ihr Gerät an und drücken Sie die Connect-Taste.**

3. **Wählen Sie Ihren Geräte-Port aus und drücken Sie Install.**

4. **Ersteinrichtung:**
    - Verbinden Sie sich mit dem "FilaMan" WiFi-Access-Point.
    - Konfigurieren Sie die WiFi-Einstellungen über das Captive Portal.
    - Greifen Sie über `http://filaman.local` oder die IP-Adresse auf die Weboberfläche zu.

### Selbst kompilieren
1. **Repository klonen:**
    ```bash
    git clone https://github.com/ManuelW77/Filaman-System-esp32.git
    cd Filaman-System-esp32
    ```
2. **Abhängigkeiten installieren:**
    ```bash
    pio lib install
    ```
3. **ESP32 flashen:**
    ```bash
    pio run --target upload
    ```
4. **Ersteinrichtung:**
    - Verbinden Sie sich mit dem "FilaMan" WiFi-Access-Point.
    - Konfigurieren Sie die WiFi-Einstellungen über das Captive Portal.
    - Greifen Sie über `http://filaman.local` oder die IP-Adresse auf die Weboberfläche zu.

## Dokumentation

### Relevante Links
- [FilaMan-System](https://github.com/Fire-Devils/filaman-system)
- [PlatformIO Dokumentation](https://docs.platformio.org/)

### Tutorials und Beispiele
- [PlatformIO Erste Schritte](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html)
- [ESP32 Web Server Tutorial](https://randomnerdtutorials.com/esp32-web-server-arduino-ide/)

## Lizenz

Dieses Projekt steht unter der MIT-Lizenz. Weitere Informationen finden Sie in der Datei [LICENSE](LICENSE).

## Materialien

### Nützliche Ressourcen
- [Offizielle ESP32-Dokumentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [Arduino Bibliotheken](https://www.arduino.cc/en/Reference/Libraries)
- [NFC-Tag Informationen](https://learn.adafruit.com/adafruit-pn532-rfid-nfc/overview)

### Community und Support
- [PlatformIO Community](https://community.platformio.org/)
- [Arduino Forum](https://forum.arduino.cc/)
- [ESP32 Forum](https://www.esp32.com/)

## Verfügbarkeit

Der Code kann getestet und die Anwendung vom [GitHub-Repository](https://github.com/ManuelW77/Filaman-System-esp32) heruntergeladen werden.

### Wenn Sie meine Arbeit unterstützen möchten, freue ich mich über einen Kaffee
<a href="https://www.buymeacoffee.com/manuelw" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 30px !important;width: 108px !important;" ></a>
