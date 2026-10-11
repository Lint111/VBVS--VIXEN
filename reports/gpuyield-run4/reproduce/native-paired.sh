set -euo pipefail
bash /home/liory/.local/bin/with-test-lock.sh --status > reports/gpuyield-run4/native-paired-queue-status.txt
env -u DISPLAY -u WAYLAND_DISPLAY -u VK_ICD_FILENAMES -u LD_LIBRARY_PATH ctest --test-dir build/gpuyield -R '^(GpuChunkCost\.|SubmitRecord\.|MultiDispatchIntegration.ActualNode)' --output-on-failure --parallel 1 > reports/gpuyield-run4/hot-full-tile-targeted-tests.log 2>&1
(source .tmp/gpuyield/runtime-env.sh; VIXEN/.vulkan-sdk/1.4.350.1/x86_64/bin/vulkaninfo --summary > reports/gpuyield-run4/dozen-device-summary.log 2>&1)
cd /mnt/c
/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\vixen-native\gpuyield\.tmp\native-paired-run4.ps1'
