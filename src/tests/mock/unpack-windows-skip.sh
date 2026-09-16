#!/bin/sh
# Unpack mock trees that cannot be stored as git paths on Windows (':' / '\').
#   sh src/tests/mock/unpack-windows-skip.sh        # skip if already unpacked
#   sh src/tests/mock/unpack-windows-skip.sh -f     # replace from tar (refresh)
set -eu
FORCE=0
for arg in "$@"; do
	case "$arg" in
	-f|--force) FORCE=1 ;;
	-h|--help)
		echo "usage: unpack-windows-skip.sh [-f|--force]"
		exit 0
		;;
	*)
		echo "unpack-windows-skip.sh: unknown option: $arg" >&2
		exit 1
		;;
	esac
done

MOCKROOT="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
LIST="${MOCKROOT}/windows-ntfs-skip.list"
TAR="${MOCKROOT}/windows-ntfs-skip.tar"

if [ ! -f "$TAR" ]; then
	echo "unpack-windows-skip.sh: missing $TAR" >&2
	exit 1
fi
if [ ! -f "$LIST" ]; then
	echo "unpack-windows-skip.sh: missing $LIST" >&2
	exit 1
fi

first=""
while IFS= read -r rel || [ -n "${rel:-}" ]; do
	[ -z "$rel" ] && continue
	case "$rel" in
	\#*) continue ;;
	esac
	first="$rel"
	break
done < "$LIST"

if [ "$FORCE" -eq 0 ] && [ -n "$first" ] && [ -e "${MOCKROOT}/${first}" ]; then
	exit 0
fi

if [ "$FORCE" -eq 1 ]; then
	while IFS= read -r rel || [ -n "${rel:-}" ]; do
		[ -z "$rel" ] && continue
		case "$rel" in
		\#*) continue ;;
		esac
		rm -rf "${MOCKROOT}/${rel}"
	done < "$LIST"
fi

tar -C "$MOCKROOT" -xf "$TAR"
