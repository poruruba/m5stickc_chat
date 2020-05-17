#include <M5StickC.h>
#include <driver/i2s.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <base64.hpp>

HTTPClient http;
const char *host = "【中継サーバのURL】/speech";

const char* wifi_ssid = "【WiFiアクセスポイントのSSID】";
const char* wifi_password = "【WiFiアクセスポイントのパスワード】";

#define htonl(x) ( ((x)<<24 & 0xFF000000UL) | \
                   ((x)<< 8 & 0x00FF0000UL) | \
                   ((x)>> 8 & 0x0000FF00UL) | \
                   ((x)>>24 & 0x000000FFUL) )
 
#define PIN_CLK       (0)           // I2S Clock PIN
#define PIN_DATA      (34)          // I2S Data PIN
#define SAMPLING_RATE (8192)       // サンプリングレート(44100, 22050, 16384, more...)
#define BUFFER_LEN    (1024)        // バッファサイズ
#define SAMPLEING_SEC (4)           // 最大サンプリング時間(秒)
#define STORAGE_LEN   (SAMPLING_RATE * SAMPLEING_SEC)      // 本体保存容量
 
#define WAVE_EXPORT   (0)           // WAVEファイルに出力するか
#define BLANK_LINE  "            "

uint8_t soundBuffer[BUFFER_LEN];    // DMA転送バッファ
uint8_t soundStorage[STORAGE_LEN];  // サウンドデータ保存領域
char httpBuffer[(STORAGE_LEN + 2)/ 3 * 4];

bool recFlag = false;               // 録音状態
int recPos = 0;                     // 録音の長さ

int http_post(uint8_t *p_inout_buffer, int in_length);

// 再生をする
void i2sPlay(){
  // 再生設定
  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
    .sample_rate          = SAMPLING_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 2,
    .dma_buf_len          = BUFFER_LEN,
    .use_apll             = false,
    .tx_desc_auto_clear   = true,
    .fixed_mclk           = 0,
  };
 
  // 再生設定実施
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, NULL);
  i2s_zero_dma_buffer(I2S_NUM_0);
 
  // 再生
  size_t transBytes;
  size_t playPos = 0;
  while( playPos < recPos ){
    for( int i = 0 ; i < BUFFER_LEN ; i+=2 ){
      soundBuffer[i] = 0;                         // 下位8ビットは無視される
      soundBuffer[i+1] = soundStorage[playPos];   // 上位8ビットにuint8_tのデータを入れる
      playPos++;
    }

    // データ転送
    i2s_write(I2S_NUM_0, (char*)soundBuffer, BUFFER_LEN, &transBytes, (100 / portTICK_RATE_MS));
  }
 
  // 後始末
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_0);
}

// 録音をする
void i2sRecord() {
  // 録音用設定
  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate          = SAMPLING_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_ALL_RIGHT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 2,
    .dma_buf_len          = BUFFER_LEN,
    .use_apll             = false,
    .tx_desc_auto_clear   = true,
    .fixed_mclk           = 0,
  };
 
  // PIN設定
  i2s_pin_config_t pin_config;
  pin_config.bck_io_num   = I2S_PIN_NO_CHANGE;
  pin_config.ws_io_num    = PIN_CLK;
  pin_config.data_out_num = I2S_PIN_NO_CHANGE;
  pin_config.data_in_num  = PIN_DATA;
 
  // 録音設定実施
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, SAMPLING_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
 
  // 録音開始
  recFlag = true;
  xTaskCreatePinnedToCore(i2sRecordTask, "i2sRecordTask", 2048, NULL, 1, NULL, 1);
}

// 録音用タスク
void i2sRecordTask(void* arg)
{
  // 初期化
  recPos = 0;
  memset(soundStorage, 0, sizeof(soundStorage));

  vTaskDelay(500); //delay(portMAX_DELAY);

  // LED On
  digitalWrite(GPIO_NUM_10, LOW );
 
  // 録音処理
  while (recFlag) {
    size_t transBytes;
 
    // I2Sからデータ取得
    i2s_read(I2S_NUM_0, (char*)soundBuffer, BUFFER_LEN, &transBytes, (100 / portTICK_RATE_MS));
 
    // int16_t(12bit精度)をuint8_tに変換
    for (int i = 0 ; i < transBytes ; i += 2 ) {
      if ( recPos < STORAGE_LEN ) {
        int16_t* val = (int16_t*)&soundBuffer[i];
        soundStorage[recPos] = ( *val + 32768 ) / 256;
        recPos++;
        if( recPos >= sizeof(soundStorage) ){
          recFlag = false;
          break;
        }
      }
    }
    Serial.printf("transBytes=%d, recPos=%d\n", transBytes, recPos);
    vTaskDelay(1 / portTICK_RATE_MS);
  }

  // LED Off
  digitalWrite(GPIO_NUM_10, HIGH);

  i2s_driver_uninstall(I2S_NUM_0);

  if( recPos > 0 ){
    int ret = http_post(soundStorage, recPos);
    if( ret > 0 ){
      recPos = ret;
      i2sPlay();
    }
  }
 
  // タスク削除
  vTaskDelete(NULL);
}

int http_post(uint8_t *p_inout_buffer, int in_length){
  strcpy(httpBuffer, "{\"message\": \"");
  
  encode_base64(p_inout_buffer, in_length, (unsigned char*)&httpBuffer[strlen(httpBuffer)]);
  strcat((char*)httpBuffer, "\"}");

  Serial.println("HTTP Post");
  http.begin(host);
  http.addHeader("Content-Type", "application/json");
  int status_code = http.POST((uint8_t*)httpBuffer, strlen(httpBuffer));
  if( status_code != HTTP_CODE_OK ){
    Serial.println("Status is not 200");
    http.end();
    return -1;
  }
  int len = http.getSize();
  WiFiClient * stream = http.getStreamPtr();
  int ptr = 0;
  while(http.connected() && (len > 0 || len == -1)) {
    size_t size = stream->available();
    if(size) {
      if(size > (sizeof(httpBuffer) - ptr) ){
        Serial.println("receive overflow");
        http.end();
        return -1;
      }
      int c = stream->readBytes(&httpBuffer[ptr], size);
      ptr += c;
      if(len > 0)
        len -= c;
    }
    delay(1);
  }
  httpBuffer[ptr] = '\0';
  http.end();

  return decode_base64((unsigned char*)httpBuffer, p_inout_buffer);
}

void wifi_connect(void){
  Serial.print("WiFi Connenting");
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected : ");
  Serial.println(WiFi.localIP());
}

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.println("[M5StickC]");

  pinMode(GPIO_NUM_10, OUTPUT);
  digitalWrite(GPIO_NUM_10, HIGH);

  i2sPlay();

  wifi_connect();

  M5.Lcd.println("Sound Recorder");
  M5.Lcd.println("BtnA Record");
  M5.Lcd.println("BtnB Play");
}
 
void loop() {
  M5.update();
 
  if ( M5.BtnA.wasPressed() ) {
    // 録音スタート
    M5.Lcd.setCursor(0, 36);
    M5.Lcd.println("REC...");
    Serial.println("Record Start");
    i2sRecord();
  } else if ( M5.BtnA.wasReleased() ) {
    // 録音ストップ
    M5.Lcd.setCursor(0, 36);
    M5.Lcd.println(BLANK_LINE);
    recFlag = false;
    delay(100); // 録音終了まで待つ
    Serial.println("Record Stop");

    // WAVEファイルをシリアルに出力
    if ( WAVE_EXPORT ) {
      Serial.printf("52494646");                        // RIFFヘッダ
      Serial.printf("%08lx", htonl(recPos + 44 - 8));   // 総データサイズ+44(チャンクサイズ)-8(ヘッダサイズ)
      Serial.printf("57415645");                        // WAVEヘッダ
      Serial.printf("666D7420");                        // フォーマットチャンク
      Serial.printf("10000000");                        // フォーマットサイズ
      Serial.printf("0100");                            // フォーマットコード
      Serial.printf("0100");                            // チャンネル数
      Serial.printf("%08lx", htonl(SAMPLING_RATE));     // サンプリングレート
      Serial.printf("%08lx", htonl(SAMPLING_RATE));     // バイト／秒
      Serial.printf("0100");                            // ブロック境界
      Serial.printf("0800");                            // ビット／サンプル
      Serial.printf("64617461");                        // dataチャンク
      Serial.printf("%08lx", htonl(recPos));            // 総データサイズ
 
      for (int n = 0; n <= recPos; n++) {
        Serial.printf("%02x", soundStorage[n]);
      }
      Serial.printf("\n");
    }    
   } else if ( M5.BtnB.wasReleased() ) {
    // 再生スタート
    M5.Lcd.setCursor(0, 36);
    M5.Lcd.println("Play...");
    Serial.println("Play Start");
    i2sPlay();
    M5.Lcd.setCursor(0, 36);
    M5.Lcd.println(BLANK_LINE);
    Serial.println("Play Stop");
  }
 
  delay(10);
}
