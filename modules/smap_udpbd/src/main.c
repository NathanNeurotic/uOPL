#include <errno.h>
#include <stdio.h>
#include <loadcore.h>
#include <thbase.h>
#include <irx.h>

#include "main.h"
#include "xfer.h"
#include "ministack.h"
#ifndef NO_UDPBD
#include "udpbd.h"
#endif
#ifndef NO_TTY
#include "udptty.h"
#endif
int udpbd_init(uint32_t myIp, uint32_t routerIP, uint32_t serverIP);

// Last SDK 3.1.0 has INET family version "2.26.0"
// SMAP module is the same as "2.25.0"
IRX_ID("SMAP_driver", 0x2, 0x1A);

//While the header of the export table is small, the large size of the export table (as a whole) places it in data instead of sdata.
extern struct irx_export_table _exp_smap __attribute__((section("data")));

uint32_t mini_atoi(const char *num)
{
    uint32_t ret = 0;
    while (*num != '\0') {
        char c = *num++;
        if (c < '0' || c > '9')
            break;
        ret *= 10;
        ret += c - '0';
    }

    return ret;
}

uint32_t mini_inet_addr(const char *adrs)
{
    uint32_t parts[4];
    int partsCnt = 1;
    parts[0] = mini_atoi(adrs);

    for (; partsCnt < 4; partsCnt++) {
        while (*adrs != '.')
            if (*adrs++ == '\0')
                goto out;
        adrs++;
        parts[partsCnt] = mini_atoi(adrs);
    }

out:
    if (partsCnt == 4)
        return (parts[0] << 24) | ((parts[1] & 0xff) << 16) | ((parts[2] & 0xff) << 8) | (parts[3] & 0xff);
    if (partsCnt == 3)
        return (parts[0] << 24) | ((parts[1] & 0xff) << 16) | (parts[2] & 0xffff);
    if (partsCnt == 2)
        return (parts[0] << 24) | (parts[1] & 0xffffff);

    return parts[0];
}

int _start(int argc, char *argv[])
{
    int result;
    uint32_t IP, SW, GW;

    // Parse IP args.
    if (argc >= 4) {
        // DEBUG_PRINTF("SMAP UDPBD: %s %s %s\n", argv[1], argv[2], argv[3]);
        IP = mini_inet_addr(argv[1]);
        SW = mini_inet_addr(argv[2]);
        GW = mini_inet_addr(argv[3]);
    } else {
        // Set some defaults.
        IP = IP_ADDR(192, 168, 0, 82);
        SW = IP_ADDR(192, 168, 0, 60);
        GW = IP_ADDR(192, 168, 0, 1);
    }

    if (RegisterLibraryEntries(&_exp_smap) != 0) {
        PRINTF("smap: module already loaded\n");
        return MODULE_NO_RESIDENT_END;
    }

    if ((result = smap_init(argc, argv)) < 0) {
        PRINTF("smap: smap_init -> %d\n", result);
        ReleaseLibraryEntries(&_exp_smap);
        return MODULE_NO_RESIDENT_END;
    }

#ifndef NO_UDPBD
    udpbd_init(IP, GW, SW);
#endif
#ifndef NO_TTY
    udptty_init(SW);
#endif

    return MODULE_RESIDENT_END;
}
