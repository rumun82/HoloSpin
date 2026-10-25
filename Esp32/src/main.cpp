#include <FastLED.h>
#include <ESP32Servo.h>

#define LedStrip0Pin 4
#define LedStrip1Pin 5
#define HallSensorDigitalPin 2
#define Brightness 64
#define LedAmount 72
#define ImageColums 64
#define EscPin 15

const int minDutyCycle = (1000*16384) / 20000;
// 1000 is ur minimum pulse time of 1000 microseconds
// 16384 is the resolution (2^14)
// 20000 is for 20000 microseconds, which is the cycle length.

const int midDutyCycle = (1200*16384) / 20000; // here's a low duty cycle for testing
const int maxDutyCycle = (2000*16384) / 20000;

// Servo ESC;
CRGB LedStrip0[LedAmount];
CRGB LedStrip1[LedAmount];

uint8_t ImgMain[ImageColums][LedAmount][3];
uint8_t ImgBuffer[ImageColums][LedAmount][3];

uint8_t (* volatile ShowImg)[LedAmount][3] = ImgMain;
uint8_t (*RxBuffer)[LedAmount][3] = ImgBuffer;

portMUX_TYPE Mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t LastPass = 0;
volatile uint32_t AngleScale = 0;

TaskHandle_t LedTaskHandle;

void IRAM_ATTR HallInterrupt(){
  uint32_t Now = micros();
  portENTER_CRITICAL_ISR(&Mux);
  if (LastPass != 0){
    uint32_t P = Now - LastPass;
    if (P > 1000){
      AngleScale = (uint32_t)((1ULL << 32) / P);
      LastPass = Now;
    }
  } else {
    LastPass = Now;
  }
  portEXIT_CRITICAL_ISR(&Mux);
};

void LedTask(void *param){
  while(true){
    uint32_t AngleScaleLocal, LastPassLocal;
    portENTER_CRITICAL_ISR(&Mux);
    AngleScaleLocal = AngleScale;
    LastPassLocal = LastPass;
    portEXIT_CRITICAL_ISR(&Mux);

    if(AngleScaleLocal == 0){
      fill_solid(LedStrip0, LedAmount, CRGB::Black);
      fill_solid(LedStrip1, LedAmount, CRGB::Black);
      LedStrip0[0] = CRGB::Red;
      LedStrip1[0] = CRGB::Red;
    } else {
      uint8_t (*Img)[LedAmount][3] = ShowImg;

      uint32_t SinceLastPass = micros() - LastPassLocal;
      uint16_t LedStrip0Angle = ((uint64_t)SinceLastPass * AngleScaleLocal) >> 16;
      uint16_t LedStrip1Angle = LedStrip0Angle + 32768;

      uint8_t Column0 = ((uint32_t)LedStrip0Angle * ImageColums) >> 16;
      uint8_t Column1 = ((uint32_t)LedStrip1Angle * ImageColums) >> 16;

      memcpy(LedStrip0, Img[Column0], LedAmount * 3);
      memcpy(LedStrip1, Img[Column1], LedAmount * 3);
      }
    FastLED.show();
    vTaskDelay(1);
  }
};

void setup(){
  Serial.begin(115200);

  ESP32PWM::allocateTimer(0);

  FastLED.addLeds<WS2812B, LedStrip0Pin, GRB>(LedStrip0, LedAmount);
  FastLED.addLeds<WS2812B, LedStrip1Pin, GRB>(LedStrip1, LedAmount);

  FastLED.setBrightness(Brightness);

  xTaskCreatePinnedToCore(LedTask, "Led Task", 4096, nullptr, 2, &LedTaskHandle, 0);
  
  pinMode(HallSensorDigitalPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HallSensorDigitalPin), HallInterrupt, FALLING);
  
  ledcAttachPin(EscPin, 0);
  ledcChangeFrequency(EscPin, 50, 14);
  Serial.println("ESCReady");
  ledcWrite(EscPin, minDutyCycle);

  /*ESC.setPeriodHertz(50);
  ESC.attach(EscPin, 1000, 2000);
  ESC.writeMicroseconds(1000);*/
}

void loop(){
  static uint8_t RxState = 0;
  static size_t RxRecived = 0;
  static uint32_t RxLastByteTime = 0;
  uint32_t RxLast = 0;
  const size_t ImgBytes = (size_t)ImageColums * LedAmount * 3;

  if (RxState != 0 && millis() - RxLastByteTime > 500) RxState = 0;

  while(Serial.available() > 0){
    RxLast = Serial.read();
    switch (RxState){
      case 0:
        switch (RxLast){
          case 0xAA:
            RxState = 1;
            break;
          case 0xAB:
            RxState = 3;
            break;
          case 0xAC:
            RxState = 5;
            break;
        }
        break;
      case 1:
        if (RxLast == 0x55){ RxState = 2;
          } else {RxState = 0;}
        break;
      case 2:
        ((uint8_t *)RxBuffer)[RxRecived] = RxLast;
        if (RxRecived >= ImgBytes){
          ShowImg = RxBuffer;
          RxBuffer = (RxBuffer == ImgMain) ? ImgBuffer : ImgMain;
          RxState = 0;
          Serial.println("OK");
        }
        break;
      case 3:
        if (RxLast == 0x56){ RxState = 4;
          } else {RxState = 0;}
        break;
      case 4:
        //ESC.writeMicroseconds(1000+min((int)RxLast, 100)*10);
        ledcWrite(EscPin, minDutyCycle + ((uint32_t)RxLast * (maxDutyCycle - minDutyCycle)) / 100);
        RxState = 0;
        break;
      case 5:
        if (RxLast == 0x57){ RxState = 6;
          } else {RxState = 0;}
        break;
      case 6:
        FastLED.setBrightness(RxLast);
        break;
    }
  }

  static uint32_t LastPrint = 0;
  if (millis() - LastPrint > 500) {
    LastPrint = millis();
    uint32_t AngleScaleLocal;
    portENTER_CRITICAL(&Mux);
    AngleScaleLocal = AngleScale;
    portEXIT_CRITICAL(&Mux);

    if (AngleScaleLocal) {
      const float RpmPerScale = 60000000.0f / 4294967296.0f;
      Serial.printf("RPM: %.1f \n", AngleScaleLocal * RpmPerScale);
    } else {Serial.println("hall pulse...");}
  }
  
  
}