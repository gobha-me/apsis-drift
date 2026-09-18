#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
visual_dir="$project_root/assets/visual"
board_tmp="$(mktemp -d -t apsis-hero-board.XXXXXXXX)"
# Full frames are fitted, never cropped. Reset virtual canvases before labeling.
for asset in cockpit ship station; do
    for view in 01 02; do
        dimensions="$(magick identify -format '%wx%h' "$visual_dir/hero-$asset-view-$view.png")"
        if [[ "$dimensions" != '2560x1440' ]]; then
            printf 'Expected final 2560x1440 render for %s/%s, got %s\n' "$asset" "$view" "$dimensions" >&2
            exit 1
        fi
        magick "$visual_dir/hero-$asset-view-$view.png" +repage \
            -resize 1440x810 -background '#08131d' -gravity center -extent 1440x810 +repage \
            "$board_tmp/$asset-$view.png"
    done
    magick "$board_tmp/$asset-01.png" "$board_tmp/$asset-02.png" +append +repage \
        -background '#0b1c28' -gravity north -splice 0x82 +repage \
        -font DejaVu-Sans -fill '#e4f2f5' -pointsize 31 \
        -annotate +0+22 "${asset^^}  /  PRIMARY VIEW + DETAIL REVIEW" \
        "$board_tmp/$asset-row.png"
done
magick "$board_tmp/cockpit-row.png" "$board_tmp/ship-row.png" "$board_tmp/station-row.png" \
    -append +repage -background '#05101a' -gravity north -splice 0x156 +repage \
    -font DejaVu-Sans -fill '#e4f2f5' -pointsize 50 -annotate +0+29 'APSIS DRIFT  /  HERO ASSET STUDIES' \
    -fill '#68c4dc' -pointsize 24 -annotate +0+100 'REVISION 02     |     BLENDER / CYCLES     |     EDITABLE GEOMETRY + FOUR LOD TIERS' \
    "$visual_dir/hero-design-board.png"
printf 'Board: %s\nWorking panels retained in: %s\n' "$visual_dir/hero-design-board.png" "$board_tmp"

for asset in cockpit ship station; do
    for tier in near mid far; do
        magick "$visual_dir/hero-$asset-lod-$tier.png" +repage \
            -background '#0b1c28' -gravity north -splice 0x58 +repage \
            -font DejaVu-Sans -fill '#bfe5ee' -pointsize 22 \
            -annotate +0+17 "${asset^^} / ${tier^^}" "$board_tmp/$asset-$tier.png"
    done
    magick "$board_tmp/$asset-near.png" "$board_tmp/$asset-mid.png" "$board_tmp/$asset-far.png" \
        +append +repage "$board_tmp/$asset-lods.png"
done
magick "$board_tmp/cockpit-lods.png" "$board_tmp/ship-lods.png" "$board_tmp/station-lods.png" \
    -append +repage -background '#05101a' -gravity north -splice 0x125 +repage \
    -font DejaVu-Sans -fill '#e4f2f5' -pointsize 40 -annotate +0+22 'APSIS DRIFT / GEOMETRY REDUCTION REVIEW' \
    -fill '#68c4dc' -pointsize 21 -annotate +0+81 'ACTUAL GLB ROUND TRIPS / MATERIAL FACTORS / IDENTICAL CAMERAS' \
    "$visual_dir/hero-lod-board.png"
