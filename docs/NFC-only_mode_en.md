# NFC-only mode

## Description
It may be convenient to have a small device near your desktop computer when adding new spools to your database.
So you are able to write NFC-tags for them right away. Since there is no need for a scale (the weight of a new
spool should be well known), it is pleasant to leave the scale functionality out in advance of a smaller footprint.

![FilaLite_no_scale](../img/FilaLite_no_scale.png)

## Usage

Before you can use the NFC-only device, you have to register it to your FilaMan System App as
you would do with any regular scale device.

### Tag Writing
 Its main purpose is to write new NFC tags.

 You start the writing process from within the FilaMan System App (Web GUI). You than select
 your NFC device and follow the instructions on the device display.

### Tag Reading
Although the device is fully capable of tag reading, its use cases are limited due to the lack of a scale.
But you can still identify a spool and assign it to a location with the according location tag.

But to be honest: the main purpose of the NFC-only device is **TAG WRITING**.

## NFC-only mode

During startup, the ESP32 tries to detect the HX711 (amplifier) together with a connected
loadcell sensor. If successful it starts into normal mode with full functionality.

Otherwise it switches in **NFC-only mode!**

Instead of a weight reading the display shows a ready prompts. Additionally the scale icon
in the top row is crossed out.

![Ready Prompt](../img/no_scale_ready.png)

## Build / Installation
### Hardware
The wiring of the hardware is the same as the original, fully equipped FilaMan scale. There are only 3 differences:

- No HX711 connected (also no loadcell of course)
- No touch sensor (without scale there is no need to tare)
- optional: 10k pull-up resistor on Pin 16 (RX2)

The pull-up resistor is not mandatory, but for good measure. It makes it more reliable to
detect a missing hx711. But it should also work w/o the pull-up resistor.

![Schaltplan mit Pull-Up](../img/Schaltplan%20NFC-only%20pullup.png)

| Component	  | ESP32 Pin |
| ----        | ----      |
| 10k pull-up | 16        |
|        	  | 17        |
| OLED SDA	  | 21        |
| OLED SCL.   | 22        |
| PN532 IRQ   | 32        |
| PN532 RESET |	33        |
| PN532 SDA	  | 21        |
| PN532 SCL	  | 22        |

#### PN532
- !! Make sure that the DIP switches on the PN532 are set to I2C
- Connect `VCC` to 5V

#### OLED
- Connect `VCC` to 3.3V

#### 10k Resistor (optional)
- Connect to PIN 16 (where normally the DOUT of the HX711 would connect to) and 5V

### Software
Follow the instructions in the [main readme](../README.md#step-by-step-installation).

You need version 3.3.1 or later for a working hardware recognition and NFC-only mode.

## Print Files
[STLs](../_3D%20Print%20Files/FilaMan_Lite/)
