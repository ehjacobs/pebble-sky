# Sky GMT — Pebble Round 2 Watchface

Analog watchface with GMT and annual calendar complications for the Pebble Round 2 (platform codename: **gabbro**).

## Project Overview

A watchface featuring analog hands, 24-hour subdial, month indicator ring, date window, and clean polished dial edge — all rendered procedurally in C on a 260x260 64-color round display.

## Tech Stack

- **Language**: C (Pebble SDK)
- **SDK**: PebbleOS SDK 4.9.148 via `pebble-tool` (installed with `uv tool install pebble-tool --python 3.13`)
- **Target**: Gabbro platform (Pebble Round 2, 260x260, 64-color, round)
- **Build**: `pebble build` (waf-based, produces `build/pebble-sky.pbw`)
- **Test**: `pebble install --emulator gabbro` (QEMU)

## Build & Test Commands

```bash
# Build
pebble build

# Install in emulator (requires qemu-pebble on PATH — see Toolchain note below)
pebble install --emulator gabbro

# Capture screenshot
pebble screenshot --no-open --emulator gabbro screenshot_gabbro.png

# View logs
pebble logs --emulator gabbro

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
resources/            # GMT disc bitmap (gmt_disc.png, 154×154 rotatable 24h ring)
build/pebble-sky.pbw  # Built watchface artifact
```

## Key Constraints (Pebble C Development)

- **No floating point** — use `sin_lookup()` / `cos_lookup()` with `TRIG_MAX_ANGLE` (65536 = full circle) and `TRIG_MAX_RATIO` (65536)
- **SECOND_UNIT** — currently using `tick_timer_service_subscribe(SECOND_UNIT, ...)` for the seconds hand. Switch back to `MINUTE_UNIT` if seconds hand is removed (battery drain)
- **Pre-allocate GPath** — create paths in `window_load`, destroy in `window_unload`
- **Dynamic bounds** — use `layer_get_bounds()`, never hardcode 260x260
- **Antialiasing** — call `graphics_context_set_antialiased(ctx, true)` for crisp edges; use odd stroke widths (1, 3, 5) for best pixel alignment
- **64-color palette** — use `GColor*` constants (e.g., `GColorOxfordBlue`, `GColorWhite`)
- **Warnings are errors** — the compiler uses `-Werror`. No unused variables, no multi-line comments with backslashes

## Design Elements

The watchface features these classic GMT complications:

1. **Analog hands** — skeletonized hour/minute hands with transparent cutouts (4-piece fill: base, left rail, right rail, outer tip) and luminous strips, plus tapered seconds hand
2. **24-hour GMT ring** — off-center rotating disc showing UTC time (via `gmtime()`), with red/white inverted triangle pointer
3. **Month indicator ring** — 12 small squares around the dial at each hour position; current month shown in red, others white (Jan=1 o'clock through Dec=12 o'clock)
4. **Date window** — white rectangle at 3 o'clock showing day of month with lens
5. **Clean dial edge** — polished light/dark concentric rings (no fluted bezel)
6. **Baton indices** — rectangular hour markers with light grey border, uniform size at all positions (skipping 12 and 3 o'clock)
7. **Brand text** — "SKY-PEBBLE" below center, decorative marks at bottom flanking 6 o'clock
8. **Earth/ZULU** — globe icon with "ZULU" label at 12 o'clock

## Git Workflow

The user manages their own commits. Claude should suggest commit messages but never run `git commit`, `git push`, or `gh repo create` directly.

## Skill Reference

The `.claude/skills/pebble-watchface/` directory contains the Pebble watchface agent skill with:
- `reference/pebble-api-reference.md` — full Pebble C API docs
- `reference/drawing-guide.md` — graphics primitives, coordinate system, patterns
- `reference/animation-patterns.md` — animation techniques
- `templates/` — starter templates for static, animated, and weather watchfaces
- `scripts/` — icon generation, GIF preview, project validation
