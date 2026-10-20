#ifndef K55D_DEVICE_H
#define K55D_DEVICE_H

#include <windows.h>
#include <stdint.h>

// Corsair K55 RGB PRO. The keyboard uses the Corsair "Bragi" protocol on two vendor HID interfaces:
//   MI_01 (usage page 0xFF42, usage 1): commands and replies, 64-byte reports.
//   MI_02 (usage page 0xFF42, usage 2): key events, 64-byte input reports.
#define K55_VID 0x1B1C
#define K55_PID 0x1BA4

// Number of bytes in the key bitmap of an event report.
#define KEY_BITMAP_SIZE 61

// Bragi command status codes (byte after the command id in a reply).
#define BRAGI_OK 0x00

typedef struct device device;

// Opens the keyboard. Returns NULL if the keyboard is not connected.
device *dev_open(void);
void dev_close(device *d);

// Puts the keyboard in software mode and applies the colors and the key routing.
// colors: 6 LEDs, 3 bytes each (R, G, B). LED 0 is the macro recording / Windows lock indicator, LEDs 1-5 are the 5 zones.
// hidden_keys: key indexes that the keyboard must not send to Windows (HID). Use -1 to end the list.
// Returns 0 on success, -1 on error.
int dev_start_software_mode(device *d, const uint8_t colors[6][3], const int *hidden_keys);

// Saves a static color (R, G, B) in the hardware lighting: the keyboard shows it in hardware mode, also after it gets power again.
// Call only in software mode. The write turns off the software lighting: call dev_start_software_mode after it.
int dev_save_hardware_color(device *d, const uint8_t color[3]);

// Gives control back to the keyboard (hardware mode): the keyboard shows the hardware lighting.
int dev_start_hardware_mode(device *d);

// Sends a keep-alive message. The keyboard goes back to hardware mode if it does not receive a message during 60 s.
// Returns the current mode (1 = hardware, 2 = software) or -1 if the keyboard does not reply.
int dev_ping(device *d);

// Event handle that becomes signaled when a key event report is available.
HANDLE dev_event_handle(device *d);
// Reads the key event report that is available. Copies the key bitmap to bitmap.
// Returns 1 if the report is a key bitmap, 0 for other reports, -1 on read error.
int dev_take_event(device *d, uint8_t bitmap[KEY_BITMAP_SIZE]);

#endif
