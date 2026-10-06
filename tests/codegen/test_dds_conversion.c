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
#include "signals.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    PGW_DDS_Signal wire;
    PGW_Signal native;
    assert(PGW_DDS_Signal_initialize(&wire));
    wire.id = 1003;
    wire.value._d = VALUE_BOOLEAN;
    wire.value._u.boolean_value = DDS_BOOLEAN_TRUE;
    assert(PGW_signal_dds_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_OK);
    assert(native.id == 1003 && native.value.kind == PGW_VALUE_BOOLEAN &&
           native.value.data.boolean);
    wire.id = 1002;
    wire.value._d = VALUE_INT64;
    wire.value._u.integer_value = -100;
    assert(PGW_signal_dds_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_OK);
    assert(native.value.kind == PGW_VALUE_INT64 && native.value.data.integer == -100);
    wire.id = 1001;
    wire.value._d = VALUE_DOUBLE;
    wire.value._u.real_value = 123.4;
    assert(PGW_signal_dds_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_OK);
    assert(native.value.kind == PGW_VALUE_DOUBLE && native.value.data.real == 123.4);
    wire.value._u.real_value = NAN;
    assert(PGW_signal_dds_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_INVALID);
    assert(native.value.data.real == 123.4);
    wire.value._d = VALUE_INT64;
    assert(PGW_signal_dds_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_INVALID);
    wire.id = 9999;
    assert(PGW_signal_dds_binding_powertrain.copy_native(&wire, &native, sizeof(native)) == PGW_INVALID);
    assert(PGW_signal_dds_binding_powertrain.copy_native(NULL, &native, sizeof(native)) == PGW_INVALID);
    assert(PGW_signal_dds_binding_powertrain.copy_native(&wire, &native, sizeof(native) - 1) == PGW_INVALID);
    assert(strcmp(PGW_signal_dds_binding_powertrain.representation->schema->fingerprint,
                  PGW_codec_schema.fingerprint) == 0);
    assert(PGW_signal_dds_binding_powertrain.representation ==
           PGW_signal_dds_binding_auxiliary.representation);
    void *storage = malloc(8192);
    assert(storage);
    PGW_Arena arena = {storage, 8192, 0};
    void *state = NULL;
    assert(PGW_signal_dds_binding_powertrain.initialize(&arena, 0, &state) == PGW_INVALID);
    assert(PGW_signal_dds_binding_powertrain.initialize(&arena, 8, &state) == PGW_OK);
    assert(state && PGW_signal_dds_binding_powertrain.length(state) == 0);
    free(storage);
    return 0;
}
