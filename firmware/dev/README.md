# ch32fun dev patches (CH32V203)

Optional patches to cnlohr/ch32fun `ch32fun.c` that fix the automatic clock setup
for CH32V203 (flash wait-states at >24 MHz; HSE-enable preserving the active HSI).

NOT required by the shipped LED firmware (noknok_leds.c does its own manual HSE/PLL
bring-up). Kept for reference if anyone wants ch32fun's auto-clock to work on V203.
See the LED debug history in the Sam agent notes for full context.
