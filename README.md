# Sky-Pebble

Analog watchface with GMT and annual calendar complications for the Pebble Round 2.

![Sky-Pebble](screenshot_gabbro.png)

## Features

- Skeletonized hour and minute hands with transparent cutouts and luminous strips
- Tapered seconds hand with motion-activated auto-disable (hides after 30s of inactivity for battery savings)
- 24-hour GMT ring showing UTC time with red/white triangle pointer
- Month indicator ring (current month highlighted in red)
- Date window at 3 o'clock with magnifier lens — tap wrist to show battery level (color-coded by charge) ![Battery](screenshot_battery.png)
- Bluetooth disconnect indicator (red Bluetooth icon in date window + vibration alert) ![Bluetooth Disconnect](screenshot_bluetooth.png)
- "SKY-PEBBLE" brand text and decorative marks
- Clean polished dial edge
- Earth/ZULU icon at 12 o'clock
- Minute track with fine white tick marks

All graphics rendered procedurally in C with antialiased drawing, except the 24-hour GMT ring which uses a pre-rendered bitmap.

## Platform

- **Target**: Pebble Round 2 (gabbro, 260x260, 64-color, round)
- **SDK**: PebbleOS SDK 4.9.148

## Build

```bash
pebble build
pebble install --emulator gabbro
```

## Author

Evan Henry Jacobs