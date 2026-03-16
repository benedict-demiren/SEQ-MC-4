#!/bin/bash
set -e
cd "$(dirname "$0")/.."

# Configure if needed
if [ ! -f build/CMakeCache.txt ]; then
    echo "=== Configuring CMake ==="
    cmake -B build -DCMAKE_BUILD_TYPE=Release
fi

# Build
echo "=== Building SEQ-MC-4 ==="
cmake --build build --config Release -j$(sysctl -n hw.ncpu)

echo ""
echo "=== Build complete ==="
echo "VST3: $(find build -name '*.vst3' -maxdepth 4 2>/dev/null | head -1)"
echo "AU:   $(find build -name '*.component' -maxdepth 4 2>/dev/null | head -1)"
echo "CLAP: $(find build -name '*.clap' -maxdepth 4 2>/dev/null | head -1)"
echo "App:  $(find build -name '*.app' -maxdepth 4 2>/dev/null | head -1)"
