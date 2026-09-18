#!/usr/bin/env bash
# Reproducible 40-second offline Godot film. Keeps source PNGs and measurements.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
engine="${GODOT_BIN:-${repo_dir}/build-godot/tools/Godot_v4.7.2-stable_linux.x86_64}"
if [[ ! -x "$engine" ]]; then
    echo 'Set GODOT_BIN to an installed Godot 4 executable.' >&2
    exit 1
fi
for program in cmake ffmpeg ffprobe python rg timeout; do command -v "$program" >/dev/null; done
if [[ $# != 1 || -e "$1" ]]; then
    echo 'Usage: tools/render_freedom_film.sh NEW_OUTPUT_DIRECTORY (must not exist)' >&2
    exit 1
fi
mkdir -p -- "$1/frames"
film_dir="$(cd -- "$1" && pwd)"
GODOT_BIN="$engine" "$repo_dir/tools/run_godot_study.sh" --stream=true --relief=true --prepare-only=true
cmake -S "$repo_dir" -B "$repo_dir/build" -DAPSIS_DRIFT_MIDI_SPIKE=ON
cmake --build "$repo_dir/build" --target apsis-drift-audio-pack-audition -j 4
timeout 20s "$engine" --headless --path "$repo_dir/experiments/godot-freedom" --script res://film_test.gd
timeout 1800s "$engine" --path "$repo_dir/experiments/godot-freedom" --audio-driver Dummy \
    --fixed-fps 24 --script res://film.gd -- --stream=true --render-size=3840x2160 \
    "--snapshot=$repo_dir/build-godot/snapshot-42-stream-true.json" \
    "--assets=$repo_dir/assets/visual" "--film-output=$film_dir/frames" \
    > "$film_dir/render.log" 2>&1
if rg -n 'ERROR:|SCRIPT ERROR:|Shader compilation failed' "$film_dir/render.log"; then
    echo 'Godot reported rendering errors; refusing to encode.' >&2
    exit 1
fi
"$repo_dir/build/apsis-drift-audio-pack-audition" "$repo_dir/assets" \
    "$film_dir/first-light.wav" "$film_dir/audio-source.json" --music-only
# Every visual pixel, including titles/fades, is rendered in Godot. FFmpeg only
# encodes, converts RGB to delivery YUV, and mixes the existing score render.
ffmpeg -hide_banner -nostdin -n -framerate 24 -i "$film_dir/frames/frame-%06d.png" \
    -i "$film_dir/first-light.wav" -t 40 -map 0:v:0 -map 1:a:0 \
    -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p \
    -vf 'scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuv444p,colorspace=ispace=bt709:iprimaries=bt709:itrc=srgb:irange=tv:all=bt709:range=tv:format=yuv420p' \
    -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv \
    -af 'afade=t=in:st=0:d=1.5,afade=t=out:st=37:d=3,loudnorm=I=-18:TP=-2:LRA=11' \
    -c:a aac -b:a 192k -ar 48000 -movflags +faststart \
    -metadata title='Apsis Drift — Freedom / In-engine preview 01' \
    -metadata comment='40-second offline Godot render; scripted cameras; not a gameplay/performance claim. First Light score; provenance: assets/provenance.json.' \
    "$film_dir/freedom-preview-4k.mp4"
ffmpeg -hide_banner -nostdin -n -i "$film_dir/freedom-preview-4k.mp4" \
    -vf scale=1920:1080 -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p \
    -c:a copy -movflags +faststart "$film_dir/freedom-preview-1080p.mp4"
python "$repo_dir/tools/verify_freedom_film.py" "$film_dir"
echo "Film, review report and regenerable source frames: $film_dir"
