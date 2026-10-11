#!/usr/bin/env bash
set -euo pipefail
git -C /mnt/c/vixen-native/brickcost fetch "$PWD" lane-brickcost
git -C /mnt/c/vixen-native/brickcost merge --ff-only FETCH_HEAD
cd /mnt/c
/mnt/c/Windows/System32/cmd.exe /c "C:\vixen-native\brickcost\reports\brickcost\reproduce\native-build.bat"
