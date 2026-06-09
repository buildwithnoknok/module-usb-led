#!/usr/bin/env python3
# Add CH32V20x/V30x flash wait-states to ch32fun SystemInit (missing upstream).
# Uses raw LATENCY[1:0] bits since V20x header lacks FLASH_ACTLR_LATENCY_* macros.
p = '/home/noknok/dev/ch32fun/ch32fun/ch32fun.c'
s = open(p).read()
anchor = '#ifndef CH5xx\n\tRCC->INTR  = 0x009F0000;'
block = (
    '// CH32V20x/V30x flash latency (ADDED by noknok: ch32fun omits this; needed >24MHz)\n'
    '#if defined(CH32V20x) || defined(CH32V30x)\n'
    '\t#if FUNCONF_SYSTEM_CORE_CLOCK > 48000000\n'
    '\t\tFLASH->ACTLR = (FLASH->ACTLR & ~0x07u) | 0x02u;\n'
    '\t#elif FUNCONF_SYSTEM_CORE_CLOCK > 24000000\n'
    '\t\tFLASH->ACTLR = (FLASH->ACTLR & ~0x07u) | 0x01u;\n'
    '\t#else\n'
    '\t\tFLASH->ACTLR = (FLASH->ACTLR & ~0x07u) | 0x00u;\n'
    '\t#endif\n'
    '#endif\n\n'
)
if 'ADDED by noknok' in s:
    print('ALREADY PATCHED')
elif anchor not in s:
    print('ANCHOR NOT FOUND - aborting')
else:
    s = s.replace(anchor, block + anchor, 1)
    open(p, 'w').write(s)
    print('PATCHED OK' if 'ADDED by noknok' in s else 'WRITE FAILED')
