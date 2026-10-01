#!/usr/bin/env python3
"""Assemble actual Godot movies and Godot-rendered editorial cards for owner review.

Large media stays outside Git. The plan supplies explicit movie excerpts, card
timing and music. FFmpeg encodes/composites; it does not synthesize gameplay.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


def run(command, log):
    with log.open("ab") as stream:
        stream.write((repr(command) + "\n").encode())
        subprocess.run(command, stdout=stream, stderr=stream, check=True)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def finite_number(value, minimum, maximum):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("Expected a finite number")
    if not math.isfinite(value) or not minimum <= value <= maximum:
        raise ValueError("Number outside reel bounds")
    return value


def probe(path):
    return json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_format", "-show_streams",
        "-of", "json", str(path),
    ]))


def write_subtitles(plan, output, narrative):
    cards = {card["id"]: card for card in narrative["cards"]}
    entries = []
    offset = 0
    for clip in plan["clips"]:
        for overlay in clip.get("overlays", []):
            card = cards.get(overlay["card"], {})
            if card.get("kind", "dialogue") == "dialogue" and card.get("text"):
                entries.append((offset + overlay["start"], offset + overlay["end"],
                                card.get("speaker", "") + ": " + card["text"]))
        offset += clip["duration"]

    def timestamp(seconds):
        milliseconds = round(seconds * 1000)
        hours, remainder = divmod(milliseconds, 3600000)
        minutes, remainder = divmod(remainder, 60000)
        seconds, milliseconds = divmod(remainder, 1000)
        return f"{hours:02d}:{minutes:02d}:{seconds:02d},{milliseconds:03d}"

    output.write_text("".join(
        f"{index}\n{timestamp(start)} --> {timestamp(end)}\n{text}\n\n"
        for index, (start, end, text) in enumerate(sorted(entries), 1)))


def stream_duration(stream):
    if stream.get("nb_frames") and stream.get("avg_frame_rate") not in (None, "0/0"):
        numerator, denominator = map(int, stream["avg_frame_rate"].split("/"))
        if numerator > 0 and denominator > 0:
            duration = int(stream["nb_frames"]) * denominator / numerator
            return finite_number(duration, 0.01, 36000)
    return finite_number(float(stream["duration"]), 0.01, 36000)


def validate(plan, card_dir, proof=False):
    if plan.get("schema_version") != 1 or not isinstance(plan.get("clips"), list):
        raise ValueError("Expected schema version 1 and clips")
    total = 0
    inputs = []
    card_receipt = json.loads((card_dir / "cards-receipt.json").read_text())
    if card_receipt.get("schema_version") != 1 or not isinstance(card_receipt.get("narrative"), dict):
        raise ValueError("Expected frozen Godot card provenance")
    for clip in plan["clips"]:
        source = Path(clip["source"]).resolve(strict=True)
        data = probe(source)
        video = [s for s in data["streams"] if s["codec_type"] == "video"]
        if len(video) != 1:
            raise ValueError("Each source must have one video stream")
        start = finite_number(clip.get("start", 0), 0, 36000)
        duration = finite_number(clip["duration"], 0.25, 600)
        if any(abs(round(number * 24) - number * 24) > 1e-6 for number in (start, duration)):
            raise ValueError("Excerpt timings must align with the 24fps delivery grid")
        if start + duration > stream_duration(video[0]) + 0.001:
            raise ValueError(f"Excerpt exceeds source: {source.name}")
        if not isinstance(clip.get("truth"), str) or not clip["truth"]:
            raise ValueError("Every clip requires an explicit truth label")
        for overlay in clip.get("overlays", []):
            card_id = overlay["card"]
            if not isinstance(card_id, str) or Path(card_id).name != card_id:
                raise ValueError("Invalid card name")
            card = (card_dir / (card_id + ".png")).resolve(strict=True)
            if card.parent != card_dir.resolve():
                raise ValueError("Card escapes card directory")
            if digest(card) != card_receipt["images_sha256"].get(card.name):
                raise ValueError("Card changed after its Godot provenance receipt")
            begin = finite_number(overlay["start"], 0, duration)
            end = finite_number(overlay["end"], 0, duration)
            if end <= begin:
                raise ValueError("Empty/reversed card interval")
        source_hash = digest(source)
        if "source_sha256" in clip and clip["source_sha256"] != source_hash:
            raise ValueError(f"Source movie changed: {source.name}")
        inputs.append({"source": str(source), "sha256": source_hash,
                       "probe": data, "start": start, "duration": duration,
                       "truth": clip["truth"]})
        total += duration
    if proof and not 1 <= total <= 60:
        raise ValueError("A recording proof must be between one and sixty seconds")
    if not proof and not 300 <= total <= 600:
        raise ValueError("The deliverable must be between five and ten minutes")
    music = Path(plan["music"]).resolve(strict=True)
    if "music_sha256" in plan and plan["music_sha256"] != digest(music):
        raise ValueError("Music source changed")
    return total, inputs, music, card_receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plan", type=Path)
    parser.add_argument("cards", type=Path)
    parser.add_argument("output", type=Path, help="New, nonexistent directory")
    parser.add_argument("--proof", action="store_true", help="Allow a short recording proof")
    args = parser.parse_args()
    plan = json.loads(args.plan.read_text())
    total, inputs, music, card_receipt = validate(plan, args.cards, args.proof)
    args.output.mkdir(parents=True, exist_ok=False)
    log = args.output / "encode.log"
    segments = []
    for index, (clip, source) in enumerate(zip(plan["clips"], inputs)):
        segment = args.output / f"segment-{index:02d}.mp4"
        command = ["ffmpeg", "-hide_banner", "-nostdin", "-n", "-ss", str(source["start"]),
                   "-t", str(source["duration"]), "-i", source["source"]]
        overlays = clip.get("overlays", [])
        for overlay in overlays:
            command += ["-loop", "1", "-framerate", "24", "-i",
                        str(args.cards / (overlay["card"] + ".png"))]
        filters = ["[0:v]scale=1920:1080:force_original_aspect_ratio=decrease,"
                   "pad=1920:1080:(ow-iw)/2:(oh-ih)/2:color=black,"
                   "setsar=1,fps=24,format=yuv420p[base]"]
        last = "base"
        for number, overlay in enumerate(overlays, 1):
            output = f"card{number}"
            filters.append(f"[{last}][{number}:v]overlay=0:0:"
                           f"enable='gte(t,{overlay['start']})*lt(t,{overlay['end']})':"
                           f"shortest=1:format=auto[{output}]")
            last = output
        command += ["-filter_complex", ";".join(filters), "-map", f"[{last}]",
                    "-an", "-t", str(source["duration"]), "-c:v", "libx264",
                    "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p",
                    "-color_primaries", "bt709", "-color_trc", "bt709",
                    "-colorspace", "bt709", "-movflags", "+faststart", str(segment)]
        run(command, log)
        segments.append(segment)
    # Segment names are controlled here; no untrusted concat syntax is admitted.
    concat = args.output / "segments.txt"
    concat.write_text("".join(f"file '{p.name}'\n" for p in segments))
    movie = args.output / "apsis-drift-beyond-the-paperwork.mp4"
    # Crossfade source repeats rather than introducing a hard sixty-second seam.
    music_duration = float(probe(music)["format"]["duration"])
    if not math.isfinite(music_duration) or music_duration < 10:
        raise ValueError("Music source must contain at least ten seconds")
    repeats = max(1, math.ceil((total - 4) / (music_duration - 4)))
    if repeats == 1:
        music_filter = "[1:a]anull[music]"
    else:
        split_labels = "".join(f"[repeat{index}]" for index in range(repeats))
        chain = [f"[1:a]asplit={repeats}{split_labels}"]
        previous = "repeat0"
        for index in range(1, repeats):
            output = "music" if index == repeats - 1 else f"mix{index}"
            chain.append(f"[{previous}][repeat{index}]acrossfade=d=4:c1=tri:c2=tri[{output}]")
            previous = output
        music_filter = ";".join(chain)
    music_filter += f";[music]atrim=duration={total},afade=t=in:st=0:d={min(2,total)},afade=t=out:st={max(0,total-5)}:d={min(5,total)},loudnorm=I=-20:TP=-2:LRA=11[score]"
    run(["ffmpeg", "-hide_banner", "-nostdin", "-n", "-f", "concat", "-safe", "1",
         "-i", str(concat), "-i", str(music), "-filter_complex", music_filter,
         "-map", "0:v:0", "-map", "[score]", "-t", str(total), "-c:v", "copy",
         "-c:a", "aac", "-b:a", "192k", "-ar", "48000", "-movflags", "+faststart",
         "-metadata", "title=Apsis Drift - Beyond the paperwork",
         "-metadata", "comment=Private owner review; Godot-rendered footage with editorial dialogue. Chapters include gameplay and isolated studies; no continuous boarding/departure claim.",
         str(movie)], log)
    data = probe(movie)
    video = [s for s in data["streams"] if s["codec_type"] == "video"]
    audio = [s for s in data["streams"] if s["codec_type"] == "audio"]
    if len(video) != 1 or len(audio) != 1 or (video[0]["width"], video[0]["height"]) != (1920, 1080):
        raise ValueError("Unexpected delivery streams/dimensions")
    if video[0]["r_frame_rate"] != "24/1" or int(video[0]["nb_frames"]) != round(total * 24):
        raise ValueError("Unexpected video rate/frame count")
    if abs(stream_duration(video[0]) - total) > 0.04 or abs(float(audio[0]["duration"]) - total) > 0.1:
        raise ValueError("Video/audio stream clocks diverge from the edit")
    if abs(float(data["format"]["duration"]) - total) > 0.1:
        raise ValueError("Unexpected delivery rate/duration")
    run(["ffmpeg", "-hide_banner", "-nostdin", "-v", "error", "-i", str(movie),
         "-map", "0:v:0", "-map", "0:a:0", "-f", "null", "-"], args.output / "decode.log")
    write_subtitles(plan, movie.with_suffix(".srt"), card_receipt["narrative"])
    receipt = {"schema_version": 1, "scope": "Private Godot demo reel with editorial narrative",
               "proof_only": args.proof, "movie": movie.name, "sha256": digest(movie), "probe": data,
               "full_decode": "passed", "plan_sha256": digest(args.plan),
               "sources": inputs, "music": {"file": str(music), "sha256": digest(music),
                                            "source_duration": music_duration, "repeats": repeats,
                                            "repeat_crossfade_seconds": 4, "filter": music_filter},
               "card_render_receipt": card_receipt,
               "cards": {p.name: digest(p) for p in sorted(args.cards.glob("*.png"))},
               "plan": plan}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(movie)


if __name__ == "__main__":
    main()
