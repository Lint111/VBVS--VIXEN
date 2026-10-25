#!/bin/sh
# Compiler launcher for GCC targets with a precompiled header (vixen_cache_compatible_pch).
# Ordinary compiles pass straight through. The PCH compile itself (its -o names a .gch) goes
# through vixen_pch_checksum.py, which also writes the <pch>.gch.sum that ccache hashes instead
# of the .gch bytes (CCACHE_PCH_EXTSUM).
# Usage: vixen_pch_launcher.sh <python> <launcher and compiler command...>
python="$1"; shift
case " $* " in
    *".gch "*) exec "$python" "$(dirname "$0")/vixen_pch_checksum.py" "$@" ;;
    *) exec "$@" ;;
esac
