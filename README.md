# k55d

Small replacement for iCUE for the Corsair K55 RGB PRO keyboard (USB 1B1C:1BA4).

k55d does these tasks:

- Sets the 5 lighting zones to one color. On request, saves this color in the keyboard memory (the color that the keyboard shows when k55d does not run).
- Makes the Fn key act as the left Windows key. While Fn is held, the keyboard does not send the other keys to Windows: k55d sends them, with the Windows key.
- Makes the G keys (G1 to G6) act as F13 to F18. Bind these keys in other programs (for example AutoHotkey or PowerToys).

k55d does not handle the other special keys (macro recording, brightness, Windows lock).

This is completely vibe coded. I have no idea what this code does and I haven't read a single line of it, but after two hours of replugging my keyboard and debugging with Big Opus, it works.

## Use

1. Stop iCUE and remove it from the startup programs. iCUE and k55d cannot control the keyboard at the same time.
2. Put `k55d.exe` and `k55d.ini` in the same folder. Edit `k55d.ini`.
3. Start `k55d.exe`. An icon appears in the notification area. Right-click it to reload `k55d.ini` or to exit.
4. To start k55d when you log in, add a value to `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` with the full path of `k55d.exe`.

k55d writes its messages to `k55d.log`, in the same folder. It shows a message box for errors.

When k55d exits, it gives control back to the keyboard (hardware mode). The keyboard then shows the color saved in its memory.

**To save the color of `k55d.ini` in the keyboard memory, select "Save color to keyboard memory" in the tray menu. The keyboard then shows this color when k55d does not run, also after it loses power. The keyboard does not keep every save: unplug and plug the keyboard to check, and save again if it shows the rainbow.** Fn+1 to Fn+5, Fn+6 to Fn+0 and Fn+~ (in hardware mode) replace the saved color with the built-in presets.

`k55d-<version>.exe --quit` stops the running instance, as "Exit" in the tray menu.

### Limits

- Windows does not let a normal program send keys to a program that runs as administrator. The G keys and Fn do not work in these windows. To remove this limit, run k55d as administrator (for example with a scheduled task "at log on" with "run with highest privileges").

## Build

The version is in `version.txt`. The build makes `k55d.exe`.

On Windows, requirements: MinGW-w64 gcc, windres and make.

```
make
```

On Linux, requirements: the MinGW-w64 cross-compiler (Debian/Ubuntu: `gcc-mingw-w64-x86-64`) and make.

```
make CROSS=x86_64-w64-mingw32-
```

GitHub Actions builds the exe for each push (see `.github/workflows/build.yml`). Editing version.txt also publishes a release.

## Protocol notes

The keyboard uses the Corsair "Bragi" protocol, on two vendor HID interfaces (usage page 0xFF42):

- MI_01: commands and replies. Output report: `00 08 <command> <parameters>`. Reply: `00 00 <command> <status> <data>`.
- MI_02: key events. Report: `00 00 02 <bitmap>`. Bit `i` of the bitmap is set while key index `i` is pressed. In software mode, all keys send events, also the keys that the keyboard sends to Windows.

| Command | Bytes | Use |
|---|---|---|
| Set property | `01 <prop> 00 <value>` | prop 0x03: mode (1 = hardware, 2 = software). prop 0x4A: 1 = key events. |
| Get property | `02 <prop> 00` | |
| Close handle | `05 01 <handle>` | |
| Write | `06 <handle> <length: 4 bytes LE> <data>` | first packet, 57 data bytes |
| Write next | `07 <handle> <data>` | next packets, 61 data bytes |
| Read | `08 <handle>` | |
| Open handle | `0D <handle> <resource: 2 bytes LE>` | |
| Ping | `12` | The keyboard goes back to hardware mode if it receives no message for 60 s. |

Resources and files:

- 0x0001: software lighting. 18 bytes: 6 red values, 6 green values, 6 blue values. LED 0 is the indicator, LEDs 1-5 are the zones.
- 0x0002: key routing. 137 bytes, one per key index. 1 = the keyboard sends the key to Windows, 0 = only the event channel receives the key.
- 0x6D61: lighting effect that the keyboard shows in hardware mode. The file can be read and written only in software mode. When the keyboard gets power, the firmware creates it from its saved preset selection (for example the rainbow effect `22 00 02 04 00 00 00 05 01 02 03 04 05`). Static color, as the firmware writes it: `7E 20 01 00 00 00 05`, then 5 x `FF <blue> <green> <red>` (zones 1-5), then `05 01 02 03 04 05` (LED list). A write after a read on the same handle fails with status 2: close and open the handle again before the write. Observed save behavior (firmware 1.2.18): at power-up, the keyboard loads only the per-zone format (33 bytes); with the 18-byte format or a 34-byte record it shows the rainbow. Only one save out of two is kept after a power loss, for an unknown reason. A write to this file turns off the software lighting until the next software lighting write. 0x6D60 is the list of effects (`69 6C 01 00 00 00 61 6D`).
- Command 0x09 `09 <handle>` returns information about an open file: bytes 2-3 of the data are the size.

Key indexes: usual keys use their HID usage code (A = 4). Fn = 122, G1-G6 = 131-136, brightness = 113, Windows lock = 114, macro recording = 127.

## Tools

`tools/` contains the programs that were used to find the protocol. Build them with `make tools`. Stop k55d and iCUE before you use the probe tools, because the replies of the keyboard go to all programs that have the device open.

- `tools/probe/hidlist.exe`: lists the HID interfaces of Corsair devices with their usage page and report sizes.
- `tools/probe/bragi.exe <bytes>`: sends one command to MI_01 and prints the reply. Example: `bragi.exe 08 02 03 00` reads the mode property.
- `tools/probe/bsh.exe [q]`: reads commands from stdin, one per line (hex bytes, `s <ms>` to wait, `#` for a comment), sends them on one connection and prints the replies. With `q`, it prints only the replies to the commands. Use it for sequences that need an open handle, for example a file read:
  ```
  printf '08 01 03 00 02\n08 0d 01 61 6d\n08 09 01\n08 08 01\n08 05 01 01\n' | bsh.exe q
  ```
- `tools/probe/listen.exe <seconds> [nop]`: prints the key event reports (MI_02) and the unsolicited replies (MI_01). It sends a ping every 5 s, except with `nop` (use `nop` while another program controls the keyboard).
- `tools/sniff/hook.dll`: when loaded in iCUE, logs every HID write of iCUE (WriteFile, HidD_SetOutputReport, HidD_SetFeature, HID DeviceIoControl) to `sniff.log`, next to the DLL. The replies are not logged: run `listen.exe <seconds> nop` at the same time to see them.
- `tools/sniff/inject.exe <exe> <dll> [arguments]`: starts a program suspended, loads the DLL in it, then resumes it. Use the absolute path of the DLL. iCUE must be started with `--autorun`:
  ```
  inject.exe "C:\Program Files\Corsair\Corsair iCUE5 Software\iCUE.exe" C:\path\to\hook.dll --autorun
  ```
- `tools/sniff/injectpid.exe <pid> <dll>`: loads the DLL in a running process (for example iCUE started normally).
- `tools/rcc_extract.py <file.rcc> <folder>`: extracts a Qt resource file. The device files of iCUE (`devices/*.rcc`) contain the device manifest (`*.cuebmf`, JSON) with the LED and key indexes.
