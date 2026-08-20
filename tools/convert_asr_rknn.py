#!/usr/bin/env python3
"""Experimental ONNX -> RKNN conversion entry point for ASR neural parts.

The Zipformer encoder/joiner graph and its cache/input contract still require
operator and latency validation on RK3568. This helper intentionally converts
only the requested neural-network component; CPU feature extraction, endpoint
logic, decoder and tokenizer remain outside RKNN.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from rknn.api import RKNN


def convert(source: Path, output: Path, target_platform: str) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    rknn = RKNN(verbose=True)
    try:
        if rknn.config(target_platform=target_platform) != 0:
            raise RuntimeError("RKNN config failed")
        if rknn.load_onnx(model=str(source)) != 0:
            raise RuntimeError(f"RKNN load_onnx failed: {source}")
        if rknn.build(do_quantization=False) != 0:
            raise RuntimeError(f"RKNN build failed: {source}")
        if rknn.export_rknn(str(output)) != 0:
            raise RuntimeError(f"RKNN export failed: {output}")
    finally:
        rknn.release()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="one ONNX neural component")
    parser.add_argument("--output", required=True, type=Path, help="output .rknn path")
    parser.add_argument("--target-platform", default="rk3568")
    args = parser.parse_args()
    convert(args.source.resolve(), args.output.resolve(), args.target_platform)
    print(f"EXPERIMENTAL_RKNN_READY {args.output.resolve()}")


if __name__ == "__main__":
    main()

