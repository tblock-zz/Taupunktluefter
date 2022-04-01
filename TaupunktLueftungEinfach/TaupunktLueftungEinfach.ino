/*
This code is without any warranty and without any liability of any kind.
Be aware that the FAN may use high voltage and it is your responsibility to understand that wrong usage may damage 
your life or your property.
*/
// These three libraries are necessary:
#include "DHT.h"       // DHT sensor library from Adafruit
#include <Wire.h> 
#include <avr/wdt.h>

//-------------------------------------------------------------------------------------------------------
#define PIN_RELAIS          6 // air venting relais control pin
#define PIN_DHT1            5 // data line inner DHT sensor
#define PIN_DHT2            4 // data line outer DHT sensor
#define PIN_LED_OK         11
#define PIN_LED_ERR        12

#define RELAIS_ON        HIGH
#define RELAIS_OFF        LOW

#define CORRECTION_T_1  -3.0f // correction value for inner temperature
#define CORRECTION_T_2  -4.0f // correction value for outer temperature
#define CORRECTION_H_1   0.0f  // correction value for inner humidity
#define CORRECTION_H_2   0.0f  // correction value for outer humidity

#define MIN_DEW          5.0f // minimal dew point difference for switching the relais
#define HYSTERESIS       1.0f // Distence for relais on/off switching
#define MIN_TEMP_1      10.0f // minimal inner temperature for activating the venting
#define MIN_TEMP_2     -10.0f // minimal outer temperature for activating the venting
//-------------------------------------------------------------------------------------------------------
typedef struct {
    float tmprt;              // temperatur
    float hmdty;              // humidity
} tData;

typedef struct {
    tData avrg;
    tData crctn;              // correction values
    float dp;                 // dew point
    DHT* pDht;                // pointer to the sensor object
} tFiltered;
//-------------------------------------------------------------------------------------------------------

DHT dht1(PIN_DHT1, DHT22);    // the inner sensor
DHT dht2(PIN_DHT2, DHT22);    // the outer sensor
tFiltered data[2];            // the data structure for the values for inner and outer sensor
bool relais, relaisState;

//-------------------------------------------------------------------------------------------------------
void initHmi()
{
  Serial.begin(115200);       // debugging output
  Serial.println(F("Testing sensors.."));
}

void printHmi(tFiltered& inner, tFiltered& outer)
{
  static const char txtSensor1[]     = "Sensor 1: ";
  static const char txtSensor2[]     = "Sensor 2: ";
  static const char txtHumidity[]    = "Humidity: ";
  static const char txtTemp[]        = "%  Temperature: ";
  static const char txtDewPoint[]    = "  Dew Point: ";
  static const char txtUnitCelsius[] = "°C  ";
    //  debugging output
#if 1
  Serial.print(txtSensor1);
  Serial.print(txtHumidity);
  Serial.print(inner.avrg.hmdty);                     
  Serial.print(txtTemp);
  Serial.print(inner.avrg.tmprt);
  Serial.print(txtUnitCelsius);
  Serial.print(txtDewPoint);
  Serial.print(inner.dp);
  Serial.println(txtUnitCelsius);

  Serial.print(txtSensor2);
  Serial.print(txtHumidity);
  Serial.print(outer.avrg.hmdty);
  Serial.print(txtTemp);
  Serial.print(outer.avrg.tmprt);
  Serial.print(txtUnitCelsius);
  Serial.print(txtDewPoint);
  Serial.print(outer.dp);
  Serial.println(txtUnitCelsius);
  Serial.println();
#endif
}
//-------------------------------------------------------------------------------------------------------
void signalError()
{
  digitalWrite(PIN_LED_ERR, HIGH); 
  digitalWrite(PIN_LED_OK , LOW); 
  digitalWrite(PIN_RELAIS , RELAIS_OFF); 
}

void signalOk()
{
  digitalWrite(PIN_LED_ERR, LOW); 
  digitalWrite(PIN_LED_OK , HIGH); 
}
//-------------------------------------------------------------------------------------------------------
float calcDewPoint(float t, float r) 
{ 
  float a, b;
  
  if (t >= 0.0f) {
    a =   7.5f;
    b = 237.3f;
  } 
  else if (t < 0.0f) {
    a =   7.6f;
    b = 240.7f;
  }
  
  // Saturated vapor pressure in hPa
  float svp = 6.1078f * pow(10.0f, (a*t)/(b+t));
  
  // Vapor pressure in hPa
  float vp = svp * (r/100.0f);
  
  // v-Parameter
  float v = log10(vp/6.1078f);
  
  // dew point temperature (°C)
  return (b*v) / (a-v);
}
//-------------------------------------------------------------------------------------------------------
void software_Reset() // restarts the program, but not the Sensors 
{
  asm volatile ("  jmp 0");  
}
//-------------------------------------------------------------------------------------------------------
void setup() {
  wdt_enable(WDTO_8S);                  // set watchdog timer to 8 seconds
  pinMode(PIN_RELAIS, OUTPUT);          
  digitalWrite(PIN_RELAIS, RELAIS_OFF); 

  pinMode(PIN_LED_OK, OUTPUT);       
  digitalWrite(PIN_LED_OK, LOW); 

  pinMode(PIN_LED_ERR, OUTPUT);       
  digitalWrite(PIN_LED_ERR, LOW); 

  initHmi();
      
  dht1.begin();                         // start sensors
  dht2.begin();   

  // initialize the data structure
  data[0].crctn.tmprt = CORRECTION_T_1;
  data[1].crctn.tmprt = CORRECTION_T_2;
  data[0].crctn.hmdty = CORRECTION_H_1;
  data[1].crctn.hmdty = CORRECTION_H_2;
  data[0].pDht = &dht1;
  data[1].pDht = &dht2;
  
  relaisState = false;
}
//-------------------------------------------------------------------------------------------------------
void waitForWatchdogReset()
{
  while(1);
}
//-------------------------------------------------------------------------------------------------------
bool isFastIntervalExceeded()
{
  static const uint32_t cInterval = 1000; // 1 second
  static       uint32_t last      = 0;
  
  uint32_t now = millis();
  bool     ret = (now >= last);
  if(ret) {
   last = now + cInterval;
  }
  return ret;
}

bool isSlowIntervalExceeded()
{
  static const uint32_t cInterval = 5000; // 5 seconds
  static       uint32_t last      = 0;
  
  uint32_t now = millis();
  bool     ret = (now >= last);
  if(ret) {
   last = now + cInterval;
  }
  return ret;
}
//-------------------------------------------------------------------------------------------------------
inline float calcMovingAverage(float x, float old)
{
  // calculate the moving average of the last 5 measurements
  return (4.0f*old + x)/5.0f;
}
//-------------------------------------------------------------------------------------------------------
void loop() 
{
  wdt_reset();                                         // Watchdog reset
  if(isFastIntervalExceeded()) {
    float h,t;
    for(auto i=0; i<2; ++i) {
      tFiltered& r = data[i];
      h = r.pDht->readHumidity();
      t = r.pDht->readTemperature();
      // Serial.println("\nh,t"); Serial.print(h); Serial.print(","); Serial.print(t);
      if(isnan(h) || isnan(t)) {
        signalError();
        waitForWatchdogReset();
      }
      else {
        signalOk();
        h += r.crctn.hmdty;
        t += r.crctn.tmprt;
        r.avrg.hmdty = calcMovingAverage(h, r.avrg.hmdty );
        r.avrg.tmprt = calcMovingAverage(t, r.avrg.tmprt );
      }
    }
  }

  if(isSlowIntervalExceeded()) {
    relais = false;
    for(auto i=0; i<2; ++i) {
      tFiltered& r = data[i];
      // becomes true when values are not allowed range
      relais = relais || (r.avrg.hmdty > 100.0f) || (r.avrg.hmdty < 1.0f);
      relais = relais || (r.avrg.tmprt < -40.0f) || (r.avrg.tmprt > 80.0f);
    }
    printHmi(data[0], data[1]);
    relais = !relais; // set relais when in allowed range
    if(relais) {
      for(auto i=0; i<2; ++i) {
        tFiltered& r = data[i];
        r.dp = calcDewPoint(r.avrg.tmprt, r.avrg.hmdty);
      }        
      float DeltaDp = data[0].dp - data[1].dp;

      // hysteresis calculation
      if(relaisState) {
        relais = (DeltaDp > MIN_DEW);
      }
      else {
        relais = (DeltaDp > (MIN_DEW + HYSTERESIS)); 
      }
      relais = relais && 
              (data[0].avrg.tmprt >= MIN_TEMP_1) &&
              (data[1].avrg.tmprt >= MIN_TEMP_2);
    }
    relaisState = relais;
    digitalWrite(PIN_RELAIS, relais ? RELAIS_ON : RELAIS_OFF);
  }
}
