# Benchmarking

```sh
python3 benchmarks/runner/run.py build/benchmarks/pgw_core_benchmark --repeat 3
python3 benchmarks/runner/run.py build/benchmarks/pgw_core_benchmark --timing 0
python3 benchmarks/runner/run.py build/benchmarks/pgw_core_benchmark --compare results/core-TIMESTAMP.json
python3 benchmarks/runner/run.py build/benchmarks/pgw_core_benchmark --perf
```

Runs persist JSON with source/executable fingerprints, build options, host,
workload and instrumentation settings, declared input fingerprints and verified
SDK/compiler/JRE/generator provenance. Source fingerprinting excludes generated
build trees, the local tool environment and results. No warm-up is hidden:
allocation monitoring and elapsed measurement include first use after READY.

The initial workload forwards four opaque keyed values per scheduler step.
It reports accepted samples/s separately from steps, initialization duration,
fixed fixture bytes, OSAPI/libc allocation calls, sampled process RSS and CPU time.
The driver samples `/proc/<owned-pid>/status` every 10 ms; short runs may have no
samples, explicitly reported as unavailable. `getrusage_maxrss_kib` is retained
separately and may include an inherited launcher high-water mark.
Step timing measures scheduler entry-to-return, **not network or wire latency**.
Percentiles are upper bounds from 64 power-of-two buckets, not exact quantiles.
Timing-disabled runs quantify measurement overhead but are noncomparable to
timing-enabled runs in regression comparisons.
Adapter workloads may accept `--metadata 0|1`; consult their documented capture
or preservation policy. The core-only workload has no metadata mode. Metadata
mode is recorded and must match for regression comparisons.
The external driver drains output while sampling RSS and enforces a per-process
`--timeout` (300 seconds by default). Each workload/profiler owns a separate
process session; timeout/interrupt cleanup targets only that owned group.

Only compatible workload/build/host/instrumentation reports are compared.
Latency/throughput deltas are observations; there is no invented timing gate.
Finite work, exact accepted counts, loan balance and zero monitored runtime
allocation are correctness gates now. RSS includes the process and is not an
exact component memory estimate. SDK/kernel/custom allocation coverage and
integrated adapter timing need their own explicit reports.

Optional `--perf` captures an extra process-limited `perf stat` run or records
the exact tool/permission failure as unavailable. It does not hide that failure
behind a fabricated profile or include the extra run in baseline measurements.
External `perf` profiling may also be applied to a benchmark process when permitted.
Do not change global perf settings or configure shared interfaces automatically.
Record unavailable tooling/permissions rather than fabricate measurements.
