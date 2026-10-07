/*
 * (c) 2026 Copyright, Real-Time Innovations, Inc. All rights reserved.
 *
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the Software. Licensee has the right to distribute object form only
 * for use with RTI products. The Software is provided "as is", with no warranty
 * of any type, including any warranty for fitness for any purpose. RTI is under no
 * obligation to maintain or support the Software. RTI shall not be liable for any
 * incidental or consequential damages arising out of the use or inability to use
 * the software.
 */

#include <pgw/signal_dds.h>
#include <pgw/can.h>
#include <pgw/can.h>
#include "signals.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static PGW_Status unavailable_view(const PGW_Sample *sample, PGW_SampleView *out)
{
    (void)sample;
    (void)out;
    return PGW_UNSUPPORTED;
}

int main(void)
{
    PGW_DDS_Signal wire;
    PGW_Signal native;
    assert(PGW_signal_dds_type_binding_powertrain.write_view == PGW_signal_write_view);
    assert(PGW_signal_dds_type_binding_powertrain.bind_view == PGW_signal_bind_view);
    PGW_TypeInfo alternate_schema = {"can.signal.view", 7, "alternate"};
    PGW_SampleViewDescriptor can_view_contract = {
        PGW_SAMPLE_VIEW_CANONICAL, sizeof(PGW_Signal),
        &PGW_CAN_SIGNAL_VALUE_IDENTITY, &PGW_CAN_METADATA_IDENTITY
    };
    const PGW_SampleAccessI can_view_access = {
        PGW_ABI_VERSION, sizeof(PGW_SampleAccessI), NULL, NULL, unavailable_view
    };
    PGW_SampleRepresentation alternate_source = {
        &alternate_schema, "can.signal", sizeof(PGW_Signal),
        _Alignof(PGW_Signal), &can_view_access, &can_view_contract
    };
    assert(PGW_signal_bind_view(NULL, &alternate_source) == PGW_OK);
    PGW_SampleViewDescriptor unknown_contract = can_view_contract;
    unknown_contract.type_identity = &alternate_schema;
    alternate_source.view_contract = &unknown_contract;
    assert(PGW_signal_bind_view(NULL, &alternate_source) == PGW_UNSUPPORTED);
    assert(PGW_DDS_Signal_initialize(&wire));
    wire.id = 1003;
    wire.value._d = VALUE_BOOLEAN;
    wire.value._u.boolean_value = DDS_BOOLEAN_TRUE;
    assert(PGW_signal_dds_type_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_OK);
    assert(PGW_signal_validate_dds(&wire));
    assert(native.id == 1003 && native.value.kind == PGW_VALUE_BOOLEAN &&
           native.value.data.boolean);
    PGW_CANMetadata source_context = {0};
    PGW_SampleView view = {
        .kind = PGW_SAMPLE_VIEW_CANONICAL,
        .value = &native,
        .value_size = sizeof(native),
        .type_identity = &PGW_CAN_SIGNAL_VALUE_IDENTITY,
        .context = &source_context,
        .context_identity = &PGW_CAN_METADATA_IDENTITY
    };
    PGW_Signal viewed;
    assert(PGW_signal_from_view(&view, &viewed, sizeof(viewed)) == PGW_OK);
    assert(viewed.id == native.id && viewed.value.data.boolean);
    view.context = NULL;
    assert(PGW_signal_from_view(&view, &viewed, sizeof(viewed)) == PGW_INVALID);
    view.context = &source_context;
    PGW_DDSMetadata dds_metadata = {.valid_data = DDS_BOOLEAN_TRUE};
    view = (PGW_SampleView){
        .kind = PGW_SAMPLE_VIEW_NATIVE,
        .value = &wire,
        .type_identity = PGW_signal_dds_type_binding_powertrain.type_identity(),
        .context = &dds_metadata,
        .context_identity = &PGW_DDS_METADATA_IDENTITY
    };
    assert(PGW_signal_from_view(&view, &viewed, sizeof(viewed)) == PGW_OK);
    dds_metadata.valid_data = DDS_BOOLEAN_FALSE;
    assert(PGW_signal_from_view(&view, &viewed, sizeof(viewed)) == PGW_INVALID);
    wire.id = 1002;
    wire.value._d = VALUE_INT64;
    wire.value._u.integer_value = -100;
    assert(PGW_signal_dds_type_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_OK);
    assert(PGW_signal_validate_dds(&wire));
    assert(native.value.kind == PGW_VALUE_INT64 && native.value.data.integer == -100);
    wire.id = 1001;
    wire.value._d = VALUE_DOUBLE;
    wire.value._u.real_value = 123.4;
    assert(PGW_signal_dds_type_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_OK);
    assert(PGW_signal_validate_dds(&wire));
    assert(native.value.kind == PGW_VALUE_DOUBLE && native.value.data.real == 123.4);
    wire.value._u.real_value = NAN;
    assert(!PGW_signal_validate_dds(&wire));
    assert(PGW_signal_dds_type_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_INVALID);
    assert(native.value.data.real == 123.4);
    wire.value._d = VALUE_INT64;
    assert(!PGW_signal_validate_dds(&wire));
    assert(PGW_signal_dds_type_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_INVALID);
    wire.id = 9999;
    assert(!PGW_signal_validate_dds(&wire));
    assert(PGW_signal_dds_type_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_INVALID);
    assert(PGW_signal_dds_type_binding_powertrain.copy_native(NULL, &native, sizeof(native)) == PGW_INVALID);
    assert(PGW_signal_dds_type_binding_powertrain.copy_native(&wire, &native, sizeof(native) - 1) == PGW_INVALID);
    assert(strcmp(PGW_signal_dds_type_binding_powertrain.representation->schema->fingerprint,
                  PGW_codec_schema.fingerprint) == 0);
    assert(PGW_signal_dds_type_binding_powertrain.representation ==
           PGW_signal_dds_type_binding_auxiliary.representation);
    void *storage = malloc(8192);
    assert(storage);
    PGW_Arena arena = {storage, 8192, 0};
    void *state = NULL;
    assert(PGW_signal_dds_type_binding_powertrain.initialize(&arena, 0, &state) == PGW_INVALID);
    assert(PGW_signal_dds_type_binding_powertrain.initialize(&arena, 8, &state) == PGW_OK);
    assert(state && PGW_signal_dds_type_binding_powertrain.length(state) == 0);
    free(storage);
    return 0;
}
