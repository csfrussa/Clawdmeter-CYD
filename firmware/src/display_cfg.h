#pragma once

#include <Arduino_GFX_Library.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// ---- Display resolution ----
#define LCD_WIDTH   320
#define LCD_HEIGHT  240

// ---- ILI9341 display SPI pins (ESP32 CYD — HSPI bus) ----
#define LCD_CS    15
#define LCD_DC     2
#define LCD_RST   GFX_NOT_DEFINED  // not wired on CYD
#define LCD_MOSI  13
#define LCD_MISO  GFX_NOT_DEFINED  // write-only display
#define LCD_SCLK  14
#define LCD_BL    21  // backlight, active HIGH via level shifter

// ---- XPT2046 touch SPI pins (ESP32 CYD — VSPI bus, separate from display) ----
#define TOUCH_CS   33
#define TOUCH_CLK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_IRQ  36

// ---- Touch calibration (landscape, ILI9341 rotation=1) ----
// Adjust if touch is misaligned: raw range is ~200–3900 per axis.
// Default values work for most CYD units in landscape mode.
#define TOUCH_X_MIN  200
#define TOUCH_X_MAX  3800
#define TOUCH_Y_MIN  300
#define TOUCH_Y_MAX  3700

// ---- Physical buttons ----
#define BTN_LEFT   35   // left physical button
#define BTN_RIGHT  34   // right physical button
// BTN_MID = GPIO 0 (boot button), polled in power.cpp

// ---- RGB LED (active LOW, common anode — kept off by default) ----
#define LED_R   4
#define LED_G  16
#define LED_B  17

// ---- Global hardware objects (defined in main.cpp) ----
extern Arduino_DataBus *bus;
extern Arduino_ILI9341 *gfx;
extern XPT2046_Touchscreen touch;
extern SPIClass touchSPI;
