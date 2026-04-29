# VC0706 Serial JPEG Camera Test

ESP-IDF example for VC0706-compatible serial JPEG camera modules.

## Wiring

Default pins follow the Arduino sketch as closely as possible:

| Device | ESP32-S3 default |
| --- | --- |
| Camera TX | GPIO2 / UART RX |
| Camera RX | GPIO3 / UART TX |
| Capture button | GPIO7 to GND, internal pull-up |
| SD MOSI | GPIO11 |
| SD MISO | GPIO13 |
| SD CLK | GPIO12 |
| SD CS | GPIO4 |

Camera power and UART level must match the module requirements. Many VC0706
modules use 5 V power but 3.3 V TTL serial; check your board before wiring.

## Build And Flash

```bash
cd test
idf.py set-target esp32s3
idf.py menuconfig
idf.py flash monitor
```

If your camera is shipped at 9600 baud, change
`VC0706 UART baud rate` in `menuconfig` to `9600`.
The example also scans common VC0706 baud rates during startup by default.

Press the button to save `/sdcard/IMAGE00.JPG` through `/sdcard/IMAGE99.JPG`.
Set `Capture button GPIO` to `-1` to take one picture immediately after boot.

## Troubleshooting

If the log shows `No camera found: ESP_ERR_TIMEOUT`, the ESP32 did not receive
any UART response from the camera. Check these first:

- Power the camera from `5V` and share `GND` with the ESP32 board.
- Confirm camera `TX` goes to the configured ESP32 RX GPIO, and camera `RX`
  goes to the configured ESP32 TX GPIO.
- Avoid boot/strap-sensitive pins while debugging. A good alternate wiring is
  camera `TX -> GPIO18` and camera `RX -> GPIO17`, then set UART RX to `18`
  and UART TX to `17` in `menuconfig`.
- If the camera TX pin outputs 5 V logic, add a level shifter or divider before
  connecting it to the ESP32 RX GPIO.
