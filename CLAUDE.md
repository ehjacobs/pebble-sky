# Sky GMT — Pebble Time 2 Watchface

Analog GMT watchface with modern complications for the Pebble Time 2 (platform codename: **emery**).

## Project Overview

A watchface featuring analog hands, 24-hour subdial, month indicator ring, date window, and fluted bezel — all rendered procedurally in C on a 200x228 64-color e-paper display.

## Tech Stack

- **Language**: C (Pebble SDK)
- **SDK**: PebbleOS SDK 4.9.148 via `pebble-tool` (installed with `uv tool install pebble-tool --python 3.13`)
- **Target**: Emery platform (Pebble Time 2, 200x228, 64-color, rectangular)
- **Build**: `pebble build` (waf-based, produces `build/pebble-sky.pbw`)
- **Test**: `pebble install --emulator emery` (QEMU)

## Build & Test Commands

```bash
# Build
pebble build

# Install in emulator (requires qemu-pebble on PATH — see Toolchain note below)
pebble install --emulator emery

# Capture screenshot
pebble screenshot --no-open --emulator emery screenshot_emery.png

# View logs
pebble logs --emulator emery

# Install on physical watch
pebble install --cloudpebble
```

### Toolchain PATH Setup

The QEMU emulator and toolchain live inside the SDK directory. You need these on PATH for emulator commands:

```bash
export PATH="$HOME/.local/bin:$HOME/Library/Application Support/Pebble SDK/SDKs/4.9.148/toolchain/bin:$PATH"
export DYLD_LIBRARY_PATH="$HOME/Library/Application Support/Pebble SDK/SDKs/4.9.148/toolchain/lib"
```

## Project Structure

```
src/c/main.c          # All watchface code (single-file C)
package.json          # Pebble project manifest (UUID, platform targets)
wscript               # Build configuration (waf)
resources/            # Fonts, images (currently empty — all procedural)
build/pebble-sky.pbw  # Built watchface artifact
```

## Key Constraints (Pebble C Development)

- **No floating point** — use `sin_lookup()` / `cos_lookup()` with `TRIG_MAX_ANGLE` (65536 = full circle) and `TRIG_MAX_RATIO` (65536)
- **MINUTE_UNIT only** — always use `tick_timer_service_subscribe(MINUTE_UNIT, ...)`. Never use `SECOND_UNIT` unless explicitly adding a seconds hand (battery drain)
- **Pre-allocate GPath** — create paths in `window_load`, destroy in `window_unload`
- **Dynamic bounds** — use `layer_get_bounds()`, never hardcode 200x228
- **64-color palette** — use `GColor*` constants (e.g., `GColorOxfordBlue`, `GColorWhite`)
- **Warnings are errors** — the compiler uses `-Werror`. No unused variables, no multi-line comments with backslashes

## Design Elements

The watchface features these classic GMT complications:

1. **Analog hands** — skeletonized baton-style hour/minute hands (drawn as 6-point GPath polygons with a center slit)
2. **24-hour subdial** — off-center rotating disc in upper dial showing home timezone, with red inverted triangle pointer
3. **Month indicator ring** — 12 small squares around the dial at each hour position; current month shown in red, others white (Jan=1 o'clock through Dec=12 o'clock)
4. **Date window** — white rectangle at 3 o'clock showing day of month
5. **Fluted bezel** — alternating light/dark radial lines around outer edge
6. **Baton indices** — rectangular hour markers, thicker at quarter hours

## Git Workflow

The user manages their own commits. Claude should suggest commit messages but never run `git commit`, `git push`, or `gh repo create` directly.

## Skill Reference

The `.claude/skills/pebble-watchface/` directory contains the Pebble watchface agent skill with:
- `reference/pebble-api-reference.md` — full Pebble C API docs
- `reference/drawing-guide.md` — graphics primitives, coordinate system, patterns
- `reference/animation-patterns.md` — animation techniques
- `templates/` — starter templates for static, animated, and weather watchfaces
- `scripts/` — icon generation, GIF preview, project validation
