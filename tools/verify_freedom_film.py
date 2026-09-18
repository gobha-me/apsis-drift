#!/usr/bin/env python3
"""Validate sequence boundaries and delivered film streams, not visual quality."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


def verify(root):
    report = json.loads((root / "frames/render.json").read_text())
    assert report["frames"] == 960 and report["fps"] == 24 and not report["review_only"]
    assert report["pixels"] == [3840, 2160]
    expected = [root / f"frames/frame-{i:06d}.png" for i in range(960)]
    assert sorted((root / "frames").glob("frame-*.png")) == expected
    unique = set()
    for path in expected:
        with path.open("rb") as stream:
            header = stream.read(24)
        assert header[:8] == b"\x89PNG\r\n\x1a\n"
        assert struct.unpack(">II", header[16:24]) == (3840, 2160)
        unique.add(hashlib.sha256(path.read_bytes()).hexdigest())
    # Black endpoints repeat intentionally; most frames must actually move.
    assert len(unique) >= 940, f"Only {len(unique)} unique frames"
    cursor = 0
    for shot in report["shots"]:
        assert shot["first_frame"] == cursor and shot["frames"] == shot["seconds"] * 24
        assert shot["stream"]["resident_tiles"] <= 384
        cursor += shot["frames"]
    assert cursor == 960
    result = {"schema_version": 1, "frames": 960, "unique_source_frames": len(unique),
              "seconds": 40, "fps": 24, "source_pixels": [3840, 2160], "deliveries": []}
    for name, dimensions in [("freedom-preview-4k.mp4", (3840, 2160)),
                             ("freedom-preview-1080p.mp4", (1920, 1080))]:
        path = root / name
        probe = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-count_frames", "-show_streams", "-show_format",
            "-of", "json", str(path)]))
        video = next(s for s in probe["streams"] if s["codec_type"] == "video")
        audio = next(s for s in probe["streams"] if s["codec_type"] == "audio")
        assert (video["width"], video["height"]) == dimensions
        assert video["codec_name"] == "h264" and video["pix_fmt"] == "yuv420p"
        assert video["color_space"] == "bt709" and video["color_primaries"] == "bt709"
        assert video["color_transfer"] == "bt709" and video["color_range"] == "tv"
        assert video["avg_frame_rate"] == "24/1" and int(video["nb_read_frames"]) == 960
        assert abs(float(probe["format"]["duration"]) - 40) < 0.1
        assert audio["codec_name"] == "aac" and audio["channels"] == 2 and audio["sample_rate"] == "48000"
        subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-i", str(path), "-f", "null", "-"], check=True)
        result["deliveries"].append({"file": name, "bytes": path.stat().st_size,
                                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                    "pixels": list(dimensions), "full_decode": "passed"})
    (root / "verification.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    verify(Path(sys.argv[1]))
