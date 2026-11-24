# FilaMan - Filament Management System

⚠️ **Important: Starting with v3.0.0, this system requires the [FilaMan-System](https://github.com/Fire-Devils/filaman-system) backend. Previous direct integrations (Spoolman, MQTT, Bambu Lab) have been moved to the central FilaMan-System.**

FilaMan is a filament management tool for 3D printing. It uses ESP32 hardware for weight measurement and NFC tag management.
Users can manage filament spools and configure the device via a web interface.
The system integrates seamlessly with the [FilaMan-System](https://github.com/Fire-Devils/filaman-system).

**NEW since 3.3.1:** A reduced version w/o scale is also available. Details can be found in [NFC-only mode](docs/NFC-only_mode_en.md).

![Scale](./img/scale_trans.png)


More Images can be found in the [img Folder](/img/)
or my website: [FilaMan Website](https://www.filaman.app)
german explanatory video: [Youtube](https://youtu.be/uNDe2wh9SS8?si=b-jYx4I1w62zaOHU)
Discord Server: [https://discord.gg/my7Gvaxj2v](https://discord.gg/my7Gvaxj2v)

## NEW: Recycling Fabrik

<a href="https://www.recyclingfabrik.com" target="_blank">
    <img src="img/rf-logo.png" alt="Recycling Fabrik" width="200">
</a>

FilaMan is supported by [Recycling Fabrik](https://www.recyclingfabrik.com).
Recycling Fabrik will soon offer a FilaMan-compatible NFC tag on their spools. This has the advantage
that the spools can be automatically recognized and imported into the FilaMan-System directly via the FilaMan scale.

**What is Recycling Fabrik?**

Recycling Fabrik is a German company dedicated to developing and manufacturing sustainable 3D printing filament.
Their filaments are made from 100% recycled material from both end customers and industry – for an environmentally conscious and resource-saving future.

More information and products can be found here: [www.recyclingfabrik.com](https://www.recyclingfabrik.com)

---

### Now more detailed informations about the usage: [Wiki](https://github.com/ManuelW77/Filaman/wiki)

### ESP32 Hardware Features
- **Weight Measurement:** Using a load cell with HX711 amplifier for precise weight tracking.
- **NFC Tag Reading/Writing:** PN532 module for reading and writing filament data to NFC tags. Optional setting "Read NFC only when the weight is stable" (web UI > Setup, off by default) reduces weight jitter caused by the NFC radio field.
- **OLED Display:** Shows current weight and connection status (WiFi, FilaMan-System).
- **WiFi Connectivity:** WiFiManager for easy network configuration.
- **NFC-Tag NTAG213 NTAG215:** Use NTAG213, better NTAG215 because of enough space on the Tag

### Web Interface Features
- **Real-time Updates:** WebSocket connection for live data updates.
- **NFC Tag Management:**
	- Write filament data to NFC tags.
	- Supports automatic Spool detection in compatible systems.
- **FilaMan-System Integration:**
  - Synchronize spool data with the central backend.
  - Update spool weights automatically.
  - Track NFC tag assignments.

### If you want to support my work, i would be happy to get a coffe

<a href="https://www.buymeacoffee.com/manuelw" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 30px !important;width: 108px !important;" ></a>

## Manufacturer Tags Support

🎉 **Exciting News!** FilaMan now supports **Manufacturer Tags** - NFC tags that come pre-programmed directly from filament manufacturers!

### First Manufacturer Partner: RecyclingFabrik

We're thrilled to announce that [**RecyclingFabrik**](https://www.recyclingfabrik.de) will be the **first filament manufacturer** to support FilaMan by offering NFC tags in the FilaMan format on their spools!

**Coming Soon:** RecyclingFabrik spools will include NFC tags that automatically integrate with your FilaMan system, eliminating manual setup and ensuring perfect compatibility.

### How Manufacturer Tags Work

When you scan a manufacturer NFC tag for the first time:
1. **Automatic Brand Detection:** FilaMan recognizes the manufacturer and creates the brand in the FilaMan-System.
2. **Filament Type Creation:** All material specifications are automatically added.
3. **Spool Registration:** Your specific spool is registered with proper weight and specifications.
4. **Future Fast Recognition:** Subsequent scans use fast-path detection for instant weight measurement.

**For detailed technical information:** [Manufacturer Tags Documentation](README_ManufacturerTags_EN.md)

### Benefits for Users
- ✅ **Zero Manual Setup** - Just scan and weigh
- ✅ **Perfect Data Accuracy** - Manufacturer-verified specifications
- ✅ **Instant Integration** - Seamless FilaMan-System compatibility
- ✅ **Future-Proof** - Tags work with any FilaMan-compatible system

## Detailed Functionality

### ESP32 Functionality
- **User Interactions:** The OLED display provides immediate feedback on the system status, including weight measurements and connection status.

### Web Interface Functionality
- **User Interactions:** The web interface allows users to interact with the system, configure the device, and monitor status.
- **UI Elements:** Includes forms for registration, buttons for scale actions, and real-time status indicators.

## Hardware Requirements

### Components (Affiliate Links)
- **ESP32 Development Board:** Any ESP32 variant.
[Amazon Link](https://amzn.to/3FHea6D)
- **HX711 5kg Load Cell Amplifier:** For weight measurement.
[Amazon Link](https://amzn.to/4ja1KTe)
- **OLED 0.96 Zoll I2C white/yellow Display:** 128x64 SSD1306.
[Amazon Link](https://amzn.to/445aaa9)
- **PN532 NFC NXP RFID-Modul V3:** For NFC tag operations.
[Amazon Link](https://amzn.eu/d/gy9vaBX)
- **NFC Tags NTAG213 NTAG215:** RFID Tag
[Amazon Link](https://amzn.to/3E071xO)
- **TTP223 Touch Sensor (optional):** For reTARE per Button/Touch
[Amazon Link](https://amzn.to/4hTChMK)


### Pin Configuration
| Component          | ESP32 Pin | ESP32-S3 Pin |
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

The I2C pins are the Arduino defaults for each chip and are not set explicitly in the code.

**!! ESP32-S3: do not use GPIO19/GPIO20 for the HX711.** These are the native USB data lines (D-/D+) and are wired to the "USB" connector on the DevKitC-1, which makes the HX711 detection unreliable.

**!! Make sure that the DIP switches on the PN532 are set to I2C**
**Use the 3V pin from the ESP for the touch sensor**

![Wiring](./img/Schaltplan.png)

![myWiring](./img/IMG_2589.jpeg)
![myWiring](./img/IMG_2590.jpeg)

*The load cell is connected to most HX711 modules as follows:
E+ red
E- black
A- white
A+ green*

## Software Dependencies

### ESP32 Libraries
- `WiFiManager`: Network configuration
- `ESPAsyncWebServer`: Web server functionality
- `ArduinoJson`: JSON parsing and creation
- `Adafruit_PN532`: NFC functionality (copy in `lib/` with an added `disableRFField()` method)
- `Adafruit_SSD1306`: OLED display control
- `HX711`: Load cell communication

### Installation

## Prerequisites
- **Software:**
  - [PlatformIO](https://platformio.org/) in VS Code
  - [FilaMan-System](https://github.com/Fire-Devils/filaman-system) instance
- **Hardware:**
  - ESP32 Development Board
  - HX711 Load Cell Amplifier
  - Load Cell (weight sensor)
  - OLED Display (128x64 SSD1306)
  - PN532 NFC Module
  - Connecting wires


### Step-by-Step Installation
### Easy Installation
1. **Go to [FilaMan Installer](https://www.filaman.app/installer.html)**

2. **Plug you device in and push Connect button**

3. **Select your Device Port and push Intall**

4. **Initial Setup:**
    - Connect to the "FilaMan" WiFi access point.
    - Configure WiFi settings through the captive portal.
    - Access the web interface at `http://filaman.local` or the IP address.

### Compile by yourself
1. **Clone the Repository:**
    ```bash
    git clone https://github.com/ManuelW77/Filaman-System-esp32.git
    cd Filaman-System-esp32
    ```
2. **Install Dependencies:**
    ```bash
    pio lib install
    ```
3. **Flash the ESP32:**
    ```bash
    pio run --target upload
    ```
4. **Initial Setup:**
    - Connect to the "FilaMan" WiFi access point.
    - Configure WiFi settings through the captive portal.
    - Access the web interface at `http://filaman.local` or the IP address.

## Updates over WiFi (OTA)

Registered devices fetch firmware updates directly from the FilaMan server, no cable needed.

- **Flow:** 30 s (+ 0-30 s random) after the first connection and then every 6 h (+ 0-30 min random) the device asks `GET /api/v1/devices/firmware/latest?project=<project>`. If the server offers a different version, the device downloads it, verifies size and SHA-256, checks the project marker inside the image, installs it and reboots. Nothing is installed while a manual upload, an NFC write, a calibration or a weighing is in progress; the device retries later.
- **Project identifiers:** `filaman-scale-esp32` (ESP32) and `filaman-scale-esp32s3` (ESP32-S3). They are embedded in every image as `FMFWINFO1:project=...;version=...;`; the server reads project and version from this marker only, and the device refuses images of another project.
- **Security:** the download path must start with `/api/v1/devices/firmware/`, redirects are never followed, so the device token only goes to your FilaMan server.
- **Rollback:** the Arduino bootloader has no hardware rollback, so the firmware does it in software. After an update the new firmware must get a 2xx answer from the server within 300 s of WiFi-connected time (heartbeat, report or manifest; time without WiFi does not count) and must not reboot more than 3 times, otherwise the device switches back to the previous firmware and reports `rolled_back`. A rolled-back version is not installed again automatically. Devices that are not registered confirm immediately.
- **Security:** images are not signed. The device only checks the SHA-256 from the server response and the project id. Anyone on the same network who can spoof traffic to the FilaMan server (especially over `http://`) could push their own firmware. The same applies to the manual upload on the device's web page. Run FilaMan and the scales only on your own trusted network, never exposed to the internet.
- **Web interface:** it is part of the firmware image (no separate website/LittleFS image anymore). LittleFS only keeps the device configuration.
- **Initial flash** (empty devices or via cable / web installer): `filaman_full_<version>.bin` (ESP32) or `filaman_full_esp32s3_<version>.bin` (ESP32-S3), flashed at offset 0.
- **Manual update:** upload `upgrade_filaman_firmware_v<version>.bin` (ESP32) or `upgrade_filaman_firmware_esp32s3_v<version>.bin` (ESP32-S3) on the device's `/upgrade` page.
- **Existing devices (older than this feature)** must be updated once manually with `upgrade_filaman_firmware_*.bin`. Afterwards updates come from the server. The old separate website image is no longer needed or accepted.

## Documentation

### Relevant Links
- [FilaMan-System](https://github.com/Fire-Devils/filaman-system)
- [PlatformIO Documentation](https://docs.platformio.org/)

### Tutorials and Examples
- [PlatformIO Getting Started](https://docs.platformio.org/en/latest/tutorials/espressif32/arduino_debugging_unit_testing.html)
- [ESP32 Web Server Tutorial](https://randomnerdtutorials.com/esp32-web-server-arduino-ide/)

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Materials

### Useful Resources
- [ESP32 Official Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [Arduino Libraries](https://www.arduino.cc/en/Reference/Libraries)
- [NFC Tag Information](https://learn.adafruit.com/adafruit-pn532-rfid-nfc/overview)

### Community and Support
- [PlatformIO Community](https://community.platformio.org/)
- [Arduino Forum](https://forum.arduino.cc/)
- [ESP32 Forum](https://www.esp32.com/)

## Availability

The code can be tested and the application can be downloaded from the [GitHub repository](https://github.com/ManuelW77/Filaman-System-esp32).

### If you want to support my work, i would be happy to get a coffe
<a href="https://www.buymeacoffee.com/manuelw" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 30px !important;width: 108px !important;" ></a>
