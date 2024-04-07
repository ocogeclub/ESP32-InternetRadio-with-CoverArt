#include <Arduino.h>

#include <WiFi.h>
#include <Audio.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LovyanGFX.hpp>
#include <UrlEncode.h>

// ST7789 Pins
#define TFT_DC 8
#define TFT_RST 9
#define TFT_MOSI 11 // SDA
#define TFT_SCLK 12
#define TFT_MISO -1
#define TFT_CS -1
// I2S Audio Pins
#define I2S_LRC 2
#define I2S_BCLK 3
#define I2S_DOUT 4

// Image buffer size (kbytes)
#define IMGBUF_SIZE 64

// Globals
const char *ssid = "your-ssid";         // Change this to your WiFi SSID
const char *password = "your-password"; // Change this to your WiFi password
Audio audio;
String streamtitle = "";

// LovyanGFX LCDクラス定義例 | あろしーど [https://aloseed.com/it/lovyangfx-setting-example/#toc54]
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI _bus_instance; // SPIバスのインスタンス
public:
  LGFX(void)
  {
    {                                    // バス制御の設定を行います。
      auto cfg = _bus_instance.config(); // バス設定用の構造体を取得します。
      // SPIバスの設定
      cfg.spi_host = SPI2_HOST;               // ESP32-S3では SPI2_HOST または SPI3_HOST
      cfg.spi_mode = 0;                       // SPI通信モードを設定 (0 ~ 3)
      cfg.freq_write = 40000000;              // 送信時のSPIクロック (最大80MHz, 80MHzを整数で割った値に丸められます)
      cfg.freq_read = 20000000;               // 受信時のSPIクロック
      cfg.dma_channel = SPI_DMA_CH_AUTO;      // 使用するDMAチャンネルを設定 (0=DMA不使用 / 1=1ch / 2=ch / SPI_DMA_CH_AUTO=自動設定)
      cfg.pin_sclk = TFT_SCLK;                // SPIのSCLKピン番号を設定
      cfg.pin_mosi = TFT_MOSI;                // SPIのMOSIピン番号を設定
      cfg.pin_miso = TFT_MISO;                // SPIのMISOピン番号を設定 (-1 = disable)
      cfg.pin_dc = TFT_DC;                    // SPIのD/Cピン番号を設定  (-1 = disable)
      _bus_instance.config(cfg);              // 設定値をバスに反映します。
      _panel_instance.setBus(&_bus_instance); // バスをパネルにセットします。
    }
    {                                      // 表示パネル制御の設定を行います。
      auto cfg = _panel_instance.config(); // 表示パネル設定用の構造体を取得します。

      cfg.pin_cs = -1;         // CSが接続されているピン番号   (-1 = disable)
      cfg.pin_rst = -1;        // RSTが接続されているピン番号  (-1 = disable)
      cfg.pin_busy = -1;       // BUSYが接続されているピン番号 (-1 = disable)
      cfg.panel_width = 240;   // 実際に表示可能な幅
      cfg.panel_height = 240;  // 実際に表示可能な高さ
      cfg.offset_rotation = 3; // 回転方向の値のオフセット 0~7 (4~7は上下反転)
      cfg.invert = true;       // パネルの明暗が反転してしまう場合 trueに設定
      cfg.bus_shared = false;  // SDカードとバスを共有している場合 trueに設定(drawJpgFile等でバス制御を行います)
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance); // 使用するパネルをセットします。
  }
};
static LGFX lcd;

void setup()
{
  Serial.begin(115200);
  // LCD module without CS reset sequence
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW);
  pinMode(TFT_SCLK, OUTPUT);
  digitalWrite(TFT_SCLK, HIGH);
  digitalWrite(TFT_RST, HIGH);
  lcd.init();
  lcd.startWrite();
  lcd.setCursor(0, 0);
  lcd.setFont(&fonts::DejaVu18);
  lcd.setTextColor(TFT_GREENYELLOW);

  // WiFi
  lcd.println("WiFi connecting...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  lcd.println("connected.");

  // I2S audio settings
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(2); // 0...21

  // Play stream
  lcd.println("\nConnecting stream...");
  String streamuri = "http://ais-edge91-dal03.cdnstream.com/2585_64.aac"; // SmoothJazz.com Global AAC 64K
  // String streamuri = "https://kathy.torontocast.com:3060/"; // Asia Dream Radio - J-Pop Powerplay Kawaii
  // String streamuri = "http://216.235.89.171:80/officemix";                // POWERHITZ.COM - THE OFFICEMIX - The Best Soft Rock And Pop
  audio.connecttohost(streamuri.c_str());
}

void loop()
{
  audio.loop();
  delay(1);
}

void lcdText(String text)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.println(text);
}

// iTunes search api
String iTunesSearch(const String keywords)
{
  String query = "https://itunes.apple.com/search?country=jp&media=music&entity=musicTrack&limit=1&lang=ja_jp&term=";
  query = query + urlEncode(keywords);
  HTTPClient http;
  http.begin(query.c_str());
  int httpResponseCode = http.GET();
  String payload = "";
  if (httpResponseCode == HTTP_CODE_OK)
  {
    payload = http.getString();
  }
  else
  {
    Serial.printf("Error code: %d\n", httpResponseCode);
    Serial.println("Cannot access iTunes Search API.");
  }
  http.end();
  return payload;
}

// ESP32でバイナリファイルのダウンロード・アップロード @poruruba [https://qiita.com/poruruba/items/82a683866aef872665a4]
long doHttpGet(String url, uint8_t *p_buffer, unsigned long *p_len)
{
  HTTPClient http;
  Serial.print("[HTTP] GET begin...\n");
  // configure traged server and url
  http.begin(url);
  Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  int httpCode = http.GET();
  unsigned long index = 0;
  // httpCode will be negative on error
  if (httpCode > 0)
  {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);
    // file found at server
    if (httpCode == HTTP_CODE_OK)
    {
      // get tcp stream
      WiFiClient *stream = http.getStreamPtr();
      // get lenght of document (is -1 when Server sends no Content-Length header)
      int len = http.getSize();
      Serial.printf("[HTTP] Content-Length=%d\n", len);
      if (len != -1 && len > *p_len)
      {
        Serial.printf("[HTTP] buffer size over\n");
        http.end();
        return -1;
      }
      // read all data from server
      while (http.connected() && (len > 0 || len == -1))
      {
        // get available data size
        size_t size = stream->available();
        if (size > 0)
        {
          // read up to 128 byte
          if ((index + size) > *p_len)
          {
            Serial.printf("[HTTP] buffer size over\n");
            http.end();
            return -1;
          }
          int c = stream->readBytes(&p_buffer[index], size);

          index += c;
          if (len > 0)
          {
            len -= c;
          }
        }
        delay(1);
      }
    }
    else
    {
      http.end();
      return -1;
    }
  }
  else
  {
    http.end();
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    return -1;
  }
  http.end();
  *p_len = index;
  return 0;
}

// Get artwork url from iTunes search result
String getArtworkUrl(String payload)
{
  JsonDocument doc;
  deserializeJson(doc, payload);
  if (!doc.containsKey("resultCount"))
    return "";
  if (doc["resultCount"] == 0)
    return "";
  String art_url = doc["results"][0]["artworkUrl100"];
  art_url.replace("100x100", "240x240");
  return art_url;
}

// Task display artwork
void task_artwork(void *arg)
{
  String art_url = "";
  String payload = iTunesSearch(streamtitle);
  if (payload.length() > 0)
    art_url = getArtworkUrl(payload);
  if (art_url.length() > 0)
  {
    // Image buffer size
    unsigned long p_len = IMGBUF_SIZE * 1024;
    // Allocate memory on PSRAM
    uint8_t *p_buffer = (uint8_t *)ps_malloc(p_len * sizeof(uint8_t));
    if (doHttpGet(art_url, p_buffer, &p_len) == 0)
    {
      int res = lcd.drawJpg(p_buffer, p_len);
      if (res == 0)
        lcdText(streamtitle);
    }
    else
      lcdText(streamtitle);
    free(p_buffer);
  }
  else
    lcdText(streamtitle);
  vTaskDelete(NULL);
}

// optional
void audio_info(const char *info)
{
  Serial.print("info        ");
  Serial.println(info);
}
void audio_showstation(const char *info)
{
  Serial.print("station     ");
  Serial.println(info);
}
void audio_showstreamtitle(const char *info)
{
  Serial.print("streamtitle ");
  Serial.println(info);
  String tmp = info;
  tmp.trim();
  if (tmp.length() == 0) // Sometimes it gets empty data.
    return;
  streamtitle = tmp;
  // Create task
  xTaskCreateUniversal(task_artwork, "task_artwork", 8192, NULL, 1, NULL, PRO_CPU_NUM);
}
void audio_bitrate(const char *info)
{
  Serial.print("bitrate     ");
  Serial.println(info);
}
void audio_icyurl(const char *info)
{ // homepage
  Serial.print("icyurl      ");
  Serial.println(info);
}
void audio_lasthost(const char *info)
{ // stream URL played
  Serial.print("lasthost    ");
  Serial.println(info);
}