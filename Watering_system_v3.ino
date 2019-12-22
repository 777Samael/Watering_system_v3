/*
 *  TO DO LATER
 *   save logs to SD card - custom watering show waterings only once, without duplicates. Variables previous date/time of watering
*/

#include <Wire.h>
#include <Time.h>
#include <DS3231.h>
#include <LiquidCrystal_I2C.h>
#include <TimerOne.h>
#include <SPI.h>
#include <SD.h>
#include <DHT.h>

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
int DHTPIN = 15;
float humidityValue = 0.0;
float tempValue     = 0.0;
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE); // Initialize DHT sensor for normal 16mhz Arduino

// microSD card reader - PINS for Mega2560 MISO-50, MOSI-51, SCK-52, SS-53
boolean dataSaved = false;
int sdCardPin     = 53;
String filename   = "data.csv";
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
  plannedEvent("2 01 08 00 01 100"),
  plannedEvent("3 21 00 00 01 1200"),
  plannedEvent("4 21 00 00 01 1200"),
  plannedEvent("5 21 00 00 01 1200"),
  plannedEvent("6 21 00 00 01 1200"),
  plannedEvent("7 21 00 00 01 1200"),
  plannedEvent("1 21 00 00 02 600"),
  plannedEvent("2 01 10 00 02 200"),
  plannedEvent("3 21 00 00 02 600"),
  plannedEvent("4 21 00 00 02 600"),
  plannedEvent("5 21 00 00 02 600"),
  plannedEvent("6 21 00 00 02 600"),
  plannedEvent("7 21 00 00 02 600")
  };

int eventCount    = 14;
int largePlanter  = 1;
int smallPlanter  = 2;

// I/O pins
int waterButtonPin      = 2;              // On/Off pin for custom watering
int lcdButtonPin        = 3;              // Turn on LCD and display all the necessary data
int timeErrorLED        = 4;              // LED pin for read time error - RED
int wateringLED         = 5;              // watering is ON, - BLUE
int waterPumpPin[4]     = {7,8,9,10};     // Water pumps relays pin (must be INPUT PULLUP)
int selectedPumpPin[4]  = {11,12,13,14};  // Selected water pump for custom watering

// Variables for custom watering using the buttons
volatile int waterButtonFlag  = 0;      // watering button clicked indicator
volatile bool waterNow        = false;  // water pump activation indicator
volatile int checkTimeFlag    = 0;      // flag for time interval interruptions
int currentPunpUsed           = 0;      // counter of watering pumps, each watering button click triggers next pump

// Variables for displaying data using the button
int lcdButtonFlag = 1;
String dateWaterScheduleLCD[2];
String timeWaterScheduleLCD[2];
String dateWaterScheduleCSV[2];
String timeWaterScheduleCSV[2];
String dateWaterCustomLCD[4];
String timeWaterCustomLCD[4];
String dateWaterCustomCSV[4];
String timeWaterCustomCSV[4];
bool customWateredCheck     = false;
bool customWatered[4]       = {false,false,false,false};
bool wateringStarted[4]     = {false,false,false,false};
bool wateringSaved[6]       = {true,true,true,true,true,true};
int wateringStartDay[4]     = {0,0,0,0};
int wateringStartHour[4]    = {0,0,0,0};
int wateringStartMinute[4]  = {0,0,0,0};
int wateringStartSecond[4]  = {0,0,0,0};
long wateringDuration[4]    = {0,0,0,0};

void setup() {

// Setting up output
  Wire.begin();       // Start the I2C interface
  Serial.begin(9600); // Start the serial interface
  lcd.begin(16,2);    // Init the LCD 2x16
  lcd.noBacklight();  // turn off backlight

// Starting humidity sensor
  dht.begin();

// Setting up pins

  // Read watering button
  pinMode(waterButtonPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(waterButtonPin),waterButtonClicked, CHANGE);
  pinMode(wateringLED,OUTPUT);
  for(int i = 0; i < 4; i++) {
    pinMode(selectedPumpPin[i],INPUT_PULLUP);
  }
  
  // Read display on/off button
  pinMode(lcdButtonPin, INPUT_PULLUP);

  // LEDs
  pinMode(timeErrorLED,OUTPUT);   // LED pin for read time error
  pinMode(wateringLED,OUTPUT);    // watering is ON, read time error (pinLED)

  // Water pump relay pin
  for(int i = 0; i < 4; i++) {
    pinMode(waterPumpPin[i],OUTPUT);
    digitalWrite(waterPumpPin[i], HIGH);
  }

// microSD card reader
  Serial.print("Initializing SD card...");
  pinMode(sdCardPin, OUTPUT); //Pin for writing to SD card

  if (!SD.begin(sdCardPin)) {
    Serial.println("initialization failed , or card not present");
  } else {
    Serial.println("initialization done.");
  }

  if (SD.exists(filename)) {
    logfile = SD.open(filename, FILE_WRITE);
    Serial.println("File open.");
  } else {
    Serial.println("File does not exists.");
  }

  logfile.println("Date,Time,Moisture 1,Moisture 2,Moisture 3,Moisture 4,Air Temp (C),Relative Humidity (%),Custom Date 1,Duration 1,Custom Date 2,Duration 2,Custom Date 3,Duration 3,Custom Date 4,Duration 4, Scheduled watering large, Scheduled watering small");   //HEADER
  
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
  String dateNowCSV = "20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
  String timeNowCSV = get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);

  Serial.println("--------------------------------------------");
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
  delay(2000);

// Start custom watering
  if (waterButtonFlag && waterNow){

    digitalWrite(wateringLED,HIGH);
    customWateredCheck = true;
    delay(250);

    for(int i = 0; i < 4; i++) {

      if(digitalRead(selectedPumpPin[i])== LOW) {

        digitalWrite(waterPumpPin[i],LOW);

        if(!wateringStarted[i]) {
          wateringStarted[i]      = true;
          wateringDuration[i]     = 0;
          wateringStartDay[i]     = dayNow;
          wateringStartHour[i]    = hourNow;
          wateringStartMinute[i]  = minuteNow;
          wateringStartSecond[i]  = secondNow;
          dateWaterCustomLCD[i]   = "Date: 20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
          timeWaterCustomLCD[i]   = "Time: " + get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);
          dateWaterCustomCSV[i]   = "20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
          timeWaterCustomCSV[i]   = get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);
          customWatered[i]        = true;
        }
      }
    }
    //Serial.println("The button is pressed, the water pump is working.");
  }

// Stop custom watering
  if (waterButtonFlag == 0 && waterNow){

    digitalWrite(wateringLED,LOW);
    waterNow = false;

    for(int i = 0; i < 4; i++) {

      if(digitalRead(selectedPumpPin[i])== LOW) {

        digitalWrite(waterPumpPin[i],HIGH);

        if (customWatered[i]) {
          wateringDuration[i]   = duration(wateringStartDay[i], wateringStartHour[i], wateringStartMinute[i], wateringStartSecond[i]);
          wateringStarted[i]    = false;
          wateringSaved[i]      = false;
        }
      }
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

            if(event.Planter == largePlanter) {

              for(byte i = 0; i < 2; i++) {

                if(moistMappedValue[i] < moistMaxPrc) {

                  digitalWrite(waterPumpPin[i],LOW);
                  digitalWrite(wateringLED,HIGH);
                  
                  for (int i = 0; i < 100; i++) {
                    delay(event.WateringTime);
                  }
        
                  digitalWrite(waterPumpPin[i],HIGH);
                  digitalWrite(wateringLED,LOW);
                  wateringSaved[4] = false;

                  dateWaterScheduleLCD[0] = "Date: 20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
                  timeWaterScheduleLCD[0] = "Time: " + get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);
                  dateWaterScheduleCSV[0] = "20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
                  timeWaterScheduleCSV[0] = get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);
                }
              }
            }

            if(event.Planter == smallPlanter) {

              for(byte i = 2; i < 4; i++) {

                if(moistMappedValue[i] < moistMaxPrc) {

                  digitalWrite(waterPumpPin[i],LOW);
                  digitalWrite(wateringLED,HIGH);
                  
                  for (int i = 0; i < 100; i++) {
                    delay(event.WateringTime);
                  }
        
                  digitalWrite(waterPumpPin[i],HIGH);
                  digitalWrite(wateringLED,LOW);
                  wateringSaved[5] = false;

                  dateWaterScheduleLCD[1] = "Date: 20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
                  timeWaterScheduleLCD[1] = "Time: " + get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);
                  dateWaterScheduleCSV[1] = "20" + String(yearNow) + "/" + get2digits(monthNow) + "/" + get2digits(dayNow);
                  timeWaterScheduleCSV[1] = get2digits(hourNow) + ":" + get2digits(minuteNow) + ":" + get2digits(secondNow);
                }
              }
            }
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

    // Date and time
    logfile.print(dateNowCSV);
    logfile.print(",");
    logfile.print(timeNowCSV);
    logfile.print(",");

    // Moisture
    for(int i = 0; i < 4; i++) {

      logfile.print(moistMappedValue[i]);
      logfile.print(",");
    }

    // Air temp and relative Humidity    
    tempValue = dht.readTemperature();
    logfile.print(tempValue);
    logfile.print(",");
  
    humidityValue = dht.readHumidity();
    logfile.print(humidityValue);
    logfile.print("%,");

    // Water pumps
    if (customWateredCheck) {
  
      for(int i = 0; i < 4; i++) {

        if(wateringSaved[i] == false) {
          logfile.print(dateWaterCustomCSV[i]);
          logfile.print(" ");
          logfile.print(timeWaterCustomCSV[i]);
          logfile.print(",");
          logfile.print(get2digits(round(wateringDuration[i] / 60)));
          logfile.print(":");
          logfile.print(get2digits(wateringDuration[i] % 60));
          logfile.print(",");
          wateringSaved[i] = true;
        } else {
          logfile.print(",,");
        }
      }
    } else {
      logfile.print(",,,,,,,,");
    }

  if(wateringSaved[4] == false) {
    logfile.print(dateWaterScheduleCSV[0]);
    logfile.print(" ");
    logfile.print(timeWaterScheduleCSV[0]);
    logfile.print(",");
    wateringSaved[4] = true;
  } else {
    logfile.print(",");
  }

  if(wateringSaved[5] == false) {
    logfile.print(dateWaterScheduleCSV[1]);
    logfile.print(" ");
    logfile.print(timeWaterScheduleCSV[1]);
    logfile.print(",");
    wateringSaved[5] = true;
  } else {
    logfile.print(",");
  }
    
    dataSaved = true;
    logfile.println();
    logfile.flush();
    Serial.println("New data saved");
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

  // Air temp and relative Humidity
  if (isnan(humidityValue) || isnan(tempValue)) {

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Temp: ");
    lcd.print(tempValue);
    lcd.print(" *C");

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("RH: ");
    lcd.print(humidityValue);
    lcd.print(" %");
  }

  // Last watering datetime
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Last watering");
  delay(2000);

  if (customWateredCheck) {

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Custom");
    lcd.setCursor(0,1);
    lcd.print("Date and Time");
    delay(2000);

    for(int i = 0; i < 4; i++) {
      
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print(dateWaterCustomLCD[i]);
      lcd.setCursor(0,1);
      lcd.print(timeWaterCustomLCD[i]);
      delay(2000);

      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Duration pump ");
      lcd.print(i);
      lcd.setCursor(0,1);
      lcd.print(get2digits(round(wateringDuration[i] / 60)));
      lcd.print(":");
      lcd.print(get2digits(wateringDuration[i] % 60));
      delay(3000);
    }
  }
  
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Scheduled");
  lcd.setCursor(0,1);
  lcd.print("Date and Time");
  delay(2000);
  
  for(int i = 0; i < 2; i++) {
    
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(dateWaterScheduleLCD[i]);
    lcd.setCursor(0,1);
    lcd.print(timeWaterScheduleLCD[i]);
    delay(3000);
  }

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
