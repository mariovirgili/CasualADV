# CasualADV

CasualADV is a PlatformIO project for the M5Stack Cardputer / StampS3 platform.

The firmware bundles three mini-games:

- FallTris
- RockADV
- PuzzleBall

It also includes SD-card based configuration and optional MP3 playlist support.

## Requirements

- PlatformIO
- Board environment: `m5stack-stamps3`

## Build

```bash
pio run -e m5stack-stamps3
```

## Project Layout

- `src/main.cpp`: main menu, app switching, SD configuration, MP3 options
- `src/AudioTask.*`: background audio playback
- `src/FallTris.*`: FallTris game
- `src/RockADV.*`: RockADV game
- `src/PuzzleBall.*`: PuzzleBall game

## Notes

- Build artifacts under `.pio/` are excluded from git.
- Local media files such as MP3 and MP4 are excluded from git by default.
