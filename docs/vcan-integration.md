# Isolated SocketCAN/vcan integration

The Linux SocketCAN adapter is implemented and compiled, but the maintained
automated suite does not claim successful vcan or physical-bus traffic. Current
tests cover bounded mapping over memory CAN and SocketCAN validation/error paths;
those are not substitutes for a live interface.

To add live integration in an authorized environment:

1. Provision a disposable, owned network namespace and a dedicated vcan
   interface using that environment's approved system tooling. Do not modify
   or reuse a shared `vcan0` or physical interface.
2. Run a CAN peer, gateway, and DDS companion as owned processes, with an
   unused configured DDS domain. Retain process IDs and clean up only the
   namespace/interface created for the test.
3. Inject known Classic, extended, and FD baseline frames; publish matching
   DDS commands; assert exact coalesced output bytes, unrelated-bit
   preservation, timestamp capture, echo suppression, backpressure and
   observable kernel overflow/error statuses.
4. Report unavailable capabilities or occupied resources as blocked/skipped,
   never as a passing memory simulation. Keep physical delivery, hardware
   timestamps and unobservable bus loss outside the claims.

The runnable example requires an explicit interface name for SocketCAN and
never provisions interfaces. `--memory` uses the bounded in-memory transport
with real DDS, but proves no kernel CAN behavior.
