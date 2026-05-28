#include <Arduino.h>
#include <Wire.h>
#include <BleMouse.h>
#include <esp_sleep.h>
#include <esp_pm.h>
#include <driver/rtc_io.h>

BleMouse bleMouse("Bluetooth Nunchuk Mouse", "Created by yyya_nico");

#define NUNCHK_ADDR (0x52)
#define COMPLETE_COUNT (5)
#define NUNCHK_Z_MASK 0x01
#define NUNCHK_C_MASK 0x02
#define POSITION_MARGIN 5
#define WAKEUP_BUTTON_PIN D3
#define INACTIVITY_TIMEOUT (10 * 60 * 1000)  // 10 minutes
#define BLE_DISCONNECTED_SLEEP_TIME 100  // 100ms sleep when BLE disconnected

static int initXposi = 0;
static int initYposi = 0;
static int intervalCount = 0;
static bool disableCount = false;
static bool disableScroll = false;
static unsigned long lastBatteryReportTime = 0;
static const unsigned long BATTERY_REPORT_INTERVAL = 10 * 60 * 1000; // 10 minutes in milliseconds
static unsigned long lastActivityTime = 0;
static unsigned long buttonPressedTime = 0;
static float accumulatedX = 0.0;
static float accumulatedY = 0.0;

void initNunchuk(void);
char decodeNunchukData (char x);
boolean nunchuckIsAvailable(int *x, int *y, uint8_t *button);
void getInitPosition(int *x, int *y, uint8_t *button);
float batteryVoltage();
int getBatteryPercentage();
void enterDeepSleep();

void setup() {
  // Setup wakeup button
  pinMode(WAKEUP_BUTTON_PIN, INPUT_PULLUP);
  
  // Configure GPIO wakeup (wake on LOW - button pushed)
  esp_deep_sleep_enable_gpio_wakeup(BIT(WAKEUP_BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
  
  // Enable automatic light sleep
  esp_pm_config_esp32c3_t pm_config = {
    .max_freq_mhz = 80,
    .min_freq_mhz = 10,
    .light_sleep_enable = true
  };
  esp_pm_configure(&pm_config);
  
  bleMouse.begin();
  bleMouse.setBatteryLevel(getBatteryPercentage());
  pinMode(A0, INPUT);
  initNunchuk();
  
  lastActivityTime = millis();
}

void loop() {
  int     x      = 0;
  int     y      = 0;
  uint8_t button = 0;
  unsigned long currentTime = millis();
  
  // Check D3 button for deep sleep
  bool currentButtonState = (digitalRead(WAKEUP_BUTTON_PIN) == LOW);
  
  if(currentButtonState) {
    // Wait for button release before entering deep sleep
    while(digitalRead(WAKEUP_BUTTON_PIN) == LOW) {
      delay(10);
    }
    delay(50);  // デバウンス用の短い待機
    enterDeepSleep();
  }
  
  // Check inactivity timeout
  if(currentTime - lastActivityTime >= INACTIVITY_TIMEOUT) {
    enterDeepSleep();
  }
  
  // Report battery percentage every 10 minutes
  if(bleMouse.isConnected() && (currentTime - lastBatteryReportTime >= BATTERY_REPORT_INTERVAL)) {
    // Send battery percentage via HID report
    // Note: BleMouse sends this as a special HID report
    bleMouse.setBatteryLevel(getBatteryPercentage());
    lastBatteryReportTime = currentTime;
  }
  
  if(bleMouse.isConnected()){
    if(nunchuckIsAvailable(&x, &y, &button) ){
      int xPosi = x - initXposi;
      int yPosi = y - initYposi;

      if(button & NUNCHK_C_MASK) { // c button
        if(intervalCount >= 1000/5) {
          bleMouse.press(MOUSE_RIGHT);
          intervalCount = 0;
          disableCount = true;
          disableScroll = true;
          delay(5);
          bleMouse.release(MOUSE_RIGHT);
        }
        else {
          if(abs(xPosi) > POSITION_MARGIN || abs(yPosi) > POSITION_MARGIN) {
            intervalCount = 0;
            disableCount = true;
            if(yPosi > POSITION_MARGIN && !disableScroll) {
              bleMouse.move(0, 0, 1);
            }
            else if(yPosi < -POSITION_MARGIN && !disableScroll) {
              bleMouse.move(0, 0, -1);
            }
            int delay1 = max(-3 * abs(yPosi) + 240, 0);
            if(xPosi > POSITION_MARGIN && !disableScroll) {
              bleMouse.move(0, 0, 0, 1);
            }
            else if(xPosi < -POSITION_MARGIN && !disableScroll) {
              bleMouse.move(0, 0, 0, -1);
            }
            int delay2 = max(-3 * abs(xPosi) + 240, 0);
            delay(min(delay1, delay2));
          }
          else if(!disableCount) {
            intervalCount++;
          }
        }
      
        // Update last activity time on any input
        lastActivityTime = currentTime;
      }
      else {
        if(abs(xPosi) > POSITION_MARGIN || abs(yPosi) > POSITION_MARGIN) {
          // Accumulate fractional movement for smoother cursor motion
          accumulatedX += (float)xPosi * 50.0 / 127.0;
          accumulatedY -= (float)yPosi * 50.0 / 127.0;
          
          // Extract integer part for movement
          signed char moveX = (signed char)accumulatedX;
          signed char moveY = (signed char)accumulatedY;
          
          if(moveX != 0 || moveY != 0) {
            bleMouse.move(moveX, moveY); // stick position x y
            accumulatedX -= moveX;
            accumulatedY -= moveY;
          }
      
          // Update last activity time on any input
          lastActivityTime = currentTime;
        }
        else {
          // Reset accumulation when within dead zone
          accumulatedX = 0.0;
          accumulatedY = 0.0;
        }
        intervalCount = 0;
        disableCount = false;
        disableScroll = false;
      }

      if(button & NUNCHK_Z_MASK) { // z button 
        bleMouse.press(MOUSE_LEFT);
      
        // Update last activity time on any input
        lastActivityTime = currentTime;
      }
      else {
        bleMouse.release(MOUSE_LEFT);
      }
    }
    else {
      bleMouse.release(MOUSE_ALL);
    }
    delay(5);
  }
  else {
    // BLE not connected - sleep longer to save power
    delay(BLE_DISCONNECTED_SLEEP_TIME);
  }
}

void initNunchuk(void) {
  uint8_t     dummy = 0;

  Wire.begin(SDA, SCL);
  Wire.beginTransmission(NUNCHK_ADDR);
  Wire.write((uint8_t)0x40);
  Wire.write((uint8_t)0x00);
  Wire.endTransmission();
  getInitPosition(&initXposi, &initYposi, &dummy);
  delay(5);
  getInitPosition(&initXposi, &initYposi, &dummy);
}

char decodeNunchukData (char x)
{
    x = (x ^ 0x17) + 0x17;
    return x;
}

boolean nunchuckIsAvailable(int *x, int *y, uint8_t *button)
{
  static uint8_t nunchuck_buf[6];
  int cnt = 0;

  Wire.requestFrom (NUNCHK_ADDR, 6);
  while (Wire.available ()) {
      nunchuck_buf[cnt] = decodeNunchukData( Wire.read() );
      cnt++;
  }
  
  Wire.beginTransmission(NUNCHK_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.endTransmission();
  
  if(cnt >= COMPLETE_COUNT){
    *button = 0;
    if(!(nunchuck_buf[5] & 0x01)) *button  = NUNCHK_Z_MASK;    // z_button 
    if(!(nunchuck_buf[5] & 0x02)) *button |= NUNCHK_C_MASK;   // c_button  
    *x = nunchuck_buf[0];
    *y = nunchuck_buf[1];
    return true;
  }
  else{
    return false;
  }
}

void getInitPosition(int *x, int *y, uint8_t *button)
{
    nunchuckIsAvailable(x, y, button);
}

float batteryVoltage() {
  uint32_t Vbatt = 0;
  for(int i = 0; i < 8; i++) {
    Vbatt = Vbatt + analogReadMilliVolts(A0); // ADC with correction   
  }
  float Vbattf = 2 * Vbatt / 8 / 1000.0;     // attenuation ratio 1/2, mV --> V
  return Vbattf;
}

int getBatteryPercentage() {
  float voltage = batteryVoltage();
  if(voltage >= 4.0) return 100;
  else if(voltage <= 2.8) return 0;
  else return (int)((voltage - 2.8) / (4.0 - 2.8) * 100);
}

void enterDeepSleep() {
  // Release all mouse buttons before sleep
  bleMouse.release(MOUSE_ALL);
  delay(100);
  
  // Disconnect BLE
  // bleMouse.end();
  // delay(100);
  
  // Enter deep sleep (wake on D3 button press)
  esp_deep_sleep_start();
}
