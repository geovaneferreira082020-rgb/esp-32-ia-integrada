#include <Arduino.h>

#include <WiFi.h>

#include <WiFiClientSecure.h>

#include <SPI.h>

#include <FS.h>

#include <SD.h>

#include <TFT_eSPI.h>

#include <ArduinoJson.h>

#include <NimBLEDevice.h>

#include <time.h>

#include <string.h>

#include <strings.h>



#define WIFI_SSID "Rede Gemini"

#define WIFI_PASS "12345678"

#define GEMINI_API_KEY "Gemini.3.2"



#define GEMINI_MODEL "gemini-3.2-flash"

#define GEMINI_THINKING_LEVEL "low"

#define GEMINI_MAX_TOKENS 2048

#define SYSTEM_PROMPT \

"Você é a IA embutida no Cyberdesk, um terminal portátil com tela pequena. " \

"Responda no idioma do usuário, de forma direta e concisa (idealmente até 600 caracteres). " \

"Use somente texto simples: sem markdown, sem emojis, sem tabelas. Para listas use hífen."



#define GMT_OFFSET_SEC (-3 * 3600)

#define DST_OFFSET_SEC 0



#define TFT_ROTATION 1

#define TEXT_SIZE 1

#define ENABLE_TOUCH 1



#define SD_CS 13

#define SD_FREQ 10000000

#define LOG_PATH "/cyberdesk_log.txt"

#define CAL_PATH "/touch.cal"



#define KB_NAME_FILTER ""

#define DEBUG_BLE_SCAN 1

#define DEBUG_HID 0



#define USE_ROOT_CA 0

#define MIN_HEAP_FOR_TLS 55000



#if USE_ROOT_CA

static const char GOOGLE_ROOT_CA[] PROGMEM = R"EOF(

-----BEGIN CERTIFICATE-----

COLE_AQUI_O_CERTIFICADO_RAIZ_PEM

-----END CERTIFICATE-----

)EOF";

#endif



#define C_BG TFT_BLACK

#define C_GREEN TFT_GREEN

#define C_CYAN TFT_CYAN

#define C_DIM 0x0320

#define C_WARN TFT_YELLOW

#define C_ERR TFT_RED



static const int SCR_W = 320;

static const int SCR_H = 240;

static const int MARGIN = 3;

static const int SCROLLBAR_W = 4;

static const int STATUS_H = 14;

static const int CHAR_W = 6 * TEXT_SIZE;

static const int LINE_H = 8 * TEXT_SIZE + 2;

static const int TXT_COLS = (SCR_W - 2 * MARGIN - SCROLLBAR_W - 2) / CHAR_W;

static const int INPUT_ROWS = (TEXT_SIZE == 1) ? 3 : 2;

static const int INPUT_Y = SCR_H - INPUT_ROWS * LINE_H - 2;

static const int OUT_Y0 = STATUS_H + 3;

static const int OUT_Y1 = INPUT_Y - 5;

static const int OUT_ROWS = (OUT_Y1 - OUT_Y0) / LINE_H;



#define INPUT_MAX 240

#define RESP_MAX 4096

#define OUT_MAX (RESP_MAX + INPUT_MAX + 16)

#define REQ_MAX 1536

#define MAX_LINES 320



static char g_in[INPUT_MAX + 1];

static size_t g_inLen = 0;

static char g_question[INPUT_MAX + 1];

static char g_resp[RESP_MAX];

static char g_out[OUT_MAX];

static char g_req[REQ_MAX];

static size_t g_qEnd = 0;

static uint16_t g_lineStart[MAX_LINES];

static uint16_t g_lineLen[MAX_LINES];

static int g_lineCount = 0;

static int g_scrollTop = 0;

static bool g_dirtyOut = true;

static bool g_dirtyIn = true;



TFT_eSPI tft = TFT_eSPI();



static bool g_wifiOk = false;

static bool g_sdOk = false;

static bool g_caps = false;

static volatile bool g_btConnected = false;

static volatile bool g_btScanning = false;

static int g_bootY = 0;



struct HidReport { uint8_t d[8]; };

static QueueHandle_t g_hidQ = nullptr;

static uint8_t g_prevKeys[6] = {0};



static const char GEMINI_HOST[] = "generativelanguage.googleapis.com";



static void submitInput();

static void drawOutput();

static void drawInput(bool cursorOn);

static void drawStatus(bool force);

static void drawFrame();



static const char LATIN1_MAP[] =

"AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"

"aaaaaaaceeeeiiiidnooooo/ouuuuyty";



static void foldToAscii(char* s) {

const uint8_t* r = (const uint8_t*)s;

char* w = s;

while (*r) {

uint8_t c = *r;

if (c < 0x80) {

if (c == '\n') { *w++ = '\n'; r++; }

else if (c < 0x20) { if (c == '\t') *w++ = ' '; r++; }

else if (c == '`') { r++; }

else if (c == '*') {

if (r[1] == '*') { r += 2; }

else if (r[1] == ' ' && (w == s || w[-1] == '\n')) { *w++ = '-'; r++; }

else { *w++ = '*'; r++; }

} else { *w++ = (char)c; r++; }

}

else if (c == 0xC3 && r[1]) {

uint8_t k = r[1];

*w++ = (k >= 0x80 && k <= 0xBF) ? LATIN1_MAP[k - 0x80] : '?';

r += 2;

}

else if (c == 0xC2 && r[1]) {

*w++ = (r[1] == 0xA0) ? ' ' : (r[1] == 0xB0 ? 'o' : '?');

r += 2;

}

else if (c == 0xE2 && r[1] && r[2]) {

if (r[1] == 0x80) {

switch (r[2]) {

case 0x93: case 0x94: *w++ = '-'; break;

case 0x98: case 0x99: *w++ = '\''; break;

case 0x9C: case 0x9D: *w++ = '"'; break;

case 0xA2: *w++ = '-'; break;

case 0xA6: *w++ = '.'; *w++ = '.'; *w++ = '.'; break;

default: *w++ = '?'; break;

}

} else { *w++ = '?'; }

r += 3;

}

else {

int n = ((c & 0xF8) == 0xF0) ? 4 : ((c & 0xF0) == 0xE0) ? 3 : ((c & 0xE0) == 0xC0) ? 2 : 1;

while (n-- > 0 && *r) r++;

}

}

*w = 0;

}



static void makeTimestamp(char* buf, size_t cap) {

time_t now = time(nullptr);

if (now > 1700000000) {

struct tm t;

localtime_r(&now, &t);

strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &t);

} else {

snprintf(buf, cap, "uptime %lus", (unsigned long)(millis() / 1000UL));

}

}



static void pushLine(size_t start, size_t len) {

if (g_lineCount < MAX_LINES) {

g_lineStart[g_lineCount] = (uint16_t)start;

g_lineLen[g_lineCount] = (uint16_t)len;

g_lineCount++;

}

}



static void layoutText() {

g_lineCount = 0;

const size_t n = strlen(g_out);

const size_t cols = (size_t)TXT_COLS;

size_t i = 0;

for (;;) {

size_t j = i;

while (j < n && g_out[j] != '\n') j++;

size_t s = i;

if (s == j) pushLine(s, 0);

while (s < j) {

size_t remain = j - s;

if (remain <= cols) { pushLine(s, remain); break; }

size_t k = s + cols;

while (k > s && g_out[k] != ' ') k--;

if (k == s) { pushLine(s, cols); s += cols; }

else { pushLine(s, k - s); s = k + 1; }

}

if (j >= n || g_lineCount >= MAX_LINES) break;

i = j + 1;

}

}



static void showOutput(const char* q, const char* body) {

if (q && q[0]) {

snprintf(g_out, OUT_MAX, "> %s\n\n%s", q, body);

g_qEnd = 2 + strlen(q);

} else {

snprintf(g_out, OUT_MAX, "%s", body);

g_qEnd = 0;

}

g_scrollTop = 0;

layoutText();

g_dirtyOut = true;

}



static void scrollBy(int d) {

int maxTop = g_lineCount - OUT_ROWS;

if (maxTop < 0) maxTop = 0;

int t = g_scrollTop + d;

if (t < 0) t = 0;

if (t > maxTop) t = maxTop;

if (t != g_scrollTop) { g_scrollTop = t; g_dirtyOut = true; }

}



static void drawScrollbar() {

const int x = SCR_W - SCROLLBAR_W;

const int h = OUT_Y1 - OUT_Y0;

tft.fillRect(x, OUT_Y0, SCROLLBAR_W, h, C_BG);

if (g_lineCount <= OUT_ROWS) return;

tft.drawFastVLine(x + SCROLLBAR_W / 2, OUT_Y0, h, C_DIM);

int thumbH = h * OUT_ROWS / g_lineCount;

if (thumbH < 8) thumbH = 8;

int maxTop = g_lineCount - OUT_ROWS;

int thumbY = OUT_Y0 + (h - thumbH) * g_scrollTop / maxTop;

tft.fillRect(x, thumbY, SCROLLBAR_W, thumbH, C_CYAN);

}



static void drawOutput() {

tft.setTextFont(1);

tft.setTextSize(TEXT_SIZE);

char buf[TXT_COLS + 1];

for (int r = 0; r < OUT_ROWS; r++) {

const int y = OUT_Y0 + r * LINE_H;

tft.fillRect(0, y, SCR_W - SCROLLBAR_W - 1, LINE_H, C_BG);

const int idx = g_scrollTop + r;

if (idx >= g_lineCount) continue;

int len = g_lineLen[idx];

if (len > TXT_COLS) len = TXT_COLS;

memcpy(buf, g_out + g_lineStart[idx], len);

buf[len] = 0;

const bool isQuestion = g_lineStart[idx] < g_qEnd;

tft.setTextColor(isQuestion ? C_CYAN : C_GREEN, C_BG);

tft.drawString(buf, MARGIN, y);

}

drawScrollbar();

}



static void drawInput(bool cursorOn) {

tft.setTextFont(1);

tft.setTextSize(TEXT_SIZE);

tft.fillRect(0, INPUT_Y, SCR_W, SCR_H - INPUT_Y, C_BG);



const size_t cols = (size_t)TXT_COLS;

const size_t total = g_inLen + 2;

const size_t cells = total + 1;

const int rowsNeeded = (int)((cells + cols - 1) / cols);

const int firstRow = rowsNeeded > INPUT_ROWS ? rowsNeeded - INPUT_ROWS : 0;



char line[TXT_COLS + 1];

for (int r = 0; r < INPUT_ROWS && firstRow + r < rowsNeeded; r++) {

size_t base = (size_t)(firstRow + r) * cols;

int n = 0;

for (size_t p = base; p < base + cols && p < cells; p++) {

char ch;

if (p == 0) ch = '>';

else if (p == 1) ch = ' ';

else if (p < total) ch = g_in[p - 2];

else ch = cursorOn ? '_' : ' ';

line[n++] = ch;

}

line[n] = 0;

const int y = INPUT_Y + r * LINE_H;

tft.setTextColor(C_GREEN, C_BG);

tft.drawString(line, MARGIN, y);

if (firstRow + r == 0) {

tft.setTextColor(C_CYAN, C_BG);

tft.drawString(">", MARGIN, y);

}

}

}



static int drawTag(int x, const char* label, const char* val, uint16_t col) {

tft.setTextColor(C_CYAN, C_BG);

x += tft.drawString(label, x, 3);

x += tft.drawString(":", x, 3);

tft.setTextColor(col, C_BG);

x += tft.drawString(val, x, 3);

return x;

}



static void drawStatus(bool force) {

static int8_t sW = -1, sB = -1, sS = -1;

static uint32_t sHeapK = 0xFFFFFFFF;



const int8_t w = g_wifiOk ? 1 : 0;

const int8_t b = g_btConnected ? 2 : (g_btScanning ? 1 : 0);

const int8_t s = g_sdOk ? 1 : 0;

const uint32_t hk = ESP.getFreeHeap() / 1024;

if (!force && w == sW && b == sB && s == sS && hk == sHeapK) return;

sW = w; sB = b; sS = s; sHeapK = hk;



tft.setTextFont(1);

tft.setTextSize(1);

tft.fillRect(0, 0, SCR_W, STATUS_H - 1, C_BG);

int x = 3;

x = drawTag(x, "WIFI", w ? "OK" : "...", w ? C_GREEN : C_WARN);

x = drawTag(x + 8, "BT", b == 2 ? "OK" : (b == 1 ? "BUSCA" : "OFF"), b == 2 ? C_GREEN : (b == 1 ? C_WARN : C_ERR));

x = drawTag(x + 8, "SD", s ? "OK" : "ERRO", s ? C_GREEN : C_ERR);



char hb[16];

snprintf(hb, sizeof(hb), "RAM:%luK", (unsigned long)hk);

tft.setTextColor(C_DIM, C_BG);

tft.drawRightString(hb, SCR_W - 3, 3, 1);

}



static void drawFrame() {

tft.fillScreen(C_BG);

tft.drawFastHLine(0, STATUS_H - 1, SCR_W, C_CYAN);

tft.drawFastHLine(0, INPUT_Y - 3, SCR_W, C_DIM);

drawStatus(true);

g_dirtyOut = true;

g_dirtyIn = true;

}



static void bootLine(const char* label, const char* status, uint16_t color) {

tft.setTextFont(1);

tft.setTextSize(1);

tft.setTextColor(C_CYAN, C_BG);

tft.drawString(label, 8, g_bootY);

tft.setTextColor(color, C_BG);

tft.drawRightString(status, SCR_W - 8, g_bootY, 1);

Serial.printf("[BOOT] %s: %s\n", label, status);

g_bootY += 12;

}



static bool sdInit() {

pinMode(SD_CS, OUTPUT);

digitalWrite(SD_CS, HIGH);

SD.end();

return SD.begin(SD_CS, SPI, SD_FREQ) && SD.cardType() != CARD_NONE;

}



static bool logInteraction(const char* q, const char* a) {

if (!g_sdOk) return false;

File f = SD.open(LOG_PATH, FILE_APPEND);

if (!f) return false;

char ts[32];

makeTimestamp(ts, sizeof(ts));

f.printf("[%s]\n", ts);

f.print("USER: "); f.println(q);

f.print("IA : "); f.println(a);

f.println("----------------------------------------");

f.close();

return true;

}



#if ENABLE_TOUCH

static uint16_t g_calData[5];



static bool loadCal() {

if (!g_sdOk || !SD.exists(CAL_PATH)) return false;

File f = SD.open(CAL_PATH, FILE_READ);

if (!f) return false;

bool ok = (f.size() == sizeof(g_calData)) &&

(f.read((uint8_t*)g_calData, sizeof(g_calData)) == (int)sizeof(g_calData));

f.close();

return ok;

}



static void saveCal() {

if (!g_sdOk) return;

File f = SD.open(CAL_PATH, FILE_WRITE);

if (f) { f.write((const uint8_t*)g_calData, sizeof(g_calData)); f.close(); }

}



static void runCalibration() {

tft.fillScreen(C_BG);

tft.setTextColor(C_CYAN, C_BG);

tft.drawCentreString("CALIBRACAO DO TOUCH", SCR_W / 2, 90, 2);

tft.drawCentreString("Toque nas setas dos 4 cantos", SCR_W / 2, 115, 2);

delay(1500);

tft.fillScreen(C_BG);

tft.calibrateTouch(g_calData, C_GREEN, C_BG, 15);

saveCal();

tft.setTouch(g_calData);

}



static void handleTouch() {

static int lastY = -1;

uint16_t x, y;

if (tft.getTouch(&x, &y, 500)) {

if ((int)y >= OUT_Y0 && (int)y < OUT_Y1) {

if (lastY < 0) {

lastY = (int)y;

} else {

int dy = (int)y - lastY;

if (dy >= LINE_H || dy <= -LINE_H) {

scrollBy(-dy / LINE_H);

lastY = (int)y;

}

}

}

} else {

lastY = -1;

}

}

#endif



static int readLine(WiFiClientSecure& c, char* buf, size_t cap, uint32_t timeoutMs) {

size_t n = 0;

const uint32_t t0 = millis();

while (millis() - t0 < timeoutMs) {

if (c.available() > 0) {

int ch = c.read();

if (ch < 0) continue;

if (ch == '\n') { buf[n] = 0; return (int)n; }

if (ch != '\r' && n < cap - 1) buf[n++] = (char)ch;

} else if (!c.connected()) {

break;

} else {

delay(2);

}

}

buf[n] = 0;

return -1;

}



static bool askGemini(const char* prompt) {

g_resp[0] = 0;



if (WiFi.status() != WL_CONNECTED) {

snprintf(g_resp, RESP_MAX, "[ERRO] Sem Wi-Fi.");

return false;

}

#if USE_ROOT_CA

if (time(nullptr) < 1700000000) {

snprintf(g_resp, RESP_MAX, "[ERRO] Hora ainda nao sincronizada (NTP). Tente em alguns segundos.");

return false;

}

#endif

if (ESP.getFreeHeap() < MIN_HEAP_FOR_TLS) {

snprintf(g_resp, RESP_MAX, "[ERRO] Heap baixo (%u B livres). Tente de novo.", (unsigned)ESP.getFreeHeap());

return false;

}



size_t bodyLen = 0;

{

JsonDocument req;

req["systemInstruction"]["parts"][0]["text"] = SYSTEM_PROMPT;

req["contents"][0]["role"] = "user";

req["contents"][0]["parts"][0]["text"] = prompt;

req["generationConfig"]["maxOutputTokens"] = GEMINI_MAX_TOKENS;

if (strlen(GEMINI_THINKING_LEVEL) > 0) {

req["generationConfig"]["thinkingConfig"]["thinkingLevel"] = GEMINI_THINKING_LEVEL;

}

bodyLen = serializeJson(req, g_req, sizeof(g_req));

}

if (bodyLen == 0 || bodyLen >= sizeof(g_req) - 1) {

snprintf(g_resp, RESP_MAX, "[ERRO] Pergunta grande demais para o buffer.");

return false;

}



WiFiClientSecure client;

#if USE_ROOT_CA

client.setCACert(GOOGLE_ROOT_CA);

#else

client.setInsecure();

#endif

client.setTimeout(20);

if (!client.connect(GEMINI_HOST, 443)) {

char e[96] = {0};

client.lastError(e, sizeof(e));

snprintf(g_resp, RESP_MAX, "[ERRO] Falha ao conectar (TLS): %s", e);

return false;

}



char hdr[352];

int hl = snprintf(hdr, sizeof(hdr),

"POST /v1beta/models/%s:generateContent HTTP/1.0\r\n"

"Host: %s\r\n"

"x-goog-api-key: %s\r\n"

"Content-Type: application/json\r\n"

"Content-Length: %u\r\n"

"Connection: close\r\n\r\n",

GEMINI_MODEL, GEMINI_HOST, GEMINI_API_KEY, (unsigned)bodyLen);

if (hl <= 0 || hl >= (int)sizeof(hdr)) {

snprintf(g_resp, RESP_MAX, "[ERRO] Cabecalho HTTP grande demais.");

return false;

}

client.write((const uint8_t*)hdr, hl);

client.write((const uint8_t*)g_req, bodyLen);



char line[128];

if (readLine(client, line, sizeof(line), 25000) < 0) {

snprintf(g_resp, RESP_MAX, "[ERRO] Timeout aguardando o Gemini.");

return false;

}

int http = 0;

if (strncmp(line, "HTTP/", 5) == 0) {

const char* sp = strchr(line, ' ');

if (sp) http = atoi(sp + 1);

}

bool chunked = false;

for (;;) {

int n = readLine(client, line, sizeof(line), 10000);

if (n < 0) {

snprintf(g_resp, RESP_MAX, "[ERRO] Cabecalhos incompletos.");

return false;

}

if (n == 0) break;

if (strncasecmp(line, "transfer-encoding:", 18) == 0 && strstr(line, "chunked")) chunked = true;

}

if (chunked) {

snprintf(g_resp, RESP_MAX, "[ERRO] Servidor respondeu em 'chunked' (ignorou HTTP/1.0).");

return false;

}



JsonDocument filter;

filter["candidates"][0]["content"]["parts"][0]["text"] = true;

filter["candidates"][0]["finishReason"] = true;

filter["promptFeedback"]["blockReason"] = true;

filter["error"]["message"] = true;



JsonDocument doc;

DeserializationError je = deserializeJson(doc, client, DeserializationOption::Filter(filter));

client.stop();



if (je) {

snprintf(g_resp, RESP_MAX, "[ERRO] JSON invalido (HTTP %d): %s", http, je.c_str());

return false;

}

if (http != 200) {

const char* m = doc["error"]["message"] | "sem detalhes";

snprintf(g_resp, RESP_MAX, "[ERRO] HTTP %d: %s", http, m);

return false;

}



size_t used = 0;

bool truncated = false;

JsonArray parts = doc["candidates"][0]["content"]["parts"].as<JsonArray>();

for (JsonVariant p : parts) {

const char* t = p["text"];

if (!t) continue;

size_t l = strlen(t);

const size_t room = RESP_MAX - 48 - used;

if (l > room) { l = room; truncated = true; }

memcpy(g_resp + used, t, l);

used += l;

if (truncated) break;

}

g_resp[used] = 0;



if (used == 0) {

const char* br = doc["promptFeedback"]["blockReason"];

const char* fr = doc["candidates"][0]["finishReason"];

if (br) snprintf(g_resp, RESP_MAX, "[BLOQUEADO] Motivo: %s", br);

else if (fr) snprintf(g_resp, RESP_MAX, "[VAZIO] finishReason: %s (aumente GEMINI_MAX_TOKENS?)", fr);

else snprintf(g_resp, RESP_MAX, "[VAZIO] Resposta sem texto.");

return false;

}



const char* fr = doc["candidates"][0]["finishReason"];

if (truncated || (fr && strcmp(fr, "MAX_TOKENS") == 0)) {

strncat(g_resp, "\n[resposta truncada]", RESP_MAX - strlen(g_resp) - 1);

}

return true;

}



static NimBLEClient* g_bleClient = nullptr;

static NimBLEAdvertisedDevice* g_kbDev = nullptr;



static void hidNotifyCb(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {

#if DEBUG_HID

Serial.printf("[HID] len=%u:", (unsigned)len);

for (size_t i = 0; i < len; i++) Serial.printf(" %02X", data[i]);

Serial.println();

#endif

if (len != 8) return;

HidReport r;

memcpy(r.d, data, 8);

xQueueSend(g_hidQ, &r, 0);

}



class KbScanCb : public NimBLEAdvertisedDeviceCallbacks {

void onResult(NimBLEAdvertisedDevice* dev) override {

const bool isHid = dev->isAdvertisingService(NimBLEUUID((uint16_t)0x1812)) ||

(dev->haveAppearance() && dev->getAppearance() == 0x03C1);

#if DEBUG_BLE_SCAN

Serial.printf("[BLE] \"%s\" %s HID=%d RSSI=%d\n",

dev->getName().c_str(), dev->getAddress().toString().c_str(), isHid ? 1 : 0, dev->getRSSI());

#endif

if (!isHid) return;

if (strlen(KB_NAME_FILTER) > 0 && dev->getName().find(KB_NAME_FILTER) == std::string::npos) return;

g_kbDev = dev;

NimBLEDevice::getScan()->stop();

}

};



class KbClientCb : public NimBLEClientCallbacks {

void onConnect(NimBLEClient* c) override {}

void onDisconnect(NimBLEClient* c) override {

g_btConnected = false;

Serial.println("[BLE] Teclado desconectado.");

}

};



static KbScanCb g_scanCb;

static KbClientCb g_clientCb;



static bool connectKeyboard(NimBLEAdvertisedDevice* dev) {

if (!g_bleClient) {

g_bleClient = NimBLEDevice::createClient();

g_bleClient->setClientCallbacks(&g_clientCb, false);

g_bleClient->setConnectionParams(12, 12, 0, 100);

g_bleClient->setConnectTimeout(8);

}

Serial.printf("[BLE] Conectando a %s ...\n", dev->getAddress().toString().c_str());

if (!g_bleClient->connect(dev)) {

Serial.println("[BLE] Falha ao conectar.");

return false;

}

if (!g_bleClient->secureConnection()) {

Serial.println("[BLE] Aviso: pareamento nao concluiu; tentando mesmo assim.");

}



NimBLERemoteService* hid = g_bleClient->getService(NimBLEUUID((uint16_t)0x1812));

if (!hid) {

Serial.println("[BLE] Servico HID (0x1812) nao encontrado.");

g_bleClient->disconnect();

return false;

}



std::vector<NimBLERemoteCharacteristic*>* chars = hid->getCharacteristics(true);

int subs = 0;

for (auto c : *chars) {

if (c->getUUID() == NimBLEUUID((uint16_t)0x2A4D) && c->canNotify()) {

if (c->subscribe(true, hidNotifyCb)) subs++;

}

}

if (subs == 0) {

for (auto c : *chars) {

if (c->getUUID() == NimBLEUUID((uint16_t)0x2A22) && c->canNotify()) {

if (c->subscribe(true, hidNotifyCb)) subs++;

}

}

}

if (subs == 0) {

Serial.println("[BLE] Nenhum report HID assinado.");

g_bleClient->disconnect();

return false;

}

Serial.printf("[BLE] Teclado pronto (%d report(s) assinado(s)).\n", subs);

memset(g_prevKeys, 0, sizeof(g_prevKeys));

return true;

}



static void bleTask(void* arg) {

NimBLEScan* scan = NimBLEDevice::getScan();

scan->setAdvertisedDeviceCallbacks(&g_scanCb, false);

scan->setActiveScan(true);

scan->setInterval(97);

scan->setWindow(37);

for (;;) {

if (g_btConnected) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }

g_kbDev = nullptr;

g_btScanning = true;

scan->clearResults();

scan->start(5, false);

g_btScanning = false;

if (g_kbDev && connectKeyboard(g_kbDev)) g_btConnected = true;

else vTaskDelay(pdMS_TO_TICKS(2000));

}

}



static char hidToChar(uint8_t k, bool shift) {

if (k >= 0x04 && k <= 0x1D) { char c = 'a' + (k - 0x04); return shift ? (char)(c - 32) : c; }

if (k >= 0x1E && k <= 0x27) {

static const char n[] = "1234567890";

static const char s[] = "!@#$%^&*()";

return shift ? s[k - 0x1E] : n[k - 0x1E];

}

if (k == 0x2C) return ' ';

if (k >= 0x2D && k <= 0x38) {

static const char n[] = "-=[]\\#;'`,./";

static const char s[] = "_+{}|~:\"~<>?";

return shift ? s[k - 0x2D] : n[k - 0x2D];

}

return 0;

}



static void handleCommand(const char* cmd) {

if (!strcmp(cmd, "/help")) {

showOutput(cmd,

"Comandos:\n"

"/help - esta ajuda\n"

"/clear - limpa a tela\n"

"/cal - recalibra o touch\n"

"/info - memoria e rede\n\n"

"Setas, PgUp/PgDn, Home/End rolam o texto.\n"

"Esc limpa a linha. Arraste o dedo na area de texto para rolar.");

} else if (!strcmp(cmd, "/clear")) {

g_out[0] = 0; g_qEnd = 0; g_scrollTop = 0;

layoutText();

g_dirtyOut = true;

} else if (!strcmp(cmd, "/info")) {

snprintf(g_resp, RESP_MAX,

"Heap livre : %u B\nMaior bloco: %u B\nWi-Fi: %s IP: %s\nBT: %s\nSD: %s",

(unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),

g_wifiOk ? "OK" : "OFF", WiFi.localIP().toString().c_str(),

g_btConnected ? "teclado conectado" : "procurando", g_sdOk ? "OK" : "ERRO");

showOutput(cmd, g_resp);

} else if (!strcmp(cmd, "/cal")) {

#if ENABLE_TOUCH

if (g_sdOk) SD.remove(CAL_PATH);

runCalibration();

drawFrame();

showOutput(cmd, "Touch calibrado.");

#else

showOutput(cmd, "Touch desativado (ENABLE_TOUCH = 0).");

#endif

} else {

showOutput(cmd, "Comando desconhecido. Use /help");

}

}



static void submitInput() {

char* q = g_in;

while (*q == ' ') q++;

size_t l = strlen(q);

while (l > 0 && q[l - 1] == ' ') l--;

if (l == 0) { g_inLen = 0; g_in[0] = 0; g_dirtyIn = true; return; }



memcpy(g_question, q, l);

g_question[l] = 0;

g_inLen = 0; g_in[0] = 0;

g_dirtyIn = true;



if (g_question[0] == '/') { handleCommand(g_question); return; }



showOutput(g_question, "[ transmitindo para o Gemini... aguarde ]");

drawOutput(); g_dirtyOut = false;

drawInput(true); g_dirtyIn = false;



const bool ok = askGemini(g_question);

if (ok) {

if (!logInteraction(g_question, g_resp)) {

Serial.println("[SD] Falha ao gravar o log.");

g_sdOk = false;

}

}

foldToAscii(g_resp);

showOutput(g_question, g_resp);

}



static void onKey(uint8_t k, bool shift) {

switch (k) {

case 0x28: case 0x58: submitInput(); return;

case 0x2A: if (g_inLen > 0) { g_in[--g_inLen] = 0; g_dirtyIn = true; } return;

case 0x29: g_inLen = 0; g_in[0] = 0; g_dirtyIn = true; return;

case 0x39: g_caps = !g_caps; return;

case 0x52: scrollBy(-1); return;

case 0x51: scrollBy(+1); return;

case 0x4B: scrollBy(-(OUT_ROWS - 1)); return;

case 0x4E: scrollBy(OUT_ROWS - 1); return;

case 0x4A: scrollBy(-9999); return;

case 0x4D: scrollBy(+9999); return;

default: break;

}

const bool isLetter = (k >= 0x04 && k <= 0x1D);

const char c = hidToChar(k, isLetter ? (shift != g_caps) : shift);

if (c && g_inLen < INPUT_MAX) {

g_in[g_inLen++] = c;

g_in[g_inLen] = 0;

g_dirtyIn = true;

}

}



static void processHidReport(const uint8_t* d) {

const bool shift = (d[0] & 0x22) != 0;

for (int i = 2; i < 8; i++) {

const uint8_t k = d[i];

if (k < 0x04) continue;

bool wasDown = false;

for (int j = 0; j < 6; j++) if (g_prevKeys[j] == k) { wasDown = true; break; }

if (!wasDown) onKey(k, shift);

}

memcpy(g_prevKeys, d + 2, 6);

}



static void housekeeping() {

const bool up = (WiFi.status() == WL_CONNECTED);

if (up && !g_wifiOk) {

configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.google.com");

Serial.println("[WiFi] Conectado, sincronizando hora (NTP).");

}

g_wifiOk = up;



static uint32_t tRetry = 0;

if (!up && millis() - tRetry > 20000) { tRetry = millis(); WiFi.reconnect(); }



static uint32_t tSd = 0;

if (!g_sdOk && millis() - tSd > 15000) { tSd = millis(); g_sdOk = sdInit(); }

}



void setup() {

Serial.begin(115200);



pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);

#ifdef TOUCH_CS

pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);

#endif

pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);



g_hidQ = xQueueCreate(16, sizeof(HidReport));



tft.init();

tft.setRotation(TFT_ROTATION);

tft.fillScreen(C_BG);

tft.setTextColor(C_CYAN, C_BG);

tft.drawCentreString("CYBERDESK", SCR_W / 2, 6, 4);

tft.setTextColor(C_GREEN, C_BG);

tft.drawCentreString("// gemini link terminal v1.0 //", SCR_W / 2, 36, 2);

tft.drawFastHLine(8, 58, SCR_W - 16, C_DIM);

g_bootY = 68;

bootLine("TFT ILI9341", "OK", C_GREEN);



g_sdOk = sdInit();

bootLine("MICROSD", g_sdOk ? "OK" : "FALHA", g_sdOk ? C_GREEN : C_ERR);



#if ENABLE_TOUCH

if (loadCal()) {

tft.setTouch(g_calData);

bootLine("TOUCH", "CALIBRACAO CARREGADA", C_GREEN);

} else {

bootLine("TOUCH", "CALIBRAR...", C_WARN);

delay(600);

runCalibration();

tft.fillScreen(C_BG);

g_bootY = 8;

bootLine("TOUCH", "CALIBRADO", C_GREEN);

}

#endif



WiFi.mode(WIFI_STA);

WiFi.persistent(false);

WiFi.setAutoReconnect(true);

WiFi.begin(WIFI_SSID, WIFI_PASS);



NimBLEDevice::init("Cyberdesk");

NimBLEDevice::setSecurityAuth(true, false, true);

NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

xTaskCreatePinnedToCore(bleTask, "ble", 6144, nullptr, 1, nullptr, 0);

bootLine("BLUETOOTH LE", "BUSCANDO TECLADO", C_WARN);



const uint32_t t0 = millis();

while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(200);

g_wifiOk = (WiFi.status() == WL_CONNECTED);

if (g_wifiOk) configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.google.com");

bootLine("WIFI", g_wifiOk ? "OK" : "SEM CONEXAO (tentando em 2o plano)", g_wifiOk ? C_GREEN : C_WARN);



char hb[40];

snprintf(hb, sizeof(hb), "%u KB livres", (unsigned)(ESP.getFreeHeap() / 1024));

bootLine("MEMORIA", hb, C_CYAN);

delay(900);



drawFrame();

showOutput("", "CYBERDESK ONLINE\n\nDigite uma pergunta e pressione ENTER.\nUse /help para ver os comandos.");

}



void loop() {

const uint32_t now = millis();



HidReport r;

while (xQueueReceive(g_hidQ, &r, 0) == pdTRUE) processHidReport(r.d);



#if ENABLE_TOUCH

static uint32_t tTouch = 0;

if (now - tTouch >= 25) { tTouch = now; handleTouch(); }

#endif



static uint32_t tBlink = 0;

static bool cursorOn = true;

if (now - tBlink >= 500) { tBlink = now; cursorOn = !cursorOn; g_dirtyIn = true; }



if (g_dirtyOut) { drawOutput(); g_dirtyOut = false; }

if (g_dirtyIn) { drawInput(cursorOn); g_dirtyIn = false; }



static uint32_t tSlow = 0;

if (now - tSlow >= 1000) { tSlow = now; housekeeping(); drawStatus(false); }



delay(5);

} 
