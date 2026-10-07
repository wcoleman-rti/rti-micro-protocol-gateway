#
# (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software. Licensee has the right to distribute object form only
# for use with RTI products. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is under no
# obligation to maintain or support the Software. RTI shall not be liable for any
# incidental or consequential damages arising out of the use or inability to use
# the software.
#

"""Run the actual DDS workload and validate its machine-readable accounting."""
import json
import subprocess
import sys

process = subprocess.run(sys.argv[1:], capture_output=True, text=True)
if process.returncode == 77:
    sys.stderr.write(process.stderr)
    sys.exit(77)
if process.returncode:
    sys.stderr.write(process.stderr)
    sys.exit(process.returncode if process.returncode > 0 else 1)
record = json.loads(process.stdout)
batch_count, timing = map(int, sys.argv[2:4])
preserve = int(sys.argv[4]) if len(sys.argv) > 4 else 0
assert record["format_version"] == 1
assert record["batches"] == batch_count and record["samples"] == batch_count * 5
assert record["timing"] == bool(timing)
assert record["offered_can_frames"] == record["received_can_frames"] == batch_count
assert record["decoded_states"] == record["local_accepted_states"] == batch_count * 4
assert record["local_dds_accepted_commands"] == batch_count
assert record["backpressure_commands"] == batch_count // 8
assert record["local_can_accepted_commands"] == record["peer_observed_can_frames"] == batch_count - batch_count // 8
assert batch_count <= record["peer_observed_state_samples"] <= batch_count * 4
assert record["peer_observed_probe_samples"] == batch_count
assert record["metadata_preservation"]["probe_source_timestamp"] == bool(preserve)
assert record["invalid_commands"] == record["rx_dropped_signals"] == record["outstanding_loans"] == 0
assert record["gateway_arena_runtime_requests"] == 0
assert record["gateway_runtime_allocations"] is None
assert record["arena_initialization_bytes"] == record["arena_reserved_bytes"]
assert "malloc" in record["arena_backing"]
assert record["observed_libc_initialization_allocations"] >= 1
assert record["observed_libc_runtime_allocations"] >= 0
assert record["osapi_runtime_allocations"] >= 0
reader = record["peer_reader"]
writer = record["dds_effective_resources"]
assert reader["history_depth"] == writer["history_depth"] == 1
assert reader["instances"] == writer["instances"]
assert reader["samples"] >= reader["instances"] * reader["per_instance"]
assert reader["unobserved_state_samples_at_stop"] == batch_count * 4 - record["peer_observed_state_samples"]
assert writer["writer_blocking_seconds"] == writer["writer_blocking_nanoseconds"] == 0
for name, summary in (("local", "dispatch_latency_ns"), ("roundtrip", "roundtrip_latency_ns")):
    buckets = record["histogram_counts"][name]
    assert len(buckets) == 64 and all(value >= 0 for value in buckets)
    assert sum(buckets) == record[summary]["count"] == (batch_count if timing else 0)
if not record["configuration"]["diagnostics"]:
    assert record["management_exports"] == record["peer_observed_management_samples"] == 0
