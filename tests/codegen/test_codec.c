#include "pgw_codec.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <string.h>

static const PGW_Signal *find(const PGW_Signal *signals, size_t count, uint32_t id)
{
    for (size_t i = 0; i < count; ++i)
        if (signals[i].id == id) return &signals[i];
    assert(0);
    return NULL;
}

int main(void)
{
    uint8_t classic[8] = {0xd2, 0x04, 0xf9, 0xca, 0xa5, 0x03, 0x55, 0xaa};
    uint8_t expected[8] = {0xd2, 0x04, 0x14, 0xda, 0xa5, 0x03, 0x55, 0xaa};
    PGW_Signal signals[8], command;
    size_t count = 99;
    memset(signals, 0x55, sizeof(signals));
    PGW_Signal untouched[8];
    memcpy(untouched, signals, sizeof(signals));
    assert(PGW_codec_decode(256, false, false, classic, 8, signals, 3, &count)
        == PGW_CODEC_BUFFER_TOO_SMALL);
    assert(count == 4 && memcmp(signals, untouched, sizeof(signals)) == 0);
    assert(PGW_codec_decode(256, false, false, classic, 8, signals, 8, &count)
        == PGW_CODEC_OK);
    assert(count == 4);
    assert(find(signals, count, 1003)->value.kind == PGW_VALUE_BOOLEAN);
    assert(find(signals, count, 1003)->value.data.boolean);
    assert(find(signals, count, 1002)->value.data.integer == -100);
    assert(fabs(find(signals, count, 1001)->value.data.real - 123.4) < 1e-10);
    assert(find(signals, count, 1004)->value.data.integer == 3);
    command = (PGW_Signal){1002, {PGW_VALUE_INT64, {.integer = 333}}};
    assert(PGW_codec_patch(&command, classic, 8) == PGW_CODEC_OK);
    assert(memcmp(classic, expected, 8) == 0);
    command = (PGW_Signal){1001, {PGW_VALUE_DOUBLE, {.real = 999.9}}};
    assert(PGW_codec_patch(&command, classic, 8) == PGW_CODEC_OK);
    assert(classic[0] == 0x0f && classic[1] == 0x27);
    command = (PGW_Signal){1003, {PGW_VALUE_BOOLEAN, {.boolean = false}}};
    assert(PGW_codec_patch(&command, classic, 8) == PGW_CODEC_OK);
    assert(classic[4] == 0xa4);
    memcpy(expected, classic, 8);
    command = (PGW_Signal){1001, {PGW_VALUE_INT64, {.integer = 1}}};
    assert(PGW_codec_patch(&command, classic, 8) == PGW_CODEC_WRONG_TYPE);
    command = (PGW_Signal){1001, {PGW_VALUE_DOUBLE, {.real = NAN}}};
    assert(PGW_codec_patch(&command, classic, 8) == PGW_CODEC_OUT_OF_RANGE);
    command.value.data.real = INFINITY;
    assert(PGW_codec_patch(&command, classic, 8) == PGW_CODEC_OUT_OF_RANGE);
    command.value.data.real = -1;
    assert(PGW_codec_patch(&command, classic, 8) == PGW_CODEC_OUT_OF_RANGE);
    command = (PGW_Signal){1004, {PGW_VALUE_INT64, {.integer = 4}}};
    assert(PGW_codec_patch(&command, classic, 8) == PGW_CODEC_OUT_OF_RANGE);
    command.id = 999;
    assert(PGW_codec_patch(&command, classic, 8) == PGW_CODEC_UNKNOWN_SIGNAL);
    assert(memcmp(expected, classic, 8) == 0);
    assert(PGW_codec_decode(256, true, false, classic, 8, signals, 8, &count)
        == PGW_CODEC_UNKNOWN_MESSAGE);
    assert(PGW_codec_decode(256, false, true, classic, 8, signals, 8, &count)
        == PGW_CODEC_INVALID_ARGUMENT);
    assert(PGW_codec_decode(256, false, false, classic, 7, signals, 8, &count)
        == PGW_CODEC_INVALID_ARGUMENT);

    uint8_t fd[16] = {0xa1, 0xec, 0xbf, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0x34, 0x12, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
    uint8_t fd_expected[16];
    assert(PGW_codec_decode(0x123, true, true, fd, 16, signals, 8, &count) == PGW_CODEC_OK);
    assert(count == 3);
    assert(find(signals, count, 2002)->value.data.real == -45);
    assert(find(signals, count, 2004)->value.data.integer == 0x1234);
    memcpy(fd_expected, fd, 16);
    command = (PGW_Signal){2003, {PGW_VALUE_INT64, {.integer = 14}}};
    assert(PGW_codec_patch(&command, fd, 16) == PGW_CODEC_INACTIVE_MULTIPLEX);
    command = (PGW_Signal){2001, {PGW_VALUE_INT64, {.integer = 2}}};
    assert(PGW_codec_patch(&command, fd, 16) == PGW_CODEC_SELECTOR_CHANGE);
    assert(memcmp(fd_expected, fd, 16) == 0);
    command = (PGW_Signal){2002, {PGW_VALUE_DOUBLE, {.real = -39.625}}};
    assert(PGW_codec_patch(&command, fd, 16) == PGW_CODEC_OK);
    fd_expected[1] = 2;
    fd_expected[2] = 0xb0;
    assert(memcmp(fd_expected, fd, 16) == 0);
    command.value.data.real = -40.375;
    assert(PGW_codec_patch(&command, fd, 16) == PGW_CODEC_OK);
    assert(fd[1] == 0xfe && fd[2] == 0xbf);
    fd[0] = 0xa2; /* A newly received baseline selects another branch. */
    command = (PGW_Signal){2003, {PGW_VALUE_INT64, {.integer = 13}}};
    assert(PGW_codec_patch(&command, fd, 16) == PGW_CODEC_OK);
    assert(fd[1] == 2 && fd[2] == 0xb0);
    assert(PGW_codec_decode(0x123, true, true, fd, 16, signals, 8, &count) == PGW_CODEC_OK);
    assert(count == 3 && find(signals, count, 2003)->value.data.integer == 14);
    assert(PGW_codec_signal_find(1004)->choice_count == 4);
    assert(strcmp(PGW_codec_signal_find(1004)->choices[2].label, "Fault") == 0);
    assert(PGW_codec_schema.version == 1 && strlen(PGW_codec_schema.fingerprint) == 64);
    return 0;
}
