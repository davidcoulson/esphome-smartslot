# Dev notes

- Host tests: `make -C tests` (gcc, Unity, pthreads). Compile: `cd example && esphome compile smartslot-bench.yaml` (ESPHome 2026.10.0, venv `~/.venvs/esphome`).
- The vendored NUT files are byte-identical (`sha256sum -c components/smartslot/nut/SHA256SUMS`).
- Open risks: UART edge cases are only tested against the host fake serial, not a real UPS. `nutdrv` request slots: a caller that times out leaves the slot to the driver task. FSD latch is process-wide and is not cleared except by restart.
- Not done: Task 9 (bench test), pin check on the bench board, any release tag.
