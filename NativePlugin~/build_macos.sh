#!/usr/bin/env bash
# Builds GfxPluginMDI.bundle for macOS (universal arm64 + x86_64).
# CMake-free fallback — invoke from the NativePlugin~ directory.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build_macos"
PLUGINS="$ROOT/../Plugins/macOS"
BUNDLE="$PLUGINS/GfxPluginMDI.bundle"

mkdir -p "$BUILD"
cd "$BUILD"

CXX_FLAGS=(
    -arch arm64 -arch x86_64
    -mmacosx-version-min=10.13
    -std=c++17
    -fvisibility=hidden
    -O2 -fPIC
    -I"$SRC"
)

clang++ "${CXX_FLAGS[@]}" -c "$SRC/MultiDrawIndirect.cpp" -o MultiDrawIndirect.o
clang++ "${CXX_FLAGS[@]}" -c "$SRC/MDIBackend_Vulkan.cpp"  -o MDIBackend_Vulkan.o
clang++ "${CXX_FLAGS[@]}" -c "$SRC/MDIBackend_GLES.cpp"    -o MDIBackend_GLES.o
clang++ "${CXX_FLAGS[@]}" -fno-objc-arc -ObjC++ \
    -c "$SRC/MDIBackend_Metal.mm" -o MDIBackend_Metal.o

clang++ -arch arm64 -arch x86_64 -mmacosx-version-min=10.13 -bundle -fvisibility=hidden \
    -framework Foundation -framework Metal \
    -o GfxPluginMDI \
    MultiDrawIndirect.o MDIBackend_Vulkan.o MDIBackend_GLES.o MDIBackend_Metal.o

mkdir -p "$BUNDLE/Contents/MacOS"
cp -f GfxPluginMDI "$BUNDLE/Contents/MacOS/GfxPluginMDI"
chmod +x "$BUNDLE/Contents/MacOS/GfxPluginMDI"

cat > "$BUNDLE/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>GfxPluginMDI</string>
    <key>CFBundleIdentifier</key><string>com.saivs.gfxpluginmdi</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>GfxPluginMDI</string>
    <key>CFBundlePackageType</key><string>BNDL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
</dict>
</plist>
PLIST

echo "Built: $BUNDLE"
file "$BUNDLE/Contents/MacOS/GfxPluginMDI"
