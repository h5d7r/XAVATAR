#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
for tool in make python3 curl unzip clang-18 ld.lld-18; do
    if ! command -v "$tool" >/dev/null; then
        echo "Missing $tool. Install: sudo apt install make python3 curl unzip clang-18 lld-18" >&2
        exit 1
    fi
done

export PS4_PAYLOAD_SDK="${PS4_PAYLOAD_SDK:-$project_dir/.tools/ps4-payload-sdk}"
if [ ! -f "$PS4_PAYLOAD_SDK/toolchain/orbis.mk" ]; then
    if [ "$PS4_PAYLOAD_SDK" != "$project_dir/.tools/ps4-payload-sdk" ]; then
        echo "PS4_PAYLOAD_SDK does not contain an installed SDK: $PS4_PAYLOAD_SDK" >&2
        exit 1
    fi
    mkdir -p "$project_dir/.tools"
    sdk_archive="$project_dir/.tools/ps4-payload-sdk-v0.9.zip"
    echo "Downloading PS4 SDK v0.9..."
    curl -fL --retry 3 https://github.com/ps4-payload-dev/sdk/releases/download/v0.9/ps4-payload-sdk.zip -o "$sdk_archive"
    printf '%s  %s\n' '3190e88255a72161e5ce2f22a8ae8f638a2c5a9d1ff1e0209ed692d36b4d9ec5' "$sdk_archive" | sha256sum -c -
    unzip -q -o "$sdk_archive" -d "$project_dir/.tools"
fi

if [ "${1:-}" = '--clean' ]; then
    make -C "$project_dir" clean
elif [ "$#" -gt 0 ]; then
    echo 'Usage: tools/build.sh [--clean]' >&2
    exit 1
fi
make -C "$project_dir" package
echo "Payloads are ready in $project_dir/build and $project_dir/dist"
