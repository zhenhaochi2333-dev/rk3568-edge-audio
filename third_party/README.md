# Third-party integration boundary

The formal Linux ASR engine uses the C API from [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx), linked by CMake through `SHERPA_ONNX_ROOT`. The repository does not copy the full runtime or its model archive into Git.

Expected Linux layout after building/installing sherpa-onnx:

```text
<prefix>/include/sherpa-onnx/c-api/c-api.h
<prefix>/lib/libsherpa-onnx-c-api.so
<prefix>/lib/libonnxruntime.so
```

The Windows validation service uses the official `sherpa-onnx` Python wheel only to prove the same model and protocol on a PC when an Ubuntu VM is unavailable.

