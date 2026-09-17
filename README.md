# Test Drive (1987) Enhanced Edition — PlayStation®Vita Port

A native, hardware-accelerated PlayStation®Vita and PlayStation®TV port of **Test Drive (1987)**, based on the static recompilation and enhanced engine by **kylofon**.

This port brings Accolade's seminal 1987 PC racing classic to the PS Vita with native 60 FPS presentation via VitaGL / GXM hardware acceleration, authentic DOS sound synthesis, responsive physical controls, dynamic multi-mode aspect scaling, and a Sony TRC-compliant LiveArea digital user manual.

---

## Features

- **Hardware Acceleration**: Smooth 60 FPS presentation powered by VitaSDK's SDL2 with VitaGL/GXM GPU backend.
- **Tri-Mode Aspect Ratio Engine**: Toggle between 3 display modes in real time with the **`Select`** button:
  - **Mode 1 (Default)**: 4:3 Aspect-Correct ($725 \times 544$ pillarboxed) — authentic retro CRT proportion with full dashboard gauge visibility.
  - **Mode 2**: 2× Integer Scale ($640 \times 480$ centered) — razor-sharp pixel doubling matching original DOS EGA scanlines.
  - **Mode 0**: 16:9 Widescreen Stretch ($960 \times 544$) — full edge-to-edge panoramic view filling the 5-inch OLED/LCD screen.
- **Ergonomic Vita Controls**: Full support for the Left Analog Stick, D-Pad, face buttons, and shoulder trigger pedals (R Trigger for Gas, L Trigger for Brake, Triangle for Shift Up, Square/Circle for Shift Down).
- **Hybrid Filesystem & Persistent Saves**: High scores (`SCORES`) and user mod files are written directly to `ux0:data/TestDrive/`, avoiding Sony's read-only `app0:` restriction.
- **Built-in LiveArea User Manual**: A complete 5-page digital manual installed to `sce_sys/manual/`, accessible directly from the LiveArea book icon.

---

## Important Notice: Game Data Setup

> [!IMPORTANT]
> **Proprietary game assets are NOT bundled in the release VPK.**
> To comply with copyright laws, you must provide the original game data from your legally owned copy of the original 1987 DOS version of *Test Drive* by Accolade.

### How to Install Game Files

1. Install `TestDriveVita.vpk` on your PS Vita using **VitaShell**.
2. Launch **VitaShell** and press **`Select`** to start a **USB** or **FTP** connection.
3. On your PS Vita memory card, navigate to:
   ```
   ux0:data/TestDrive/
   ```
   *(This directory is created automatically when the game first launches, or you can create it manually).*
4. Copy all files from your original DOS version into `ux0:data/TestDrive/`.

### Required Files Checklist

| File | Type | Description |
| :--- | :--- | :--- |
| **`TDEGA.EXE`** | **Required** | Primary EGA game executable (needed to boot) |
| **`*.CMP`** | **Required** | Compressed car models, cockpits, and scenery |
| **`*.PES`** | **Required** | Palette and screen layout files |
| **`*.BIN`** | **Required** | Precalculated instrument and gauge tables |
| **`*.SS`** | **Required** | Sprite sequences (opponents, police cruisers) |
| **`TDSND.SND`** | **Required** | Engine audio and collision sound effects |
| **`SCORES`** | Optional | High score table (auto-generated if missing) |
| **`CARS.TXT`** | Optional | Car performance specifications and text |

*Note: The game engine resolves filenames case-insensitively, so both lowercase (`tdega.exe`) and uppercase (`TDEGA.EXE`) work seamlessly.*

---

## Controls

### In-Game Driving

| Input | Action | Description |
| :--- | :--- | :--- |
| **Left Stick** or **D-Pad ◄ / ►** | **Steer** | Turn vehicle left and right with analog precision |
| **Cross ($\times$)** / **R Trigger** / **D-Pad ▲** | **Gas (Accelerate)** | Depress accelerator pedal |
| **Square ($\square$)** / **L Trigger** / **D-Pad ▼** | **Brake** | Apply vehicle brakes |
| **Triangle ($\triangle$)** | **Shift Up** | Shift manual transmission to higher gear |
| **Square ($\square$)** or **Circle ($\bigcirc$)** | **Shift Down** | Shift manual transmission to lower gear |
| **Select** | **Cycle Display Mode** | Switch between 4:3, 2× Integer, and 16:9 |
| **Start** | **Pause / Menu** | Pause gameplay or exit current drive (`Esc`) |

### Menu Navigation

| Input | Action |
| :--- | :--- |
| **D-Pad ▲ / ▼** | Highlight menu options and vehicle selection |
| **Cross ($\times$)** | Confirm selection / DOS `Enter` key |
| **Circle ($\bigcirc$)** | Toggle options / DOS `Spacebar` key |
| **Start** | Cancel / Return / DOS `Esc` key |

### Driving Tips
- **Watch the Tachometer (RPM)**: You must shift up before reaching the red line. Revving past redline for more than 2 seconds will **blow your engine**, ending the run!
- **Radar Detector**: When your radar detector beeps and flashes, immediately tap the brake to drop below the speed limit before the highway patrol cruiser spots you!

---

## Building from Source

### Prerequisites
- [Docker](https://www.docker.com/) (recommended) or a local installation of [VitaSDK](https://vitasdk.org/).

### Build using Docker (One-Liner)

```bash
# Clone the repository
git clone https://github.com/gainusha/TestDriveVita.git
cd TestDriveVita

# Build VPK with VitaSDK Docker container
docker run --platform linux/amd64 --rm -v "$(pwd):/src" -w /src vitasdk/vitasdk bash -c "
  cmake -B build-vita -DCMAKE_TOOLCHAIN_FILE=/usr/local/vitasdk/share/vita.toolchain.cmake -DBUNDLE_GAME_DATA=OFF &&
  cmake --build build-vita
"
```

The output package `TestDriveVita.vpk` will be generated in the root directory.

### CMake Build Options

- `-DBUNDLE_GAME_DATA=OFF` *(Default)*: Builds a clean, standalone VPK without bundled DOS assets.
- `-DBUNDLE_GAME_DATA=ON` : Automatically bundles local files in `Game/` into the VPK under `app0:Game/`. You need to provide the files from your own copy of the game

---

## Credits & Acknowledgments

- **[kylofon](https://github.com/kylofon)** — Creator of the exceptional [**testdrive-enhanced**](https://github.com/kylofon/testdrive-enhanced) static recompilation and the original reverse-engineering work in [**test-drive-sdl3**](https://github.com/kylofon/test-drive-sdl3). This port is built directly upon their incredible reverse-engineering efforts and enhanced C codebase.
- **Accolade & Distinctive Software (DSI)** — Creators of the original legendary *Test Drive* (1987) game.
- **VitaSDK Team** — For the open-source cross-compiler toolchain and Sony PS Vita homebrew libraries.
- **Vita3K Team** — For the PlayStation Vita emulator facilitating development and testing.
