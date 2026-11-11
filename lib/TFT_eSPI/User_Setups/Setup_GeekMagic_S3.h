// GeekMagic S3 - ESP32-S3 N16R8 + 1.54" IPS ST7789 240x240 (no CS pin)
// Pinout from https://github.com/GeekMagicClock/GeekMagic-S3 (lib/TFT_eSPI/User_Setup.h)
#define USER_SETUP_ID 230
#define USER_SETUP_INFO "GeekMagic_S3"

#define ST7789_2_DRIVER
#define TFT_RGB_ORDER TFT_RGB
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define TFT_MISO -1
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   -1
#define TFT_DC    7
#define TFT_RST   6
#define TFT_BL   14
#define TFT_BACKLIGHT_ON LOW   // backlight enable is active-low on this board

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       80000000
#define SPI_READ_FREQUENCY  20000000
