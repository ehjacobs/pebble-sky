# Sky GMT

Analog GMT watchface with modern complications for the Pebble Round 2.

![Sky GMT](screenshot_gabbro.png)

## Features

- Skeletonized baton-style hour and minute hands
- 24-hour subdial with rotating disc and red triangle pointer
- Month indicator ring (current month highlighted in red)
- Date window at 3 o'clock
- Clean polished dial edge
- Earth/ZULU icon at 12 o'clock
- Minute track with fine tick marks

All graphics rendered procedurally in C with antialiased drawing.

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