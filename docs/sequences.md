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

# Typed Micro sequences

Runtime registries, routes and write outcomes use one typed sequence member, not
parallel pointer/count/capacity members. Each sequence is a distinct C type with
its own matching installed Micro sequence implementation.

| Core collection | Typed member / interface |
| --- | --- |
| Adapter registry | `PGW_Registry.adapters: PGW_AdapterSeq` |
| Binding registry | `PGW_Registry.bindings: PGW_RepresentationSeq` |
| Route catalog | `PGW_Service.routes: PGW_RouteSeq` |
| Borrowed samples | `PGW_Route.samples: PGW_SampleSeq` |
| Per-sample outcomes | `PGW_Route.results: PGW_WriteResultSeq` |
| Resource sizing input | `PGW_SizeSeq` |
| DDS compiled endpoint configuration | `PGW_DDSEndpointConfigSeq` |
| DDS runtime endpoint catalog | `PGW_DDSEndpointSeq` |
| Compiled example route/native-stream catalogs | `PGW_CompiledRouteSeq`, `PGW_CompiledNativeStreamSeq` |
| CAN message/signal/category catalogs | `PGW_CANMessageDefinitionSeq`, `PGW_CANSignalDefinitionSeq`, `PGW_CANCategorySeq` |
| CAN endpoint/message/decode/write scratch | `PGW_CANCategoryStateSeq`, `PGW_CANMessageSeq`, `PGW_CANDecodedSeq`, `PGW_CANIndexSeq` |
| CAN opaque loans/socket filters | Per-category `PGW_CANLoanSeq`, `PGW_CANSocketFilterSeq` |
| CAN circular queue storage | `PGW_CANSampleSeq` and `PGW_CANFrameSeq`; fixed native length is slot capacity, FIFO occupancy remains separate `head`/`count` state |
| Diagnostics event slots | `PGW_EventSeq`; ring head/occupancy remain separate from fixed sequence capacity |

Declarations and definitions directly include RTI's installed templates, without
a PGW sequence implementation or convenience facade. The implementation follows
the generated-C pattern, checked against both the
actual signal type support and a separate bounded IDL probe:

```idl
module PGW_SequenceProbe {
    struct Item { long value; };
    typedef sequence<Item, 4> Items;
    struct Container { Items items; };
};
```

Micro Codegen 4.7.0 (bundled with Micro 4.3.0) generates `T`/`TSeq` configuration
followed by the installed `reda_sequence_decl.h` / `reda_sequence_defn.h`
templates. The generated probe compiled against the installed SDK. Generated
sample initializers may call `set_maximum`; gateway runtime collections instead
borrow fixed storage before READY and never expose growth operations.

Opaque sample references, adapter callback tables and native route objects
cannot use an IDL serialization model. Their support is therefore declared
manually using the same template mechanism, rather than adding generated DDS
payload types or reproducing SDK layouts.

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

These are SDK-generated typed methods, implemented by the installed templates
and linked Micro infrastructure. There is no gateway replacement vector, copied
SDK layout or runtime-sized reinterpretation of another sequence type.
The core selects only its required native methods through `TSeq_*` configuration
flags and `REDA_SEQUENCE_API_UNTYPED`; other components may select the documented
user API subset they require. Keep declaration/definition selections consistent
and clean up template configuration macros as the generated code does.

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
Initialization state belongs to the enclosing registry/service lifecycle, not
duplicated sequence metadata.

## Ownership and exclusions

- Pointer-array unloan is not adapter `return_loan`; a protocol loan still has
  exactly one return through its reader.
- Registry, route, diagnostics and adapter configuration constructors accept
  initialized typed sequences and adopt their native fixed-buffer views.
  Generated DBC tables remain raw array/count inputs only at the SDK-free codec
  boundary; the CAN adapter immediately wraps them in typed views.
- Compiled DDS endpoint and example graph tables are emitted as native sequence
  descriptors using RTI's standard static loan initializer. Runtime iteration
  uses `get_length`/`get_reference`; there is no parallel endpoint or catalog
  count member.
- CAN payload/DLC fields, SDK-owned native DDS loan sequences and generated
  codec C ABI tables remain protocol/generator boundaries.
- Ring queue cursors/occupancy are not vector length: replacing FIFO indexing
  with linear sequence ordering would require moving elements or weaken
  bounded work. Typed sequence storage supplies the fixed slots; the ring
  maintains its own head/occupancy and never overloads sequence length with
  live FIFO count.
- Arenas are heterogeneous aligned byte storage, not collections of one
  element type. Fixed counter/histogram arrays are indexed scalar schemas,
  not variable-length containers.

The infrastructure subset and ABI still require a separate audit before use
with any certification-qualified SDK or target.
