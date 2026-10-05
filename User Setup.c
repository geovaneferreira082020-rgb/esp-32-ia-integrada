// ============================================================================
//  User_Setup.h  —  CYBERDESK (ESP32 DevKit v1 + ILI9341 2.4" + XPT2046 + microSD)
//
//  Onde salvar:
//    Documentos/Arduino/libraries/TFT_eSPI/User_Setup.h   (substitui o arquivo)
//  Obs.: atualizar a biblioteca TFT_eSPI sobrescreve este arquivo. Guarde uma cópia.
//
//  Barramento VSPI compartilhado: MOSI 23 | MISO 19 | SCK 18
//  Chip selects: TFT 5 | TOUCH 14 | SD 13 (o SD_CS é definido no .ino)
// ============================================================================

#define USER_SETUP_INFO "Cyberdesk"

// ---- Driver do display ------------------------------------------------------
#define ILI9341_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Se as cores saírem trocadas (ciano aparecendo vermelho), descomente:
//#define TFT_RGB_ORDER TFT_BGR
// Se o preto aparecer branco / cores "negativas", descomente:
//#define TFT_INVERSION_ON

// ---- Pinos (ESP32 / VSPI) ---------------------------------------------------
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS    5   // Chip select do display
#define TFT_DC    2   // Data/Command (GPIO2 é pino de boot: se o upload falhar, desligue este fio durante a gravação)
#define TFT_RST   4   // Reset do display

#define TOUCH_CS 14   // Chip select do XPT2046 (touch resistivo)

// ---- Fontes carregadas (só o necessário, para economizar flash) -------------
#define LOAD_GLCD     // Fonte 1: 6x8 monoespaçada (usada no terminal)
#define LOAD_FONT2    // Fonte 2: 16 px pequena
#define LOAD_FONT4    // Fonte 4: 26 px (títulos / boot)

// ---- Frequências SPI --------------------------------------------------------
#define SPI_FREQUENCY        40000000   // Se houver artefatos/linhas na tela (fios longos), use 27000000
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000

// ---- OBRIGATÓRIO com barramento compartilhado (TFT + Touch + SD) ------------
// Faz a TFT_eSPI usar SPI.beginTransaction()/endTransaction(), liberando o
// barramento corretamente para o cartão SD entre uma escrita e outra.
#define SUPPORT_TRANSACTIONS
