#!/usr/bin/env python3
"""Run the real selected Chinese ASR model on its downloaded public samples."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--model-dir",
        default="models/asr/sherpa-onnx-streaming-zipformer-zh-14M-2023-02-23",
    )
    parser.add_argument("--output", help="optional JSON report path")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parents[1]
    model = (repo / args.model_dir).resolve()
    wavs = sorted((model / "test_wavs").glob("*.wav"))
    if not wavs:
        raise SystemExit(f"no test WAVs found under {model / 'test_wavs'}")

    report = []
    for wav in wavs:
        proc = subprocess.run(
            [sys.executable, str(repo / "tools" / "asr_file_test.py"), str(wav)],
            cwd=repo,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env={**os.environ, "PYTHONIOENCODING": "utf-8"},
        )
        item = {"wav": str(wav.relative_to(repo)), "returncode": proc.returncode}
        if proc.returncode == 0:
            try:
                item.update(json.loads(proc.stdout))
            except json.JSONDecodeError:
                item["stdout"] = proc.stdout.strip()
                item["error"] = "ASR output was not valid JSON"
        else:
            item["stderr"] = (proc.stderr or proc.stdout).strip()
            item["negative_test"] = wav.name == "8k.wav"
        report.append(item)

    result = {
        "model": model.name,
        "source": "official sherpa-onnx model release test_wavs",
        "cases": report,
        "real_model": True,
    }
    # Keep the console path ASCII-safe on the default Windows GBK terminal;
    # the optional report file remains UTF-8 with readable Chinese text.
    print(json.dumps(result, ensure_ascii=True, indent=2))
    if args.output:
        output = Path(args.output)
        if not output.is_absolute():
            output = repo / output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    valid = [x for x in report if x["returncode"] == 0]
    invalid = [x for x in report if x["returncode"] != 0 and not x.get("negative_test")]
    return 0 if valid and not invalid else 1


if __name__ == "__main__":
    raise SystemExit(main())
