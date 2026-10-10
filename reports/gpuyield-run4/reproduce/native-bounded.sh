set -euo pipefail
git -C /mnt/c/vixen-native/gpuyield fetch "$PWD" lane-gpuyield
cd /mnt/c
/mnt/c/Windows/System32/cmd.exe /c "C:\vixen-native\gpuyield\.tmp\native-run4.bat"
