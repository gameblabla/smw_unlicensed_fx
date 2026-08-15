#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
archive="$project_dir/kit_blastem/blastem_headless_headerless_v3.zip"
source_name=blastem-7650b0eb0fa8
build_dir=$(mktemp -d /tmp/smb-md-sfx-renderer.XXXXXX)

unzip -q "$archive" -d "$build_dir"
patch -d "$build_dir/$source_name" -p1 < "$project_dir/tools/md_sfx_headless_audio.patch"
make -C "$build_dir/$source_name" headless
install -m 0755 "$build_dir/$source_name/blastem" \
    "$project_dir/kit_blastem/md_sfx_renderer_fixed"

echo "Built kit_blastem/md_sfx_renderer_fixed"
