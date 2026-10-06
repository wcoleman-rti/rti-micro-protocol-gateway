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

#include "control_binding.h"
#include "control_resources.h"
#include "ddsAppgen.h"
#include "osapi/osapi_thread.h"
#include "pgw/runtime.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_number(const char *text, unsigned long maximum, unsigned long *value)
{
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || !*text || *end || parsed > maximum) return false;
    *value = parsed;
    return true;
}

static const char *outcome_name(PGW_ControlOutcome outcome)
{
    static const char *const names[] = {
        "APPLIED", "NO_CHANGE", "UNSUPPORTED", "INVALID", "FAILED"
    };
    return (unsigned)outcome < sizeof(names) / sizeof(names[0]) ?
        names[outcome] : "INVALID_OUTCOME";
}

int main(int argc, char **argv)
{
    bool expect_telemetry = argc == 5 && !strcmp(argv[4], "--telemetry");
    if ((argc != 4 && !expect_telemetry) ||
        (argc == 5 && !expect_telemetry)) {
        fprintf(stderr, "usage: %s <domain-id> <resource-enum-value> "
                "<action-enum-value> [--telemetry]\n",
                argv[0]);
        return 2;
    }
    unsigned long domain, resource, action;
    if (!parse_number(argv[1], 232, &domain) ||
        !parse_number(argv[2], PGW_CONTROL_RESOURCE_COUNT - 1u, &resource) ||
        !parse_number(argv[3], PGW_CONTROL_ROUTE_RESUME, &action)) {
        fprintf(stderr, "domain/resource/action is outside its generated range\n");
        return 2;
    }
    PGW_DDSRemoteControlOptions options = {
        PGW_DDS_REMOTE_CONTROL_OPTIONS_VERSION,
        sizeof(PGW_DDSRemoteControlOptions),
        true,
        (DDS_DomainId_t)domain,
        PGW_CONTROL_MAX_CONTROLLER_PEERS,
        0,
        PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS
    };
    PGW_Status status = PGW_DDS_remote_control_options_validate(&options,
        PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0,
        PGW_CONTROL_TELEMETRY_MINIMUM_PERIOD_MS);
    if (status != PGW_OK || !PGW_Runtime_initialize()) {
        fprintf(stderr, "DDS control options/runtime initialization failed: %s\n",
                PGW_status_name(status));
        return 1;
    }

    int result_code = 1;
    DDS_DomainParticipant *participant = NULL;
    PGW_DDSStaticEndpoint endpoints[
        3 + (PGW_CONTROL_TELEMETRY_METRIC_COUNT ? 1 : 0)];
    size_t endpoint_count = 0;
    DDS_DataWriter *commands = NULL;
    DDS_DataReader *states = NULL, *results = NULL, *telemetry = NULL;
    status = PGW_DDS_register_model(APPGEN_get_library_seq());
    if (status != PGW_OK) {
        fprintf(stderr, "registering controller AppGen model failed: %s\n",
                PGW_status_name(status));
        goto done;
    }
    status = PGW_DDS_create_controller_participant(&options, endpoints,
        sizeof(endpoints) / sizeof(endpoints[0]), &endpoint_count, &participant);
    if (status != PGW_OK ||
        endpoint_count != 3 + PGW_CONTROL_TELEMETRY_METRIC_COUNT) {
        fprintf(stderr, "creating controller participant/entities failed: %s (%zu endpoints)\n",
                PGW_status_name(status), endpoint_count);
        goto done;
    }
    for (size_t i = 0; i < endpoint_count; ++i) {
        if (!strcmp(endpoints[i].name, "ControlCommand") && !endpoints[i].reader)
            commands = endpoints[i].datawriter;
        else if (!strcmp(endpoints[i].name, "ControlState") && endpoints[i].reader)
            states = endpoints[i].datareader;
        else if (!strcmp(endpoints[i].name, "ControlResult") && endpoints[i].reader)
            results = endpoints[i].datareader;
        else if (!strcmp(endpoints[i].name, "ControlTelemetry") && endpoints[i].reader)
            telemetry = endpoints[i].datareader;
    }
    if (!commands || !states || !results ||
        ((PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0) != (telemetry != NULL)) ||
        (expect_telemetry && !telemetry)) goto done;
    struct DDS_PublicationMatchedStatus matched;
    matched.current_count = 0;
    for (unsigned attempt = 0; attempt < 200 && matched.current_count == 0; ++attempt) {
        if (DDS_DataWriter_get_publication_matched_status(commands, &matched) != DDS_RETCODE_OK) {
            fprintf(stderr, "querying ControlCommand matches failed\n");
            goto done;
        }
        if (!matched.current_count) OSAPI_Thread_sleep(25);
    }
    printf("ControlCommand matched readers=%d\n", matched.current_count);
    if (!matched.current_count) {
        struct DDS_OfferedIncompatibleQosStatus incompatible;
        if (DDS_DataWriter_get_offered_incompatible_qos_status(commands, &incompatible) ==
            DDS_RETCODE_OK)
            fprintf(stderr, "ControlCommand has no compatible readers; incompatible=%d\n",
                    incompatible.total_count);
        goto done;
    }
    PGW_ControlCommand command = {(uint32_t)resource, (PGW_ControlAction)action};
    if (PGW_example_control_types.write_command(commands, &command) != DDS_RETCODE_OK) {
        fprintf(stderr, "DDS rejected the command write; no delivery is implied\n");
        goto done;
    }
    printf("command published: domain=%lu resource=%lu action=%lu\n",
           domain, resource, action);
    static const PGW_ControlResourceStatus desired_status[] = {
        PGW_CONTROL_STATUS_UP,
        PGW_CONTROL_STATUS_DOWN,
        PGW_CONTROL_STATUS_UP,
        PGW_CONTROL_STATUS_DOWN,
        PGW_CONTROL_STATUS_UP,
        PGW_CONTROL_STATUS_DOWN,
        PGW_CONTROL_STATUS_PAUSED,
        PGW_CONTROL_STATUS_UP
    };
    bool result_received = false;
    bool state_received = false;
    bool telemetry_received = false;
    for (unsigned attempt = 0; attempt < 250 &&
         (!result_received || !state_received ||
          (expect_telemetry && !telemetry_received)); ++attempt) {
        PGW_ControlState state;
        PGW_Status state_status;
        while ((state_status = PGW_example_control_types.take_state(states, &state)) == PGW_OK) {
            printf("state: resource=%u status=%u command-mask=0x%08x telemetry-mask=0x%08x\n",
                   state.resource_id, (unsigned)state.status,
                   state.command_capabilities, state.telemetry_capabilities);
            if (state.resource_id == resource && state.status == desired_status[action])
                state_received = true;
        }
        if (state_status != PGW_NO_DATA) goto done;
        PGW_ControlResult result;
        PGW_Status result_status = PGW_example_control_types.take_result(results, &result);
        if (result_status == PGW_OK) {
            printf("result: %s publication-sequence=%d:%u handle=",
                   outcome_name(result.outcome),
                   result.correlation.publication_sequence_high,
                   result.correlation.publication_sequence_low);
            for (size_t i = 0; i < sizeof(result.correlation.publication_handle); ++i)
                printf("%02x", result.correlation.publication_handle[i]);
            putchar('\n');
            result_received = true;
        } else if (result_status != PGW_NO_DATA) goto done;
#if PGW_CONTROL_TELEMETRY_METRIC_COUNT > 0
        if (telemetry && !telemetry_received) {
            PGW_ControlTelemetry sample;
            PGW_Status telemetry_status =
                PGW_example_control_types.take_telemetry(telemetry, &sample);
            if (telemetry_status == PGW_OK) {
                printf("telemetry: resource=%u kind=%u scalar=%u unit=%s ",
                       sample.resource_id, sample.telemetry_kind,
                       (unsigned)sample.scalar.type, sample.unit);
                switch (sample.scalar.type) {
                    case PGW_CONTROL_SCALAR_UINT64:
                        printf("value=%llu\n",
                               (unsigned long long)sample.scalar.value.uint64_value);
                        break;
                    case PGW_CONTROL_SCALAR_INT64:
                        printf("value=%lld\n",
                               (long long)sample.scalar.value.int64_value);
                        break;
                    case PGW_CONTROL_SCALAR_UINT32:
                        printf("value=%u\n", sample.scalar.value.uint32_value);
                        break;
                    case PGW_CONTROL_SCALAR_INT32:
                        printf("value=%d\n", sample.scalar.value.int32_value);
                        break;
                    case PGW_CONTROL_SCALAR_BOOLEAN:
                        printf("value=%s\n",
                               sample.scalar.value.boolean_value ? "true" : "false");
                        break;
                    case PGW_CONTROL_SCALAR_DOUBLE:
                        printf("value=%.17g\n", sample.scalar.value.double_value);
                        break;
                }
                telemetry_received = true;
            } else if (telemetry_status != PGW_NO_DATA) goto done;
        }
#endif
        if (!result_received || !state_received ||
            (expect_telemetry && !telemetry_received))
            OSAPI_Thread_sleep(20);
    }
    if (result_received) {
        PGW_ControlState state;
        PGW_Status state_status;
        while ((state_status = PGW_example_control_types.take_state(states, &state)) == PGW_OK)
            printf("state: resource=%u status=%u command-mask=0x%08x telemetry-mask=0x%08x\n",
                   state.resource_id, (unsigned)state.status,
                   state.command_capabilities, state.telemetry_capabilities);
        if (state_status != PGW_NO_DATA) result_received = false;
    }
    result_code = result_received && state_received &&
        (!expect_telemetry || telemetry_received) ? 0 : 1;
done:
    if (participant && PGW_DDS_delete_dynamic_participant(participant) != PGW_OK)
        result_code = 1;
    return result_code;
}
