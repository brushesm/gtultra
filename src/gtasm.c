// Command-line wrapper for GTUltra's embedded Magnus/Exomizer-style assembler.

#include "membuf.h"
#include "parse.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum OutputMode
{
    OUTPUT_RAW,
    OUTPUT_PRG,
    OUTPUT_SID
} OutputMode;

typedef struct Options
{
    const char *inputPath;
    const char *outputPath;
    OutputMode mode;
    int loadAddress;
    int haveLoadAddress;
    int initAddress;
    int haveInitAddress;
    int playAddress;
    int havePlayAddress;
} Options;

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: gtasm [options] <input.asm> <output>\n"
        "\n"
        "Options:\n"
        "  --raw                    Write raw assembled bytes (default)\n"
        "  --prg                    Prefix a C64 two-byte load address\n"
        "  --sid                    Write a PSID file containing the PRG data\n"
        "  --load-address <addr>    Load address for --prg/--sid, e.g. $1000 or 0x1000\n"
        "  --init-address <addr>    SID init address (default: load address)\n"
        "  --play-address <addr>    SID play address (default: load address + 3)\n"
        "  -h, --help               Show this help text\n");
}

static int parse_u16(const char *text, int *out)
{
    char *end = NULL;
    unsigned long value;
    int base = 10;

    if (text == NULL || *text == '\0') {
        return 0;
    }

    if (text[0] == '$') {
        text++;
        base = 16;
        if (*text == '\0') {
            return 0;
        }
    } else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
    }

    errno = 0;
    value = strtoul(text, &end, base);
    if (errno != 0 || end == text || value > 0xffffUL) {
        return 0;
    }

    while (*end != '\0') {
        if (!isspace((unsigned char)*end)) {
            return 0;
        }
        end++;
    }

    *out = (int)value;
    return 1;
}

static int read_file_to_membuf(const char *path, struct membuf *out)
{
    FILE *in;
    unsigned char block[4096];
    size_t count;

    in = fopen(path, "rb");
    if (in == NULL) {
        fprintf(stderr, "gtasm: error: could not open input '%s'\n", path);
        return 0;
    }

    do {
        count = fread(block, 1, sizeof block, in);
        if (count > 0) {
            membuf_append(out, block, (int)count);
        }
    } while (count == sizeof block);

    if (ferror(in)) {
        fprintf(stderr, "gtasm: error: could not read input '%s'\n", path);
        fclose(in);
        return 0;
    }

    fclose(in);
    return 1;
}

static int write_file_from_membuf(const char *path, struct membuf *in)
{
    FILE *out;
    int len = membuf_memlen(in);

    out = fopen(path, "wb");
    if (out == NULL) {
        fprintf(stderr, "gtasm: error: could not open output '%s'\n", path);
        return 0;
    }

    if (len > 0 && fwrite(membuf_get(in), 1, (size_t)len, out) != (size_t)len) {
        fprintf(stderr, "gtasm: error: could not write output '%s'\n", path);
        fclose(out);
        return 0;
    }

    if (fclose(out) != 0) {
        fprintf(stderr, "gtasm: error: could not close output '%s'\n", path);
        return 0;
    }

    return 1;
}

static void append_u16be(struct membuf *out, int value)
{
    unsigned char bytes[2];

    bytes[0] = (unsigned char)((value >> 8) & 0xff);
    bytes[1] = (unsigned char)(value & 0xff);
    membuf_append(out, bytes, sizeof bytes);
}

static void append_u32be(struct membuf *out, unsigned long value)
{
    unsigned char bytes[4];

    bytes[0] = (unsigned char)((value >> 24) & 0xff);
    bytes[1] = (unsigned char)((value >> 16) & 0xff);
    bytes[2] = (unsigned char)((value >> 8) & 0xff);
    bytes[3] = (unsigned char)(value & 0xff);
    membuf_append(out, bytes, sizeof bytes);
}

static void append_fixed_text(struct membuf *out, const char *text, int width)
{
    int len = (int)strlen(text);
    unsigned char zero = 0;

    if (len > width) {
        len = width;
    }

    if (len > 0) {
        membuf_append(out, text, len);
    }

    for (int i = len; i < width; i++) {
        membuf_append(out, &zero, 1);
    }
}

static const char *line_end(const char *p, const char *end)
{
    while (p < end && *p != '\n' && *p != '\r') {
        p++;
    }
    return p;
}

static const char *skip_spaces(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }
    return p;
}

static int parse_assignment_address(const char *line, const char *end, const char *name, int *out)
{
    size_t nameLen = strlen(name);
    char value[32];
    size_t valueLen = 0;

    line = skip_spaces(line, end);
    if ((size_t)(end - line) < nameLen || memcmp(line, name, nameLen) != 0) {
        return 0;
    }

    line += nameLen;
    if (line < end && (isalnum((unsigned char)*line) || *line == '_')) {
        return 0;
    }

    line = skip_spaces(line, end);
    if (line >= end || *line != '=') {
        return 0;
    }

    line++;
    line = skip_spaces(line, end);
    while (line < end && valueLen + 1 < sizeof value) {
        if (!(isalnum((unsigned char)*line) || *line == '$' || *line == 'x' || *line == 'X')) {
            break;
        }
        value[valueLen++] = *line++;
    }
    value[valueLen] = '\0';

    return parse_u16(value, out);
}

static int infer_load_address(struct membuf *source, int *out)
{
    const char *p = (const char *)membuf_get(source);
    const char *end = p + membuf_memlen(source);

    while (p < end) {
        const char *eol = line_end(p, end);
        if (parse_assignment_address(p, eol, "base", out)) {
            return 1;
        }
        p = eol;
        while (p < end && (*p == '\n' || *p == '\r')) {
            p++;
        }
    }

    return 0;
}

static int find_assignment_address(struct membuf *source, const char *name, int *out)
{
    const char *p = (const char *)membuf_get(source);
    const char *end = p + membuf_memlen(source);

    while (p < end) {
        const char *eol = line_end(p, end);
        if (parse_assignment_address(p, eol, name, out)) {
            return 1;
        }
        p = eol;
        while (p < end && (*p == '\n' || *p == '\r')) {
            p++;
        }
    }

    return 0;
}

static void append_prg_image(struct membuf *out, struct membuf *assembled, int loadAddress)
{
    unsigned char loadBytes[2];

    loadBytes[0] = (unsigned char)(loadAddress & 0xff);
    loadBytes[1] = (unsigned char)((loadAddress >> 8) & 0xff);
    membuf_append(out, loadBytes, sizeof loadBytes);
    membuf_append(out, membuf_get(assembled), membuf_memlen(assembled));
}

static int append_sid_image(struct membuf *out, struct membuf *source, struct membuf *assembled, const Options *options)
{
    int sid2Base = 0;
    int numChannels = 3;
    int version = 2;
    int flags = 0x0014; /* PAL + 6581. */
    int sid2Address = 0;

    find_assignment_address(source, "SID2BASE", &sid2Base);
    find_assignment_address(source, "NUMCHANNELS", &numChannels);

    if (sid2Base != 0 || numChannels > 3) {
        version = 3;
        flags = 0x0054; /* PAL + 6581 for SID1/SID2. */
        if (sid2Base != 0) {
            sid2Address = (sid2Base & 0x0ff0) >> 4;
        }
    }

    membuf_append(out, "PSID", 4);
    append_u16be(out, version);
    append_u16be(out, 0x007c);
    append_u16be(out, 0x0000); /* Load address is taken from the following PRG data. */
    append_u16be(out, options->initAddress);
    append_u16be(out, options->playAddress);
    append_u16be(out, 1); /* songs */
    append_u16be(out, 1); /* start song */
    append_u32be(out, 0); /* PAL/vblank speed bits */
    append_fixed_text(out, "GTUltra assembled tune", 32);
    append_fixed_text(out, "", 32);
    append_fixed_text(out, "", 32);
    append_u16be(out, flags);

    if (version >= 3) {
        unsigned char relocAndSidAddresses[4];

        relocAndSidAddresses[0] = 0;
        relocAndSidAddresses[1] = 0;
        relocAndSidAddresses[2] = sid2Address & 0xff;
        relocAndSidAddresses[3] = 0;
        membuf_append(out, relocAndSidAddresses, sizeof relocAndSidAddresses);
    } else {
        append_u32be(out, 0);
    }

    append_prg_image(out, assembled, options->loadAddress);
    return 1;
}

static int parse_args(int argc, char **argv, Options *options)
{
    int positional = 0;

    memset(options, 0, sizeof *options);
    options->mode = OUTPUT_RAW;
    options->loadAddress = -1;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        } else if (strcmp(arg, "--raw") == 0) {
            options->mode = OUTPUT_RAW;
        } else if (strcmp(arg, "--prg") == 0) {
            options->mode = OUTPUT_PRG;
        } else if (strcmp(arg, "--sid") == 0) {
            options->mode = OUTPUT_SID;
        } else if (strcmp(arg, "--load-address") == 0) {
            if (i + 1 >= argc || !parse_u16(argv[++i], &options->loadAddress)) {
                fprintf(stderr, "gtasm: error: invalid --load-address value\n");
                return 0;
            }
            options->haveLoadAddress = 1;
        } else if (strcmp(arg, "--init-address") == 0) {
            if (i + 1 >= argc || !parse_u16(argv[++i], &options->initAddress)) {
                fprintf(stderr, "gtasm: error: invalid --init-address value\n");
                return 0;
            }
            options->haveInitAddress = 1;
        } else if (strcmp(arg, "--play-address") == 0) {
            if (i + 1 >= argc || !parse_u16(argv[++i], &options->playAddress)) {
                fprintf(stderr, "gtasm: error: invalid --play-address value\n");
                return 0;
            }
            options->havePlayAddress = 1;
        } else if (arg[0] == '-') {
            fprintf(stderr, "gtasm: error: unknown option '%s'\n", arg);
            return 0;
        } else if (positional == 0) {
            options->inputPath = arg;
            positional++;
        } else if (positional == 1) {
            options->outputPath = arg;
            positional++;
        } else {
            fprintf(stderr, "gtasm: error: too many positional arguments\n");
            return 0;
        }
    }

    if (positional != 2) {
        fprintf(stderr, "gtasm: error: expected input and output paths\n");
        return 0;
    }

    return 1;
}

int main(int argc, char **argv)
{
    Options options;
    struct membuf source;
    struct membuf assembled;
    struct membuf output;
    int ok = 0;

    if (!parse_args(argc, argv, &options)) {
        usage(stderr);
        return 2;
    }

    membuf_init(&source);
    membuf_init(&assembled);
    membuf_init(&output);

    if (!read_file_to_membuf(options.inputPath, &source)) {
        goto cleanup;
    }

    if ((options.mode == OUTPUT_PRG || options.mode == OUTPUT_SID) && !options.haveLoadAddress) {
        if (!infer_load_address(&source, &options.loadAddress)) {
            fprintf(stderr, "gtasm: error: PRG/SID output needs --load-address when no 'base = <addr>' assignment is present\n");
            goto cleanup;
        }
        options.haveLoadAddress = 1;
    }

    if (options.mode == OUTPUT_SID) {
        if (!options.haveInitAddress) {
            options.initAddress = options.loadAddress;
            options.haveInitAddress = 1;
        }
        if (!options.havePlayAddress) {
            options.playAddress = (options.loadAddress + 3) & 0xffff;
            options.havePlayAddress = 1;
        }
    }

    if (assemble(&source, &assembled) != 0) {
        fprintf(stderr, "gtasm: error: assembly failed\n");
        goto cleanup;
    }

    if (options.mode == OUTPUT_PRG) {
        append_prg_image(&output, &assembled, options.loadAddress);
    } else if (options.mode == OUTPUT_SID) {
        append_sid_image(&output, &source, &assembled, &options);
    } else {
        membuf_append(&output, membuf_get(&assembled), membuf_memlen(&assembled));
    }

    ok = write_file_from_membuf(options.outputPath, &output);

cleanup:
    membuf_free(&source);
    membuf_free(&assembled);
    membuf_free(&output);

    return ok ? 0 : 1;
}
