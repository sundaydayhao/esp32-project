# ESP32-S3 Desk Clock

This project now targets only generic `NT35510` LCD boards connected over the `I80` bus.

## Features

- ESP32-S3 + LVGL UI
- 800x480 desk clock layout
- Wi-Fi, NTP, and QWeather integration
- NT35510 `8-bit` or `16-bit` I80 bus support
- LCD diagnostic mode

## Hardware Configuration

Configure the panel in `menuconfig`:

```powershell
idf.py menuconfig
```

Open `Desk Clock Hardware` and set:

- resolution
- I80 bus width
- color order
- mirror/swap options
- WR / CS / DC / RST / BK / POWER / D0..D15 pins

The main display macros are defined in `main/config.h`.

## Build

Default build:

```powershell
idf.py set-target esp32s3
idf.py build
```

Flash:

```powershell
idf.py -p COMx flash monitor
```

The checked-in `sdkconfig.defaults` already includes the example NT35510 I80
pin mapping. Update the pin assignments there before flashing to real hardware.
