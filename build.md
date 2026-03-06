## 🛠️ Build & Run Instructions

These steps assume you’re on **macOS (Apple Silicon)**, but they’ll work on Linux too with small path adjustments.

---

### 1. 📦 Install Dependencies

Use Homebrew (or your package manager of choice):

```bash
brew install cmake antlr opencascade
```

> Note: the `antlr` command installs the ANTLR4 tool used by CMake to generate the lexer/parser.  
> The C++ runtime (`antlr4-runtime`) is included via CMake’s `find_package`.

---

### 2. 🧩 Project Structure

Expected layout:
```
opendcad/
├── CMakeLists.txt
├── grammar/
│   └── OpenDCAD.g4
├── src/
│   ├── opendcad.cpp
│   ├── output.cpp
│   └── (future source files)
└── build/                # Generated automatically by CMake
```

---

### 3. ⚙️ Configure the Project with CMake

Run this once to generate build files:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix opencascade)"
```

Explanation:
- `-S .` = source directory  
- `-B build` = build directory  
- `-DCMAKE_PREFIX_PATH` ensures CMake finds OpenCascade (needed on macOS/Homebrew)

CMake will:
- Generate ANTLR C++ sources from `grammar/OpenDCAD.g4`
- Compile them with your `src/*.cpp` files
- Link against `antlr4_shared` and `OpenCascade`

---

### 4. 🧱 Build the Executable

```bash
cmake --build build --config Release
```

That compiles everything and creates:
```
build/bin/opendcad
```

---

### 5. ▶️ Run the Program

```bash
# Basic usage — outputs STEP + STL + JSON to build/
./build/bin/opendcad examples/battery.dcad

# Custom output directory
./build/bin/opendcad examples/battery.dcad -o output/

# STEP only (no STL), production quality
./build/bin/opendcad examples/battery.dcad --fmt step -q production

# List available exports without building
./build/bin/opendcad examples/battery.dcad --list-exports

# See all options
./build/bin/opendcad --help
```

Output files per export: `name.step`, `name.stl` (optional), `name.json` (metadata manifest).

Open them in **FreeCAD**, **CAD Assistant**, or any STEP/STL viewer to confirm geometry.

---

### 6. ♻️ (Optional) Clean Rebuild

If you modify the grammar or CMake files:
```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix opencascade)"
cmake --build build --config Release
```

---

### ✅ Summary

| Step | Command | Purpose |
|------|----------|----------|
| 1 | `brew install cmake antlr opencascade` | Install dependencies |
| 2 | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix opencascade)"` | Configure build system |
| 3 | `cmake --build build --config Release` | Compile project |
| 4 | `./build/bin/opendcad examples/battery.dcad` | Run a script |

---

You now have a reproducible, fully automated C++ build system for **OpenDCAD**, integrating:
- ANTLR grammar generation
- OpenCascade geometry and export
- STEP/STL verification output
