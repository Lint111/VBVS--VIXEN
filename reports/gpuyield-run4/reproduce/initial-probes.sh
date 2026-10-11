set -uo pipefail
bash .tmp/gpuyield-run4/dozen-final.sh
rc=$?
printf 'Dozen recovery exit: %s\n' "$rc"
(cd /mnt/c/vixen-native/gpuyield && /mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\vixen-native\gpuyield\.tmp\native-pacing-run4.ps1')
winrc=$?
python3 - "$rc" "$winrc" <<'JSON'
import json,sys
from pathlib import Path
Path('reports/gpuyield-run4/initial-probes-status.json').write_text(json.dumps({'dozen_exit':int(sys.argv[1]),'native_pacing_exit':int(sys.argv[2])},indent=2)+'\n')
JSON
if (( rc != 0 || winrc != 0 )); then exit 1; fi
