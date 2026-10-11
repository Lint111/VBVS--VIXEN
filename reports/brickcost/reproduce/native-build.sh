#!/usr/bin/env bash
set -euo pipefail
git -C /mnt/c/vixen-native/brickcost fetch "$PWD" lane-brickcost
cd /mnt/c
/mnt/c/Windows/System32/cmd.exe /c "C:\vixen-native\brickcost\reports\brickcost\reproduce\native-build.bat"
