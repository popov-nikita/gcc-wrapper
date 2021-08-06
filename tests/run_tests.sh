#!/bin/bash

# set -x
set -e -u -o pipefail

prepare_sources() {
	local dir="$1"
	local tgz_ball="$2"

	if test -d "$dir"; then
		rm -r -f "$dir";
	fi
	mkdir -p "$dir"
	tar -x --strip-components=1 -f "$tgz_ball" -C "$dir"
	return 0
}

SDIR="$(dirname "$0")"
APR="apr_1.6.3.orig.tar.bz2"
NPROC="$(grep -c '^processor' /proc/cpuinfo)"
export CC="$(readlink -f "${SDIR}/../gcc-wrapper")"

prepare_sources "${SDIR}/APR" "${SDIR}/${APR}"
cd "${SDIR}/APR"

./configure
make "-j${NPROC}"

replace_c_files() {
	local orig_file
	local file

	for file in $(find -name '*._i_.c'); do
		orig_file="${file%._i_.c}.c"
		echo "Replacing ${orig_file}"
		cp "$orig_file" "${orig_file}.orig"
		cp "$file" "$orig_file"
	done
	return 0
}

replace_c_files

make "-j${NPROC}" clean
make "-j${NPROC}"

echo "TEST #1: Successful"

exit 0
