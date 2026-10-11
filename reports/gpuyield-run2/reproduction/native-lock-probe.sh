set -euo pipefail
batch="$(wslpath -w "$PWD/.tmp/gpuyield-run2/native-lock-probe.bat")"
cd /mnt/c
/mnt/c/Windows/System32/cmd.exe /c "$batch"
