<!--
  (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.

  RTI grants Licensee a license to use, modify, compile, and create derivative
  works of the Software. Licensee has the right to distribute object form only
  for use with RTI products. The Software is provided "as is", with no warranty
  of any type, including any warranty for fitness for any purpose. RTI is under no
  obligation to maintain or support the Software. RTI shall not be liable for any
  incidental or consequential damages arising out of the use or inability to use
  the software.
-->

# Typed sequences

Collections use RTI Connext Micro's typed sequence API with caller-provisioned
fixed storage. Sequence types represent the element type; capacity is set
during initialization and the runtime does not grow buffers.

| Core collection | Typed member / interface |
| --- | --- |
| Adapter registry | `PGW_Registry.adapters: PGW_AdapterSeq` |
| Sample representation registry | `PGW_Registry.representations: PGW_SampleRepresentationSeq` |
| Route catalog | `PGW_Service.routes: PGW_RouteSeq` |
| Borrowed samples | `PGW_Route.samples: PGW_SampleSeq` |
| Per-sample outcomes | `PGW_Route.results: PGW_WriteResultSeq` |
| Resource sizing input | `PGW_SizeSeq` |
| DDS compiled endpoint configuration | `PGW_DDSEndpointConfigSeq` |
| DDS runtime endpoint catalog | `PGW_DDSEndpointSeq` |
| Compiled example route/adapter-stream catalogs | `PGW_CompiledRouteSeq`, `PGW_CompiledAdapterStreamSeq` |
| CAN message/signal/category catalogs | `PGW_CANMessageDefinitionSeq`, `PGW_CANSignalDefinitionSeq`, `PGW_CANCategorySeq` |
| CAN endpoint/message/decode/write scratch | `PGW_CANCategoryStateSeq`, `PGW_CANMessageSeq`, `PGW_CANDecodedSeq`, `PGW_CANIndexSeq` |
| CAN opaque loans/socket filters | Per-category `PGW_CANLoanSeq`, `PGW_CANSocketFilterSeq` |
| CAN circular queue storage | `PGW_CANSampleSeq` and `PGW_CANFrameSeq`; fixed native length is slot capacity, FIFO occupancy remains separate `head`/`count` state |
| Diagnostics event slots | `PGW_EventSeq`; ring head/occupancy remain separate from fixed sequence capacity |

Core route, session, representation, adapter, endpoint, and result collections
are typed sequences. Their element buffers remain caller-owned and borrowed by
the service.

## Adding a collection

After the element type is complete, declare its sequence in the owning header
using the same pattern as generated support:

```c
typedef const MyDescriptor *MyDescriptorRef;
#define REDA_SEQUENCE_USER_API
#define T MyDescriptorRef
#define TSeq MyDescriptorSeq
#include <reda/reda_sequence_decl.h>
typedef struct MyDescriptorSeq MyDescriptorSeq;
```

Instantiate it once in that component's C source:

```c
#include "my_descriptor.h"
#include "osapi/osapi_log.h"
#include "osapi/osapi_log_impl.h"
#define REDA_SEQUENCE_USER_API
#define T MyDescriptorRef
#define TSeq MyDescriptorSeq
#include "reda/reda_sequence_defn.h"
```

The sequence templates provide the typed methods through the linked Micro
infrastructure. Select the required `TSeq_*` methods and API subset, keep the
declaration and definition selections consistent, and clean up template
configuration macros after including the templates.

```c
MyDescriptorRef slots[8];
MyDescriptorSeq descriptors;
if (!MyDescriptorSeq_initialize(&descriptors) ||
    !MyDescriptorSeq_loan_contiguous(&descriptors, slots, 0, 8)) {
    /* Return the component's initialization error. */
}
```

`get_reference` returns a pointer to an element; for reference sequences this
is a pointer to a pointer slot. The SDK getter accepts a const sequence; keep
read-only element references const in the consuming code.
`set_length` cannot exceed borrowed capacity.
The element typedef may itself be const for an immutable borrowed catalog.
Only initialization provisions backing storage. No sequence owns or destroys
the pointed-to adapter, sample or route objects. Native lengths/capacities are
`RTI_INT32`; initialization checks external `size_t` bounds before conversion.
`has_ownership` distinguishes native-owned storage from a borrowed buffer.

## Loans and ownership

- `loan_contiguous` attaches caller-owned fixed storage; it does not allocate or
  transfer ownership of the pointed-to objects.
- Finalize the sequence view after its consumer has stopped using it.
- A sample-reference sequence loan is not a protocol loan return. Every
  successful reader operation still requires exactly one adapter
  `return_loan`.
- Sequence length must remain within its preallocated capacity. Micro sequence
  lengths use `RTI_INT32`; validate external `size_t` values before conversion.
