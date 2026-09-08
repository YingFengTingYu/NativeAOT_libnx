// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <stdint.h>
#include <stdio.h>

struct Snapshot
{
    uint64_t General[17];
    uint64_t Padding;
    uint8_t Vector[32][16];
};

extern uintptr_t CaptureTlsRegisters(int thunk, struct Snapshot* snapshot);

int main(void)
{
    for (int iteration = 0; iteration < 1000; iteration++)
    {
        for (int thunk = 0; thunk < 2; thunk++)
        {
            struct Snapshot snapshot = {0};
            uintptr_t result = CaptureTlsRegisters(thunk, &snapshot);
            if (result != (thunk ? 0x5678u : 0x1234u))
                return 1;

            for (int reg = 1; reg <= 17; reg++)
            {
                if (snapshot.General[reg - 1] != (uint64_t)(0x200 + reg))
                {
                    fprintf(stderr, "TLS lookup clobbered x%d\n", reg);
                    return 2;
                }
            }

            for (int reg = 0; reg < 32; reg++)
            {
                for (int lane = 0; lane < 16; lane++)
                {
                    if (snapshot.Vector[reg][lane] != 0x40 + reg)
                    {
                        fprintf(stderr, "TLS lookup clobbered v%d byte %d\n", reg, lane);
                        return 3;
                    }
                }
            }
        }
    }
    puts("PASS: both TLS bridges preserve x1-x17 and all 128 bits of v0-v31");
    return 0;
}
