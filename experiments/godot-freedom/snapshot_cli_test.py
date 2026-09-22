#!/usr/bin/env python3
"""Exercise the built snapshot CLI, with fresh isolated outputs on every run."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exporter", type=Path)
    args = parser.parse_args()
    exporter = args.exporter.resolve()
    if not exporter.is_file():
        parser.error(f"missing exporter: {exporter}")
    cases = 0
    with tempfile.TemporaryDirectory(prefix="physical-snapshot-cli-") as directory:
        work = Path(directory)

        def invoke(arguments):
            nonlocal cases
            cases += 1
            return subprocess.run([str(exporter), *map(str, arguments)],
                                  capture_output=True, text=True, timeout=20,
                                  cwd=work, check=False)

        def require(condition, message, result):
            if not condition:
                raise AssertionError(f"{message}\nexit={result.returncode}\n"
                                     f"stdout={result.stdout}\nstderr={result.stderr}")

        def valid(name, options, schema, seed):
            path = work / f"{name}.json"
            result = invoke([path, *options])
            require(result.returncode == 0 and path.is_file(), name, result)
            data = json.loads(path.read_text())
            require(data["schema_version"] == schema and data["samples"] == 3,
                    f"{name}: wrong schema/dimensions", result)
            require(len(data["vertices"]) == 9 and len(data["replay"]) == 301,
                    f"{name}: incomplete export", result)
            if schema == 2:
                require(data["world_context"]["family"] == "physical_circular"
                        and data["world_context"]["origin_universe_seed"] == seed
                        and data["world_context"]["planet_seed"] == data["planet"]["planet_seed"],
                        f"{name}: wrong physical owner", result)
            else:
                require("world_context" not in data and data["planet"]["planet_seed"] == seed,
                        f"{name}: legacy identity changed", result)
            return path, data

        physical = ["--physical-origin=42", "--samples=3", "--span-metres=1000"]
        physical_path, data = valid("physical", physical + ["--relief-version=1",
                                    "--latitude=-0.7", "--longitude=-2.4"], 2, "42")
        assert data["frame"]["latitude_radians"] == -0.7
        assert data["frame"]["longitude_radians"] == -2.4
        valid("maximum-seed", ["--physical-origin=18446744073709551615", "--samples=3"],
              2, "18446744073709551615")
        valid("zero-seed", ["--physical-origin=0", "--samples=3"], 2, "0")
        legacy_path, _ = valid("legacy", ["42", "3", "1000", "0"], 1, "42")
        valid("legacy-relief", ["42", "3", "1000", "1"], 1, "42")

        # Each invalid invocation receives a NEW absent path. Refusal therefore
        # cannot accidentally pass merely because an earlier output exists.
        invalid = [
            ["--physical-origin="], ["--physical-origin"],
            ["--physical-origin=-1"], ["--physical-origin=18446744073709551616"],
            ["--physical-origin=1.5"], ["--physical-origin=nan"],
            ["--samples=3", "--physical-origin=42"],
            [*physical, "--unknown=1"], [*physical, "--samples=4"],
            [*physical, "--physical-origin=42"], [*physical, "--latitude"],
            [*physical, "42"], ["42", "3", "1000", "--physical-origin=42"],
            ["42", "3", "1000", "0", "unexpected"],
        ]
        for value in ("1", "514", "-1", "3.5", "nan", "inf", "4294967296"):
            invalid.append(["--physical-origin=42", f"--samples={value}"])
        for value in ("63", "262145", "nan", "inf", "-inf"):
            invalid.append(["--physical-origin=42", "--samples=3", f"--span-metres={value}"])
        for key, values in (("latitude", ("1.6", "-1.6", "nan", "inf")),
                            ("longitude", ("3.2", "-3.2", "nan", "-inf")),
                            ("relief-version", ("2", "-1", "0.5", "inf"))):
            for value in values:
                invalid.append([*physical, f"--{key}={value}"])
        invalid.append([*physical, "--relief-version=1", "--latitude=0",
                        "--longitude=0", "--extra=1"])
        for index, options in enumerate(invalid):
            path = work / f"invalid-{index}.json"
            assert not path.exists()
            result = invoke([path, *options])
            require(result.returncode in (1, 2) and bool(result.stderr.strip()),
                    f"invalid arguments accepted or crashed: {options}", result)
            require(not path.exists(), f"invalid request created output: {options}", result)

        result = invoke([])
        require(result.returncode == 2 and "usage:" in result.stderr,
                "missing output did not print usage", result)
        # Both mode parsers must preserve preexisting bytes, including arbitrary
        # non-JSON files: output protection must not depend on a valid snapshot.
        sentinel = work / "existing-not-json.json"
        sentinel.write_bytes(b"do not overwrite\x00\xff\n")
        for path, options in ((physical_path, physical),
                              (legacy_path, ["42", "3", "1000", "0"]),
                              (sentinel, physical)):
            before = path.read_bytes()
            result = invoke([path, *options])
            require(result.returncode == 1 and "already exists" in result.stderr,
                    "preexisting output not explicitly refused", result)
            require(path.read_bytes() == before, "preexisting bytes changed", result)
    print(f"Physical snapshot CLI: {cases} cases, 0 failures")


if __name__ == "__main__":
    main()
