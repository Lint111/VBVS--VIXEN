#!/usr/bin/env bash
# Build Mesa Dozen (Vulkan-over-D3D12) no-sudo into a cache dir, for GPU Vulkan on WSL2.
# Usage: provision-wsl-vulkan.sh <cache-dir>
# Idempotent: each stage skips if its output already exists. Exits non-zero on failure (the caller
# treats that as non-fatal and falls back to software Vulkan).
set -euo pipefail

CACHE="${1:?cache dir required}"
DEPS="$CACHE/deps"
MESA="$CACHE/mesa"
DZN_SO="$MESA/build/src/microsoft/vulkan/libvulkan_dzn.so"
ICD="$CACHE/dzn_icd.json"
MESA_TAG="mesa-25.2.8"

mkdir -p "$CACHE" "$DEPS"

# Already built? Nothing to do.
if [ -f "$DZN_SO" ] && [ -f "$ICD" ]; then
    echo "[provision-wsl-vulkan] cache hit: $DZN_SO"
    exit 0
fi

# 1) Python build tooling (user site; pip skips already-satisfied).
# --break-system-packages is required on Debian/Ubuntu 23.04+ (PEP 668 externally-managed guard);
# safe here because --user never touches the system Python prefix.
python3 -m pip install --user --quiet --break-system-packages meson mako ply
PYUSERBIN="$(python3 -c 'import site,os;print(os.path.join(site.getuserbase(),"bin"))')"
export PATH="$PYUSERBIN:$PATH"

# 2) No-sudo -dev deps: download the .debs and extract into $DEPS (headers + .so symlinks + .pc).
DEV_PKGS="bison flex libdrm-dev directx-headers-dev libvulkan-dev libexpat1-dev libx11-dev \
libx11-xcb-dev libxext-dev libxcb1-dev libxcb-dri3-dev libxcb-present-dev libxcb-sync-dev \
libxshmfence-dev libxrandr-dev libxcb-randr0-dev libxcb-shm0-dev libxcb-keysyms1-dev \
libxcb-image0-dev libxcb-render-util0-dev libxcb-render0-dev libxcb-shape0-dev libxcb-xfixes0-dev \
libxfixes-dev libxrender-dev"
if [ ! -d "$DEPS/usr/include" ]; then
    ( cd "$DEPS" && apt-get download $DEV_PKGS )
    for deb in "$DEPS"/*.deb; do dpkg -x "$deb" "$DEPS"; done
    rm -f "$DEPS"/*.deb
fi
export PATH="$DEPS/usr/bin:$PATH"                                   # bison/flex
# PKG_CONFIG_LIBDIR replaces (not appends to) the pkg-config search path so the extracted .pc files
# win over any system default. Append system fallbacks so deps already installed natively still resolve.
export PKG_CONFIG_LIBDIR="$DEPS/usr/lib/x86_64-linux-gnu/pkgconfig:$DEPS/usr/share/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig"
export LIBRARY_PATH="$DEPS/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"

# 3) Mesa source (shallow, pinned).
if [ ! -d "$MESA/.git" ]; then
    git clone --depth 1 --branch "$MESA_TAG" https://gitlab.freedesktop.org/mesa/mesa.git "$MESA"
fi

# 4) Configure Dozen only (exact proven flags).
# Guard on coredata.dat (written only on successful setup) not on the directory itself, so a
# partial/failed previous setup does not silently skip re-configuration.
INC="-I$DEPS/usr/include -I$DEPS/usr/include/x86_64-linux-gnu -I$DEPS/usr/include/libdrm -I/usr/include/drm"
if [ ! -f "$MESA/build/meson-private/coredata.dat" ]; then
    meson setup "$MESA/build" "$MESA" \
        -Dbuildtype=release -Dvulkan-drivers=microsoft-experimental -Dgallium-drivers= \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=false -Dplatforms=x11 \
        -Dvulkan-layers= -Dvideo-codecs= \
        "-Dc_args=$INC" "-Dcpp_args=$INC"
fi

# 5) Build.
ninja -C "$MESA/build" src/microsoft/vulkan/libvulkan_dzn.so

# 6) ICD manifest (absolute path to the built driver).
printf '{"file_format_version":"1.0.0","ICD":{"library_path":"%s","api_version":"1.3.0"}}\n' \
    "$DZN_SO" > "$ICD"
echo "[provision-wsl-vulkan] built $DZN_SO; ICD $ICD"
