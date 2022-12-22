/*
  CosmicWatch Desktop Muon Detector Arduino Code

  This code does not use the microSD card reader/writer, but does used the OLED screen.
  
  Questions?
  Spencer N. Axani
  saxani@mit.edu

  Requirements: Sketch->Include->Manage Libraries:
  SPI, EEPROM, SD, and Wire are probably already installed.
  1. Adafruit SSD1306     -- by Adafruit Version 1.0.1
  2. Adafruit GFX Library -- by Adafruit Version 1.0.2
  3. TimerOne             -- by Jesse Tane et al. Version 1.1.0
*/

#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <TimerOne.h>
#include <Wire.h>
#include <SPI.h>
#include <EEPROM.h>

typedef struct Numbers {                             //each number has a num, knows wheter it is rolling or not and know the next number after it
        int num; 
        bool roll;
        struct Numbers *next; 
  } Number;


Number *head = malloc(sizeof(Number));

const byte OLED = 1;                      // Turn on/off the OLED [1,0]

const int SIGNAL_THRESHOLD      = 50;    // Min threshold to trigger on. See calibration.pdf for conversion to mV.
const int RESET_THRESHOLD       = 25;    

const int LED_BRIGHTNESS        = 255;    // Brightness of the LED [0,255]

const long double cal[] = {-9.085681659276021e-27, 4.6790804314609205e-23, -1.0317125207013292e-19,
  1.2741066484319192e-16, -9.684460759517656e-14, 4.6937937442284284e-11, -1.4553498837275352e-08,
   2.8216624998078298e-06, -0.000323032620672037, 0.019538631135788468, -0.3774384056850066, 12.324891083404246};
   
const int cal_max = 1023;

//INTERUPT SETUP
#define TIMER_INTERVAL 1000000          // Every 1,000,000 us the timer will update the OLED readout

//OLED SETUP
#define OLED_RESET 10
Adafruit_SSD1306 display(OLED_RESET);

//initialize variables
char detector_name[40];

unsigned long time_stamp                      = 0L;
unsigned long measurement_deadtime            = 0L;
unsigned long time_measurement                = 0L;      // Time stamp
unsigned long interrupt_timer                 = 0L;      // Time stamp
int start_time                                = 0L;      // Reference time for all the time measurements
unsigned long total_deadtime                  = 0L;      // total measured deadtime
unsigned long waiting_t1                      = 0L;
unsigned long measurement_t1;
unsigned long measurement_t2;

float sipm_voltage                            = 0;
long int count                                = 0L;      // A tally of the number of muon counts observed
float last_sipm_voltage                       = 0;


byte waiting_for_interupt                     = 0;
byte SLAVE;
byte MASTER;
byte keep_pulse                               = 0;


void OpeningScreen(void) 
{
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(8, 0);
    display.clearDisplay();
    display.print(F("True\nRandomness!"));
    display.display();
    display.setTextSize(1);
    display.clearDisplay();
}

boolean get_detector_name(char* det_name) 
{
    byte ch;                              // byte read from eeprom
    int bytesRead = 0;                    // number of bytes read so far
    ch = EEPROM.read(bytesRead);          // read next byte from eeprom
    det_name[bytesRead] = ch;               // store it into the user buffer
    bytesRead++;                          // increment byte counter

    while ( (ch != 0x00) && (bytesRead < 40) && ((bytesRead) <= 511) ) 
    {
        ch = EEPROM.read(bytesRead);
        det_name[bytesRead] = ch;           // store it into the user buffer
        bytesRead++;                      // increment byte counter
    }
    if ((ch != 0x00) && (bytesRead >= 1)) {det_name[bytesRead - 1] = 0;}
    return true;
}


void get_time() 
{
  unsigned long int OLED_t1             = micros();
  
  //display.setCursor(0, 0);
  //display.clearDisplay();
  //display.print(F("Total Count: "));
  //display.println(count);
  //display.print(F("Uptime: "));

  int minutes                 = ((interrupt_timer - start_time) / 1000 / 60) % 60;
  int seconds                 = ((interrupt_timer - start_time) / 1000) % 60;
  char min_char[4];
  char sec_char[4];
  
  sprintf(min_char, "%02d", minutes);
  sprintf(sec_char, "%02d", seconds);

  //display.println((String) ((interrupt_timer - start_time) / 1000 / 3600) + ":" + min_char + ":" + sec_char);

  if (count == 0) {
    display.println("Starting: "+(String)detector_name);
    }

  display.display();
  
  total_deadtime                      += (micros() - OLED_t1 +73)/1000.;
}


void timerIsr() 
{
  interrupts();
  interrupt_timer                       = millis();
  if (waiting_for_interupt == 1){
      total_deadtime += (millis() - waiting_t1);}
  waiting_for_interupt = 0;
  if (OLED == 1){
      get_time();}
}


// This function converts the measured ADC value to a SiPM voltage via the calibration array
float get_sipm_voltage(float adc_value)
{
  float voltage = 0;
  for (int i = 0; i < (sizeof(cal)/sizeof(float)); i++) {
    voltage += cal[i] * pow(adc_value,(sizeof(cal)/sizeof(float)-i-1));
    }
    return voltage;
}



void setup() {
  // set up our numbers
  float temperatureC = (((analogRead(A3)+analogRead(A3)+analogRead(A3))/3. * (3300./1024)) - 500.)/10. ;
  srand(temperatureC);
  Serial.begin(9600);
  Number *temp = malloc(sizeof(Number));
  head->num = rand() %10;
  head->roll = 1;
  head->next = temp;

  temp->num = rand() %10;
  temp->roll = 1;
  temp->next = NULL;

  for(int i = 0; i <8 ; i++)  {

      Number *number = malloc(sizeof(Number));
      number->num = rand() %10;
      number->roll =1;
      temp->next = number;
      temp = number;
      temp->next = NULL;
    
    } 


  
  analogReference (EXTERNAL);
  ADCSRA &= ~(bit (ADPS0) | bit (ADPS1) | bit (ADPS2));  // clear prescaler bits
  ADCSRA |= bit (ADPS0) | bit (ADPS1);                   // Set prescaler to 8
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);                               
  pinMode(3, OUTPUT);
  pinMode(6, INPUT);
  if (digitalRead(6) == HIGH) {
      SLAVE = 1;
      MASTER = 0;
      digitalWrite(3,HIGH);
      delay(1000);}

  else{
      delay(10);
      MASTER = 1;
      SLAVE = 0;
      pinMode(6, OUTPUT);
      digitalWrite(6, HIGH);}

  if (OLED == 1){
      display.setRotation(2);         // Upside down screen (0 is right-side-up)
      OpeningScreen();                // Run the splash screen on start-up
      delay(2000);                    // Delay some time to show the logo, and keep the Pin6 HIGH for coincidence
      display.setTextSize(1);}

  else {delay(2000);}
  digitalWrite(3,LOW);
  if (MASTER == 1) {digitalWrite(6, LOW);}

  Serial.println(F("##########################################################################################"));
  Serial.println(F("### CosmicWatch: The Desktop Muon Detector"));
  Serial.println(F("### Questions? saxani@mit.edu"));
  Serial.println(F("### Comp_date Comp_time Event Ardn_time[ms] ADC[0-1023] SiPM[mV] Deadtime[ms] Temp[C] Name"));
  Serial.println(F("##########################################################################################"));

  get_detector_name(detector_name);
  Serial.println(detector_name);
  get_time();
  delay(900);
  start_time = millis();
  
  Timer1.initialize(TIMER_INTERVAL);             // Initialise timer 1
  Timer1.attachInterrupt(timerIsr);              // attach the ISR routine
  
}

void increment(Number *n)  {
    if(!n)  {
        return;
      
      }
    if(n->roll ==1) {
        n->num = (n->num +1)%10;
        increment(n->next);
        return;
      }
    increment(n->next);
  
  
  }
//TODO MAKE IT DISPLAY ON SCREEN  
void show() {
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(8, 0);
    display.clearDisplay();
    Number *printer = malloc(sizeof(Number));
    printer = head;
    char numString[]="";
    char temp[8];
    while(printer) {
        fflush(stdin);
        dtostrf(printer->num,1,0,temp);   
        strcat(numString,temp);
        
        //Serial.print(printer->num);
        printer = printer->next;
  
      }
    display.print(numString);
    display.display();
    Serial.print(numString);
    display.setTextSize(1);
    display.clearDisplay();
    delay(8);
    Serial.print("\n");  
  }


void stopRoll(Number *n) {
    if(!n)  {
        return;        
      }
    if(n->roll == 1)  {
        n->roll =0;
        return;     
      }
    stopRoll(n->next);
    return;
  
  
  }


void loop()
{
  while (1){
    //run + 1 function 
    increment(head);
    show();    
    if (analogRead(A0) > SIGNAL_THRESHOLD){ 

      // Make a measurement of the pulse amplitude
      int adc = analogRead(A0);
      
      // If Master, send a signal to the Slave
      if (MASTER == 1) {
          digitalWrite(6, HIGH);
          count++;
          // here we stop one from rolling 
          stopRoll(head);
          //this is where we will make it do something e.g. stop the numbers from rolling
          keep_pulse = 1;}

      // Wait for ~8us
      analogRead(A3);

      // Wait for ~8us
      analogRead(A3);

      // If Master, stop signalling the Slave
      if (MASTER == 1) {
          digitalWrite(6, LOW);}


      
      // Measure deadtime
      measurement_deadtime = total_deadtime;
      time_stamp = millis() - start_time;
      
      
      // If you are within 15 miliseconds away from updating the OLED screen, we'll let if finish 
      if((interrupt_timer + 1000 - millis()) < 15){ 
          waiting_t1 = millis();
          waiting_for_interupt = 1;
          delay(30);
          waiting_for_interupt = 0;}

      measurement_t1 = micros();
      
      if (MASTER == 1) {
          analogWrite(3, LED_BRIGHTNESS);
          sipm_voltage = get_sipm_voltage(adc);
          last_sipm_voltage = sipm_voltage;    

      
      keep_pulse = 0;
      digitalWrite(3, LOW);
      //while(analogRead(A0) > RESET_THRESHOLD){continue;}
      total_deadtime += (micros() - measurement_t1) / 1000.;}}
  }
}







// This function reads the EEPROM to get the detector ID
