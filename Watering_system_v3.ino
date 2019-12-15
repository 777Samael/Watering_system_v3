/*
 *  TO DO LATER
 *  indywidualnie podlewane donice z ręcznego odpalenia (menu lub każde uruchomienie przełącza 
    zmienną numeru pomki na kolejną
 *   save logs to SD card
 *   humidity sensor
*/

#include <Wire.h>
#include <Time.h>
#include <DS3231.h>
#include <LiquidCrystal_I2C.h>
#include <TimerOne.h>
#include <SPI.h>
#include <SD.h>

// Real Time Clock DS3231
DS3231 RTC;
bool Century = false;
bool h12 = false;
bool PM = false;

// LCD with I2C module
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

// Moisture sensors
int moistSensorPin[4]   = {A0,A1,A2,A3}; // Moisture sensors analog pins A0 to A3
int moistRead[4]        = {0,0,0,0};     // Raw moisture values from analog pins A0 to A3
int moistMappedValue[4] = {0,0,0,0};     // Moisture values after calculation
int moistMaxADC    = 615;  // Replace with min ADC value read in the air
int moistMinADC    = 140;  // Replace with max ADC value read fully submerged in water
int moistMaxPrc    = 60;   // The maximum value for soil moisture

// Humidity sensor
int DHTPIN = 9;

// microSD card reader
boolean dataSaved = false;
int sdCardPin     = 10;
File logfile;

class plannedEvent{
  public:
  plannedEvent(const char* value);
  int Hour, Min, Sec;
  int WeekDay;
  int Planter;
  int WateringTime;
};

// Watering schedule
plannedEvent::plannedEvent(const char* value)
{
  sscanf(value, "%d %d %d %d %d %i", &WeekDay, &Hour, &Min, &Sec, &Planter, &WateringTime);
}
//Length -> 10 is the seconds
// 1 is Sunday
plannedEvent schedule[]={
  plannedEvent("1 21 00 00 01 1200"),
  plannedEvent("2 21 00 00 01 1200"),
  plannedEvent("3 21 00 00 01 1200"),
  plannedEvent("4 21 00 00 01 1200"),
  plannedEvent("5 21 00 00 01 1200"),
  plannedEvent("6 21 00 00 01 1200"),
  plannedEvent("7 21 00 00 01 1200"),
  plannedEvent("1 21 00 00 02 600"),
  plannedEvent("2 21 00 00 02 600"),
  plannedEvent("3 21 00 00 02 600"),
  plannedEvent("4 21 00 00 02 600"),
  plannedEvent("5 21 00 00 02 600"),
  plannedEvent("6 21 00 00 02 600"),
  plannedEvent("7 21 00 00 02 600")
  };

int eventCount = 14;
int largePlanter = 1;
int smallPlanter = 2;

// I/O pins
int waterButtonPin  = 2;          // On/Off pin for custom watering
int lcdButtonPin    = 3;          // Turn on LCD and display all the necessary data
int timeErrorLED    = 4;          // LED pin for read time error - RED
int wateringLED     = 5;          // watering is ON, - BLUE
int chargingLED     = 6;          // LED pin for charging indicator - GREEN
int waterPumpPin[4] = {7,8,A4,A5}; // Water pumps relays pin

// Variables for custom watering using the buttons
volatile int waterButtonFlag  = 0;      // watering button clicked indicator
volatile bool waterNow        = false;  // water pump activation indicator
volatile int checkTimeFlag    = 0;      // flag for time interval interruptions

// Variables for displaying data using the button
int lcdButtonFlag = 1;
String dateWaterScheduleLCD;
String timeWaterScheduleLCD;
String dateWaterCustomLCD;
String timeWaterCustomLCD;
bool customWatered      = false;
bool wateringStarted    = false;
int wateringStartDay    = 0;
int wateringStartHour   = 0;
int wateringStartMinute = 0;
int wateringStartSecond = 0;
long wateringDuration   = 0;

void setup() {

// Setting up output
  Wire.begin();       // Start the I2C interface
  Serial.begin(9600); // Start the serial interface
  lcd.begin(16,2);    // Init the LCD 2x16
  lcd.noBacklight();  // turn off backlight

// Setting up pins

  // Read watering button
  pinMode(waterButtonPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(waterButtonPin),waterButtonClicked, CHANGE);
  pinMode(wateringLED,OUTPUT);
  
  // Read display on/off button
  pinMode(lcdButtonPin, INPUT_PULLUP);

  // LEDs
  pinMode(timeErrorLED,OUTPUT);   // LED pin for read time error
  pinMode(wateringLED,OUTPUT);    // watering is ON, read time error (pinLED)

  // Water pump relay pin
  pinMode(waterPumpPin,OUTPUT);
  digitalWrite(waterPumpPin, HIGH);

  // microSD card reader
  pinMode(sdCardPin, OUTPUT); //Pin for writing to SD card

  char filename[] = "LOGFILE01.CSV";
  for (uint8_t i = 0; i < 100; i++) {
    filename[6] = i/10 + '0';
    filename[7] = i%10 + '0';
    if (! SD.exists(filename)) {              // only open a new file if it doesn't exist
      logfile = SD.open(filename, FILE_WRITE); 
      break;                                  // leave the loop
    }
  }

  logfile.println("Date,Time,Moisture 1, Moisture 2, Moisture 3, Moisture 4, Air Temp (C),Relative Humidity (%),Watering");   //HEADER 
  
  // Initialize interruptions
  Timer1.initialize(1000000);
  Timer1.attachInterrupt(ReadTimeNow);
}

void loop() {

// Time reading from RTC
  int yearNow       = RTC.getYear();          // current year from real time clock
  int monthNow      = RTC.getMonth(Century);  // current month from real time clock
  int dayNow        = RTC.getDate();          // current day from real time clock
  int wDayNow       = RTC.getDoW();           // current weekday from real time clock (1 = SUNDAY)
  int hourNow       = RTC.getHour(h12, PM);   // current hour from real time clock
  int minuteNow     = RTC.getMinute();        // current minute from real time clock
  int secondNow     = RTC.getSecond();        // current second from real time clock

// Moisture read
  for(byte i = 0; i < 4; i++) {
    moistRead[i]        = analogRead(moistSensorPin[i]);
    moistMappedValue[i] = map(moistRead[i],moistMaxADC,moistMinADC, 0, 100);
  }

// LCD button read
  lcdButtonFlag = digitalRead(lcdButtonPin);
  
// Current date and time to display on lcd
  String dateNowLCD = "Date: 20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
  String timeNowLCD = "Time: " + get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);

  /*Serial.println("--------------------------------------------");
  Serial.println(tNow);
  Serial.print("yearNow = ");
  Serial.println(yearNow);
  Serial.print("monthNow = ");
  Serial.println(monthNow);
  Serial.print("dayNow = ");
  Serial.println(dayNow);
  Serial.print("wDayNow = ");
  Serial.println(wDayNow);
  Serial.print("hourNow = ");
  Serial.println(hourNow);
  Serial.print("minuteNow = ");
  Serial.println(minuteNow);
  Serial.print("secondNow = ");
  Serial.println(secondNow);
  delay(5000);*/

// Start custom watering
  if (waterButtonFlag && waterNow){
    
    delay(250);
    digitalWrite(waterPumpPin,LOW);
    digitalWrite(wateringLED,HIGH);
    customWatered = true;
    
    if(!wateringStarted) {
      wateringStarted     = true;
      wateringDuration    = 0;
      wateringStartDay    = dayNow;
      wateringStartHour   = hourNow;
      wateringStartMinute = minuteNow;
      wateringStartSecond = secondNow;
      dateWaterCustomLCD  = "Date: 20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
      timeWaterCustomLCD  = "Time: " + get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);
    }
    //Serial.println("The button is pressed, the water pump is working.");
  }

// Stop custom watering
  if (waterButtonFlag == 0 && waterNow){
    
    digitalWrite(waterPumpPin,HIGH);
    digitalWrite(wateringLED,LOW);
    waterNow = false;
    
    if (customWatered) {
      wateringDuration  = duration(wateringStartDay, wateringStartHour, wateringStartMinute, wateringStartSecond);
      wateringStarted   = false;
    }
    //Serial.println("The button has been released, the water pump stopped working.");
  }

// Start validations before scheduled watering
  if (waterButtonFlag == 0 && checkTimeFlag){
    
    if (yearNow < 50) {   // Check if read datetime is not 1/1/1960

      digitalWrite(timeErrorLED,LOW);
      //Serial.println("Read from RTC is OK");

      for (int i = 0; i < eventCount; i++) {

        plannedEvent event = schedule[i];
        //Serial.println("Looping through event schedule.");

        if (wDayNow == event.WeekDay){
          //Serial.println("Weekday matches the schedule element.");
          
          if (hourNow == event.Hour && minuteNow == event.Min /*&& secondNow == event.Sec*/){
            // Serial.println("Hour and minute match the schedule element. Watering...");
            
// Watering BEGIN

            if(event.Planter = largePlanter) {

              for(byte i = 0; i < 2; i++) {

                if(moistMappedValue[i] < moistMaxPrc) {

                  digitalWrite(waterPumpPin[i],LOW);
                  digitalWrite(wateringLED,HIGH);
                  
                  for (int i = 0; i < 100; i++) {
                    delay(event.WateringTime);
                  }
        
                  digitalWrite(waterPumpPin[i],HIGH);
                  digitalWrite(wateringLED,LOW);
                }
              }
            }

            if(event.Planter = smallPlanter) {

              for(byte i = 2; i < 4; i++) {

                if(moistMappedValue[i] < moistMaxPrc) {

                  digitalWrite(waterPumpPin[i],LOW);
                  digitalWrite(wateringLED,HIGH);
                  
                  for (int i = 0; i < 100; i++) {
                    delay(event.WateringTime);
                  }
        
                  digitalWrite(waterPumpPin[i],HIGH);
                  digitalWrite(wateringLED,LOW);
                }
              }
            }

            dateWaterScheduleLCD = "Date: 20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
            timeWaterScheduleLCD = "Time: " + get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);
            //Serial.println("Watering finished.");

            delay(60000);   // Delay not to fall into the same loop (scheduled minute) for the second time.
          }
        }
      }

      checkTimeFlag=0;
    } else {

      if (yearNow > 50) {  // In case of disconnection of RTC or RTC has a malfunction

        digitalWrite(timeErrorLED,HIGH);
        //Serial.println("The DS3231 is stopped.  Please run the SetTime or check the circuitry.");
      }
    }
  }

// Save data to file

  if(minuteNow%10 == 0 && dataSaved == false) {

    logfile.print(dateNowLCD);
    logfile.print(",");
    logfile.print(timeNowLCD);
    logfile.print(",");

    for(int i = 0; i < 4; i++) {

      logfile.print(moistMappedValue[i]);
    }
    dataSaved = true;
  } else if (minuteNow%10 != 0 && dataSaved == true) {
    
    dataSaved = false;
  }

// Turn on LCD and display all data

  if (lcdButtonFlag == LOW) {

  lcd.backlight();
  lcd.display();

  // Display basic data
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Hello :)");
  lcd.setCursor(0,1);
  lcd.print("Watering system");
  delay(3000);

  // Current date and time
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Current DateTime");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(dateNowLCD);
  lcd.setCursor(0,1);
  lcd.print(timeNowLCD);
  delay(3000);

  // Soil moisture level
  for(int i = 0; i < 4; i++) {

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Moisture level");
    lcd.setCursor(0,1);
    lcd.print(i);
    lcd.print(" - ");
    lcd.print(moistMappedValue[i]);
    lcd.print(" %");
    delay(3000);
  }

  // Last watering datetime
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Last watering");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Custom");
  lcd.setCursor(0,1);
  lcd.print("Date and Time");
  delay(2000);
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(dateWaterCustomLCD);
  lcd.setCursor(0,1);
  lcd.print(timeWaterCustomLCD);
  delay(2000);

  if (customWatered) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Duration");
    lcd.setCursor(0,1);
    lcd.print(get2digits(round(wateringDuration / 60)));
    lcd.print(":");
    lcd.print(get2digits(wateringDuration % 60));
    delay(3000);
  }
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Scheduled");
  lcd.setCursor(0,1);
  lcd.print("Date and Time");
  delay(2000);
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(dateWaterScheduleLCD);
  lcd.setCursor(0,1);
  lcd.print(timeWaterScheduleLCD);
  delay(3000);

  lcd.clear();
  lcd.noBacklight();
  lcd.noDisplay();
  }
  delay(500);
}

void ledBlink(int pinLED, int blinkCount, int intervalTime) {

  for (int i; i < blinkCount; i++) {
    digitalWrite(pinLED, HIGH);
    delay(intervalTime);
    digitalWrite(pinLED, LOW);
    delay(intervalTime);
  }
}

String get2digits(int number) { // return number lower than 10 as a string with 0 as a prefix
  String str;
  if (number >= 0 && number < 10) {
    str = "0" + String(number);
  } else {
    str = String(number);
  }
  return str;
}

void waterButtonClicked(){
  if(digitalRead(waterButtonPin)== LOW){
    waterButtonFlag = 1;
  }
  else{
    waterButtonFlag = 0;
  }
  waterNow = true;
}

void ReadTimeNow(){
  checkTimeFlag=1;
}

long duration(int day, int hour, int minute, int second) {
  long currentTime;
  long durationSeconds;
  
  int dayCurrent     = RTC.getDate();
  int hourCurrent    = RTC.getHour(h12, PM);
  int minuteCurrent  = RTC.getMinute();
  int secondCurrent   = RTC.getSecond();
  
  long inputTime = day * 86400 + hour * 3600 + minute * 60 + second;
  
  if(dayCurrent < day) {
    currentTime = (dayCurrent + day) * 86400 + hourCurrent * 3600 + minuteCurrent * 60 + secondCurrent;
  } else {
    currentTime = dayCurrent * 86400 + hourCurrent * 3600 + minuteCurrent * 60 + secondCurrent;
  }

  durationSeconds = currentTime - inputTime;

  if(durationSeconds < 0) {
    return 0;
  } else {
    return durationSeconds;
  }
  
}
