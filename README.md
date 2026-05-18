# ESP32-S3 OV3660 MJPEG Stream

Arduino IDE project for an ESP32-S3-WROOM N16R8 camera board with OV3660, 16 MB flash and 8 MB PSRAM.

The main sketch starts a single HTTP site on the ESP32:

```text
http://BOARD_IP/
```

The page embeds the MJPEG stream from the same device:

```text
http://BOARD_IP/stream
```

## Current target board

The camera pinout in the sketch matches ESP32-S3 camera boards compatible with the ESP32-S3-EYE / Keyestudio MB0184 / DIYables ESP32-S3 WROOM N16R8 CAM style wiring.

Camera pins used:

```cpp
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD   4
#define CAM_PIN_SIOC   5
#define CAM_PIN_D0     11
#define CAM_PIN_D1     9
#define CAM_PIN_D2     8
#define CAM_PIN_D3     10
#define CAM_PIN_D4     12
#define CAM_PIN_D5     18
#define CAM_PIN_D6     17
#define CAM_PIN_D7     16
#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   7
#define CAM_PIN_PCLK   13
```

If your board has a different camera wiring, change these defines first.

## Arduino IDE settings

Recommended starting settings:

- Board: `ESP32S3 Dev Module`
- Flash Size: `16MB`
- PSRAM: `OPI PSRAM` / `Enabled`
- USB CDC On Boot: use `Disabled` for the USB-UART port, `Enabled` for native USB-OTG
- Upload Speed: `460800`
- Partition Scheme: `Huge APP` / 16 MB variant if available
- Serial Monitor: `115200`

## Files

- `ESP32S3_OV3660_MJPEG/ESP32S3_OV3660_MJPEG.ino` - main camera web stream sketch.
- `ESP32S3_Blink_Serial_Test/ESP32S3_Blink_Serial_Test.ino` - firmware/Serial/LED test.
- `ESP32S3_SD_MMC_Test/ESP32S3_SD_MMC_Test.ino` - SD_MMC card test.

## Stream tuning

Useful settings in the main sketch:

```cpp
#define CAMERA_FRAME_SIZE       FRAMESIZE_QVGA
#define CAMERA_JPEG_QUALITY     14
#define MAX_STREAM_CLIENTS      4
#define STREAM_TARGET_FPS       15
```

For more stability with 2-4 viewers, use QVGA/CIF, JPEG quality 12-16, and 8-12 FPS. For better detail with 1-2 viewers, try `FRAMESIZE_VGA`.

JPEG quality is inverse: lower number means better quality and larger frame; higher number means smaller frame and lower quality.

## Notes

ESP32-S3 can serve a few MJPEG viewers directly, but it is limited by CPU, Wi-Fi bandwidth, RAM and camera frame buffers. For many viewers, use a relay server: ESP32-S3 streams to one PC/Raspberry Pi/server, and that server redistributes the stream.