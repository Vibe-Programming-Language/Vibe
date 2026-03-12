# Building Vibe for Windows

## Prerequisites

| Tool | Where to get it |
|------|----------------|
| **Visual Studio Build Tools 2022** (C++ workload) *or* Visual Studio Community | https://visualstudio.microsoft.com/downloads/ |
| **CMake ≥ 3.20** | https://cmake.org/download/ |
| **Ninja** (optional, faster builds) | `winget install Ninja-build.Ninja` |
| **NSIS** (optional, for installer) | https://nsis.sourceforge.io/Download |

## 1. Build from Source

Open **Developer Command Prompt for VS 2022** (or PowerShell with vcvars loaded):

```powershell
# Clone / navigate to repo root
cd nova

# Configure (use Ninja for speed, or omit -G for default VS generator)
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release -j

# Verify
.\build\vibe.exe version
```

Output: `build\vibe.exe` (~500 KB statically linked).

### MinGW Alternative

If you prefer MinGW-w64 (e.g. via MSYS2):

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 2. Create a ZIP Release

```powershell
mkdir vibe-1.0.0-windows-x64
copy build\vibe.exe vibe-1.0.0-windows-x64\
copy README.md vibe-1.0.0-windows-x64\
xcopy examples vibe-1.0.0-windows-x64\examples\ /E /I

# Create archive (PowerShell 5+)
Compress-Archive -Path vibe-1.0.0-windows-x64 -DestinationPath vibe-1.0.0-windows-x64.zip
```

Distribute `vibe-1.0.0-windows-x64.zip` — users extract and add the folder to PATH.

## 3. Create an Installer (NSIS)

The `installer.nsi` script in this folder produces a click-through `vibe-setup.exe` that:

- Installs `vibe.exe` to `%LOCALAPPDATA%\Vibe`
- Adds Vibe to the user's PATH (no admin required)
- Provides an uninstaller that removes files and restores PATH
- Bundles README and example programs

### Build the installer

1. Install [NSIS](https://nsis.sourceforge.io/Download)
2. Make sure `build\vibe.exe` exists (see step 1)
3. Right-click `installer.nsi` → **Compile NSIS Script**
   — or from command line:
   ```powershell
   makensis installer.nsi
   ```
4. Output: `vibe-setup.exe` (~600 KB)

## 4. Add Vibe to PATH Manually

If you don't want the installer, add Vibe to PATH manually:

1. Copy `vibe.exe` to a permanent location (e.g. `C:\Tools\Vibe\`)
2. Open **Settings → System → About → Advanced system settings → Environment Variables**
3. Under **User variables**, edit `Path` and add `C:\Tools\Vibe\`
4. Open a new terminal and run `vibe version`

## Cross-Compiling from Linux

You can cross-compile for Windows from Linux using MinGW:

```bash
sudo apt install mingw-w64
cd nova
cmake -S . -B build-win \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j
# Output: build-win/vibe.exe
```

This produces a Windows .exe you can distribute directly.

