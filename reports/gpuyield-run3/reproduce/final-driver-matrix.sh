set -uo pipefail
bash .tmp/gpuyield-run3/native-matrix.sh
native_rc=$?
bash .tmp/gpuyield-run3/dozen-final.sh
dozen_rc=$?
printf "native_matrix_exit=%s dozen_exit=%s\n" "$native_rc" "$dozen_rc"
(( native_rc == 0 && dozen_rc == 0 ))
