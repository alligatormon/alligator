#!/bin/sh
# Pack mock trees that cannot be stored as git paths on Windows (':' / '\').
# Refresh after editing unpacked files:
#   sh src/tests/mock/unpack-windows-skip.sh
#   # edit files under the paths in windows-ntfs-skip.list
#   sh src/tests/mock/pack-windows-skip.sh
set -eu
MOCKROOT="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
LIST="${MOCKROOT}/windows-ntfs-skip.list"
TAR="${MOCKROOT}/windows-ntfs-skip.tar"
TMP="${TAR}.tmp"

if [ ! -f "$LIST" ]; then
	echo "pack-windows-skip.sh: missing $LIST" >&2
	exit 1
fi

missing=0
while IFS= read -r rel || [ -n "${rel:-}" ]; do
	[ -z "$rel" ] && continue
	case "$rel" in
	\#*) continue ;;
	esac
	if [ ! -e "${MOCKROOT}/${rel}" ]; then
		echo "pack-windows-skip.sh: missing ${rel} (unpack first)" >&2
		missing=1
	fi
done < "$LIST"
if [ "$missing" -ne 0 ]; then
	exit 1
fi

# shellcheck disable=SC2034
set --
while IFS= read -r rel || [ -n "${rel:-}" ]; do
	[ -z "$rel" ] && continue
	case "$rel" in
	\#*) continue ;;
	esac
	set -- "$@" "$rel"
done < "$LIST"

# macOS tar otherwise stores AppleDouble ._* members; Linux tests do not need them.
COPYFILE_DISABLE=1 COPY_EXTENDED_ATTRIBUTES_DISABLE=1 \
	tar -C "$MOCKROOT" --exclude='._*' --exclude='.DS_Store' -cf "$TMP" "$@"
mv "$TMP" "$TAR"
echo "Wrote $TAR"
