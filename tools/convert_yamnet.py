#!/usr/bin/env python3
"""Convert the official YAMNet ONNX artifact to an RK3568 FP16 model."""

import argparse

from rknn.api import RKNN


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("onnx")
    parser.add_argument("output")
    args = parser.parse_args()

    rknn = RKNN(verbose=True)
    try:
        print("--> config rk3568")
        if rknn.config(target_platform="rk3568", float_dtype="float16") != 0:
            raise SystemExit("RKNN config failed")
        print("--> load ONNX")
        if rknn.load_onnx(model=args.onnx) != 0:
            raise SystemExit("RKNN load_onnx failed")
        print("--> build FP16")
        if rknn.build(do_quantization=False) != 0:
            raise SystemExit("RKNN build failed")
        print("--> export RKNN")
        if rknn.export_rknn(args.output) != 0:
            raise SystemExit("RKNN export failed")
    finally:
        rknn.release()


if __name__ == "__main__":
    main()

