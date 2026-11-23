# Image validation

Windows MinGW and a full Linux Server build passed six test groups. Linux C++ ASan/UBSan also passed with SQLite compiled normally. Instrumenting the bundled SQLite C source caused a reproducible crash during random-number initialization; its cause remains unknown.

The tests use loopback HTTP/HTTPS and a temporary SQLite database. For example:

```bash
cmake -S Tests/ImageTest -B build/image-validation
cmake --build build/image-validation --parallel 2
ctest --test-dir build/image-validation --output-on-failure
```
