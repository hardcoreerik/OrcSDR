#pragma once

/*
 * ATTINY88 panel controller on a Raspberry Pi 7"-compatible MIPI-DSI module.
 *
 * Our unit answers on I2C address 0x45 and returns 0xC3 from REG_ID, which the
 * upstream kernel driver accepts as a known revision (0xDE = v1, 0xC3 = v2).
 *
 * Register semantics below are hardware facts taken from the mainline kernel
 * driver drivers/regulator/rpi-panel-attiny-regulator.c. Only the addresses and
 * values are reused here; the implementation is written from scratch.
 *
 * Sequence the kernel performs:
 *   probe:  POWERON=0, 30 ms, PWM=0
 *   enable: PORTC=0 (5-10 ms), PORTA=PA_LCD_LR (5-10 ms),
 *           PORTB=PB_LCD_MAIN (5-10 ms), PORTC=PC_LED_EN, 80 ms
 *   backlight: PWM = 0..0xff
 * Without this the bridge stays powered down and the backlight stays off, which
 * looks identical to a completely dead panel.
 */

#define ATTINY_I2C_ADDR      0x45

#define ATTINY_REG_ID        0x80
#define ATTINY_REG_PORTA     0x81
#define ATTINY_REG_PORTB     0x82
#define ATTINY_REG_PORTC     0x83
#define ATTINY_REG_POWERON   0x85
#define ATTINY_REG_PWM       0x86

#define ATTINY_PA_LCD_LR     0x04   /* BIT(2): horizontal scan left-to-right */

#define ATTINY_PB_LCD_MAIN   0x80   /* BIT(7): main regulator on */

#define ATTINY_PC_LED_EN     0x01   /* BIT(0) */
#define ATTINY_PC_RST_TP_N   0x02   /* BIT(1) */
#define ATTINY_PC_RST_LCD_N  0x04   /* BIT(2) */
#define ATTINY_PC_RST_BRIDGE_N 0x08 /* BIT(3) */

#define ATTINY_PC_DEASSERT_ALL (ATTINY_PC_LED_EN | ATTINY_PC_RST_TP_N | \
                                ATTINY_PC_RST_LCD_N | ATTINY_PC_RST_BRIDGE_N)

#define ATTINY_VERSION_V1    0xDE
#define ATTINY_VERSION_V2    0xC3
