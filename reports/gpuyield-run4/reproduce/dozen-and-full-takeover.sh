set -uo pipefail
bash .tmp/gpuyield-run4/r6-and-dozen-takeover.sh
echo "r6-and-dozen exit: $?"
bash .tmp/gpuyield-run4/full-suite-takeover.sh
echo "full-suite-takeover exit: $?"
