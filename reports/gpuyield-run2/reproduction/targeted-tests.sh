set -euo pipefail
python3 .tmp/gpuyield-run2/filtered-contract-tests.py
python3 .tmp/gpuyield-run2/metric-smoke.py
