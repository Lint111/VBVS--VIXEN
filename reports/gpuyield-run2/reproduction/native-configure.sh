set -euo pipefail
repo_win="$(wslpath -w "$PWD")"
cd /mnt/c
/mnt/c/Windows/System32/cmd.exe /c "$repo_win\build.bat" configure vixen-ninja
