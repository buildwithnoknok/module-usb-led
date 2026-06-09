#!/usr/bin/env python3
# Fix ch32fun HSE enable: it does `RCC->CTLR = RCC_HSEON;` which clears HSION
# while the CPU is still clocked by HSI -> on CH32V203 this freezes the core.
# Keep HSI on while bringing up HSE.
import re
p = '/home/noknok/dev/ch32fun/ch32fun/ch32fun.c'
s = open(p).read()
old = re.search(r'RCC->CTLR = RCC_HSEON;[ \t]*// Only turn on HSE\.', s)
if 'noknok: keep HSI on' in s:
    print('ALREADY PATCHED')
elif not old:
    print('ANCHOR NOT FOUND')
else:
    new = 'RCC->CTLR = BASE_CTLR | RCC_HSION | RCC_HSEON; // noknok: keep HSI on (CH32V203 freezes if active HSI is cleared)'
    s = s[:old.start()] + new + s[old.end():]
    open(p, 'w').write(s)
    print('PATCHED OK' if 'noknok: keep HSI on' in s else 'WRITE FAILED')
