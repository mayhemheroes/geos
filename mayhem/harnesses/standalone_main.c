/*
 * standalone_main.c — single-input run-once driver (no libFuzzer runtime).
 * Reads one file path from argv[1], feeds the whole file to LLVMFuzzerTestOneInput
 * once, and returns. Used to build the `-standalone` reproducer binaries so a
 * crashing input can be replayed outside the fuzzing engine.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *pData, size_t size);

int main(int argc, char **argv) {
    FILE *pFile;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
        return 1;
    }
    pFile = fopen(argv[1], "rb");
    if (pFile == NULL) {
        fprintf(stderr, "failed to open file\n");
        return 2;
    }
    fseek(pFile, 0, SEEK_END);
    const long size = ftell(pFile);
    fseek(pFile, 0, SEEK_SET);
    if (size < 0) {
        fclose(pFile);
        return 3;
    }
    uint8_t *pData = (uint8_t *)malloc((size_t)size ? (size_t)size : 1);
    if (pData == NULL) {
        fclose(pFile);
        return 3;
    }
    size_t r = size ? fread(pData, (size_t)size, 1, pFile) : 0;
    fclose(pFile);
    if (size && r != 1) {
        fprintf(stderr, "read failed\n");
        free(pData);
        return 4;
    }
    LLVMFuzzerTestOneInput(pData, (size_t)size);
    free(pData);
    return 0;
}
