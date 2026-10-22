#!/usr/bin/env bash
# Prefer the runner's existing Ubuntu fallback; BSD-3-Clause.
set -euo pipefail

# https://github.com/actions/runner-images/issues/12949
# The image supplies these mirrors in /etc/apt/apt-mirrors.txt.
mirror_file=${1:-/etc/apt/apt-mirrors.txt}
azure='^http://azure\.archive\.ubuntu\.com/ubuntu/[[:space:]]+priority:[12]$'
archive='^https://archive\.ubuntu\.com/ubuntu/[[:space:]]+priority:[12]$'
if ! grep -Eq "$azure" "$mirror_file" || ! grep -Eq "$archive" "$mirror_file"; then
    echo 'Unsupported runner APT mirror list; refusing to rewrite it.' >&2
    exit 1
fi

# Keep Azure as fallback and preserve the security mirror and other sources.
sed -i \
    -e '/^http:\/\/azure\.archive\.ubuntu\.com\/ubuntu\//s/priority:1$/priority:2/' \
    -e '/^https:\/\/archive\.ubuntu\.com\/ubuntu\//s/priority:2$/priority:1/' \
    "$mirror_file"
