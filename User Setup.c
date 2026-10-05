#define USER_SETUP_INFO "Cyberdesk"

#define ILI9341_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS    5   // Chip select do display
#define TFT_DC    2   // Data/Command (GPIO2 é pino de boot: se o upload falhar, desligue este fio durante a gravação)
#define TFT_RST   4   // Reset do display

#define TOUCH_CS 14   // Chip select do XPT2046 (touch resistivo)

#define LOAD_GLCD     // Fonte 1: 6x8 monoespaçada (usada no terminal)
#define LOAD_FONT2    // Fonte 2: 16 px pequena
#define LOAD_FONT4    // Fonte 4: 26 px (títulos / boot)

#define SPI_FREQUENCY        40000000   // Se houver artefatos/linhas na tela (fios longos), use 27000000
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000

#define SUPPORT_TRANSACTIONS
