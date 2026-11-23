# Async validation

The async tests cover response parsing, worker and loop thread affinity, cancellation, deadlines, HTTP/HTTPS, and an HTTP → job → HTTP chain. See [async usage](../FeiLib/docs/async-system.md).

```bash
cmake -S Tests/AsyncTest -B build/async-validation
cmake --build build/async-validation --parallel 2
ctest --test-dir build/async-validation --output-on-failure
```
