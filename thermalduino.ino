/*
	Thermalduino
	
	Arduino based thermal regulator for renewable energy systems

	(c)yann jautard <bricofoy@free.fr> 2017
*/

#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <yasm.h>
#include <btn.h>
#include <DS1307.h>
#include <SPI.h>
#include <SdFat.h>
#include <Streaming.h>

#include <avr/wdt.h> //watchdog timer

//sensor addresses EEPROM storage address
#define EEPROM_SENSOR_ADR 	0
#define SENSOR_NBR 			10
#define EEPROM_PARAM_ADR	EEPROM_SENSOR_ADR + sizeof(SensorAddress)
// arrays to hold sensor addresses and temperatures
DeviceAddress SensorAddress[SENSOR_NBR];
float T[SENSOR_NBR];
//float Offset[SENSOR_NBR];
bool TForce[SENSOR_NBR];
 
#define TEMPERATURE_RESOLUTION 9 //0,5°C sensor acccuracy.

#define PIN_ONE_WIRE_BUS A3

#define DELAY_MENU_BACK			600E3	//10 minutes
#define DELAY_MENU_EXIT_PARAM 	40E3	//40 seconds
#define DELAY_MENU_REFRESH		1500
#define DELAY_LOG_PERIOD		5E3	
#define DELAY_SDINIT			20E3
#define DELAY_SOLAR_OFF			1800E3	//30 minutes. delay before switching off
										//solar pump power

//encoder pins
#define PIN_A	6
#define PIN_B	7
#define PIN_BTN 8
char Counter=0;


#define PIN_SD_CS	SS
#define PIN_SD_ALIM 49
#define LOGFILENAME	"datalog"
#define LOGFILEEXT 	".csv"
// File system object.
SdFat Sd;
// Log file.
SdFile Logfile;
bool SdOK=false,FileOK=false;

//mixing valve movements
#define MVOPEN 		1
#define MVCLOSE 	0

//outputs
#define PIN_PWM0	10	//solar pump speed signal
#define PIN_R0		36	//solar pump
#define PIN_R1		34	//heating pump
#define PIN_R2		32	//heating water temp servo +/-
#define PIN_R3		30	//heating water temp servo on
#define PIN_R4		28	//boiler pump
#define PIN_R5		26
#define PIN_R6		24
#define PIN_R7		22


#define RELAYS_NBR 5
byte R=0;
byte RF=0;
#define BIT_R0	1
#define BIT_R1	2
#define BIT_R2	4
#define BIT_R3	8
#define BIT_R4	16
char Pwm0=0;


#define NUM_S0	3
#define NUM_S1	5
#define NUM_S2	4
#define NUM_S3	4

#define NUM_C0	5
#define NUM_C1	3

#define NUM_P0	4
#define NUM_P1	3

#define NUM_B0	3

//arrays wich store the Sx parameters, one for each section
char S0[NUM_S0],S1[NUM_S1],S2[NUM_S2],S3[NUM_S3];
//array with the numbers of elements in each of the parameter arrays
const char Sn[]={NUM_S0,NUM_S1,NUM_S2,NUM_S3};
//same thing for the Cx params
char C0[NUM_C0],C1[NUM_C1];
const char Cn[]={NUM_C0,NUM_C1};
//same thing for the Px params
char P0[NUM_P0],P1[NUM_P1];
const char Pn[]={NUM_P0,NUM_P1};
//same thing for the Bx params
char B_0[NUM_B0];
const char Bn[]={NUM_B0};
//pointer to the array of arrays storing the parameters
char *S[]={S0,S1,S2,S3};
char *C[]={C0,C1};
char *P[]={P0,P1};
char *B[]={B_0};
//arrays of strings with details for each parameter
const char *St0[]={
	"Consigne bas ballon",
	"Differentiel demarra",
	"Differentiel arret"};
const char *St1[]={	
	"PWM max %",
	"PWM min %",
	"Increment %",
	"Periode cycle s",
	"Ecart T0-T1 voulu"}; 
const char *St2[]={	
	"Demarrage periodique",
	"T mini capteur",
	"Duree impulsions s",
	"Periode (mn)"}; 
const char *St3[]={	
	"Protection surchauff",
	"T max capteur",
	"Differentiel arret",
	"T max bas ballon"}; 	
const char **St[]={St0,St1,St2,St3};

const char *Ct0[]={
	"ON/OFF",
	"T jour",
	"T nuit",
	"H jour",
	"H nuit"};
const char *Ct1[]={	
	"Tps mvmt complet s",
	"Tps manoeuvre max  s",
	"Tps manoeuvre min  s"}; 
const char **Ct[]={Ct0,Ct1};

const char *Pt0[]={
	"Periode enreg. x10s",
	"xx",
	"xxx",
	"xxxx"};
const char *Pt1[]={	
	"Delai sortie menu s",
	"xx",
	"xxx"}; 
const char **Pt[]={Pt0,Pt1};

const char *Bt0[]={	
	"T demarrage pompe",
	"differentiel arret",
	"T max bas ballon"}; 
const char **Bt[]={Bt0};

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(PIN_ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);


//Values for the chinese GY-IICLCD backpack used
LiquidCrystal_I2C lcd(0x20, 4, 5, 6, 0, 1, 2, 3, 7, NEGATIVE);

YASM menu;
YASM gettemp;
YASM datalog;
YASM solar;
YASM solarRun;
YASM heat;
YASM boiler;
BTN btn;

DS1307_t DateTime;

void setup(void)
{
  Serial.begin(9600); 
  
  wdt_enable(WDTO_4S); //4 seconds watchdog
  
  Wire.setClock(10000);
  lcd.begin(4,20);
  lcd.backlight();
  //lcd.clear();
  //lcd.print(F("Initialisation..."));
  
  RTC.start();
  
  pinMode(PIN_A,INPUT_PULLUP);  //encoder input
  pinMode(PIN_B,INPUT_PULLUP);  
  pinMode(PIN_BTN,INPUT_PULLUP);
  
  pinMode(PIN_SD_ALIM,OUTPUT);
  
  pinMode(PIN_R0,OUTPUT);
  pinMode(PIN_R1,OUTPUT);
  pinMode(PIN_R2,OUTPUT);
  pinMode(PIN_R3,OUTPUT);
  pinMode(PIN_R4,OUTPUT);
  pinMode(PIN_PWM0,OUTPUT);
  outputsWrite();
  
  
  
  loadParams();//get all the parameters from eeprom
  
  menu.next(menu_start); 
  gettemp.next(gettemp_start);
  datalog.next(datalog_start);
  solar.next(solar_off);
}

void loop(void) 
{
	wdt_reset(); //clear watchdog timer
	R &= RF; //erase all output bits except forced ones
	RTC.get(&DateTime,true);
	btn.update(!digitalRead(PIN_BTN)); // ! because btn switch to gnd
	encoderRead();
	gettemp.run();
	
	solar.run();
	heat.run();
	boiler.run();
	menu.run(); //menu must be called after the automation SMs because if not 
				//R values are not set and it always display 0
	datalog.run();
	//R |= RF; //
	outputsWrite();
}

//from http://jeelabs.org/2011/05/22/atmega-memory-use/
int freeRam () {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

void setOutput(byte pin, bool state)
{
	if(!(RF&pin))
		if(state) R |= pin; //set the <pin> bit
		else R &= ~pin; //unset the <pin> bit		
}

void moveMixValve(bool direction)
{
	setOutput(PIN_R2, direction);
	setOutput(PIN_R3, 1);
}


void outputsWrite()
{
	byte pwm=0;
	
	// ! because relays are active low
	digitalWrite(PIN_R0,!(R&BIT_R0));
	digitalWrite(PIN_R1,!(R&BIT_R1));
	digitalWrite(PIN_R2,!(R&BIT_R2));
	digitalWrite(PIN_R3,!(R&BIT_R3));
	digitalWrite(PIN_R4,!(R&BIT_R4));
	
	if(RF&BIT_R0) 					//if output is forced just remap the % value
		pwm=map(Pwm0,0,100,0,255);  //to the 0->255 needed for analogWrite().
	else //if output is not forced, we need to adapt to what the pump want
		if(R&BIT_R0)
			if(Pwm0==0) 
				pwm=247; //=97%. This is the value needed to stop the OEG pump
			else 
				pwm=map(map(Pwm0,1,100,84,10),0,100,0,255);
				//because for OEG pumps, min->max speed is corresponding to 
				//84%(min)->10%(max) pwm duty cycle. This is the first map().
				//And then we need this % value to get re-maped to the 0->255
				//needed for analogWrite(). This is the second map().
		else pwm=0; //if relay is of, turn off PWM output
	//then eventually write the PWM value to the pin
	analogWrite(PIN_PWM0,pwm);
}

void loadSensorsAddresses()
{
	EEPROM.get(EEPROM_SENSOR_ADR,SensorAddress);
}

void saveParams()
{
	int address = EEPROM_PARAM_ADR;
	EEPROM.put(address,S0);
	address += sizeof(S0);
	EEPROM.put(address,S1);
	address += sizeof(S1);
	EEPROM.put(address,S2);
	address += sizeof(S2);
	EEPROM.put(address,S3);
	address += sizeof(S3);
	
	EEPROM.put(address,C0);
	address += sizeof(C0);	
	EEPROM.put(address,C1);
	address += sizeof(C1);
	
	EEPROM.put(address,P0);
	address += sizeof(P0);
	EEPROM.put(address,P1);
	address += sizeof(P1);
	
	EEPROM.put(address,B_0);
	//address += sizeof(B_0);	
}
void loadParams()
{
	int address = EEPROM_PARAM_ADR;
	EEPROM.get(address,S0);
	address += sizeof(S0);
	EEPROM.get(address,S1);
	address += sizeof(S1);
	EEPROM.get(address,S2);
	address += sizeof(S2);
	EEPROM.get(address,S3);
	address += sizeof(S3);

	EEPROM.get(address,C0);
	address += sizeof(C0);	
	EEPROM.get(address,C1);
	address += sizeof(C1);	
	
	EEPROM.get(address,P0);
	address += sizeof(P0);
	EEPROM.get(address,P1);	
	address += sizeof(P1);
	
	EEPROM.get(address,B_0);
	//address += sizeof(B_0);
}

void setSensorsResolution()
{
	for (byte i=0;i<SENSOR_NBR; i++)
	{
		sensors.setResolution(SensorAddress[i], TEMPERATURE_RESOLUTION);
	}
}

void encoderRead()
{
	static byte old_states=0;
	static char state_sub=0;
	
	byte pin_states = (digitalRead(PIN_A)<<1);
	pin_states += digitalRead(PIN_B);
	
	// state is now 0, 1, 2 or 3.
	if (pin_states!=old_states)
	{
		// Exor the old & new states to determine the rotation direction.
		char inc = ((pin_states>>1)^old_states)&0x01;
		if (inc==0) inc = -1;
		old_states = pin_states;
		
		// Reset on change of direction.
		if ((inc<0 && state_sub>0) || (inc>0 && state_sub<0)) state_sub = 0;
		
		state_sub += inc;
		if (state_sub<=-4 || state_sub>=4)
		{
			state_sub -= (inc<<2);
			Counter += inc;
		}
	}
}

char encoderCount()
{
	char retval=Counter;
	Counter=0;
	return retval;
}

void printT(byte i)
{
	if(TForce[i]) lcd<<"F";
	else 
		if(T[i]==-127) 
		{
			lcd<<" --.-";
			return; 
		}
	if(T[i]>=0) 
	{
		if(!TForce[i]&&T[i]<100) lcd<<F(" ");
		if(T[i]<10) lcd<<F(" ");
	}
	else if(T[i]>-10) lcd<<F(" "); 
	
	lcd.print(T[i],1);
}

void printR(byte bit)
{
	if(RF&bit) lcd<<F("F");
	else lcd<<F(" ");
	lcd<<((R&bit)&&1);
}
	

////////////////////////gettemp state machine///////////////////////////////////
void gettemp_start()
{
	sensors.begin(); // Start up the dallas library
	sensors.setWaitForConversion(false); //don't wait !
	loadSensorsAddresses(); //fill the addresses array with the values in EEPROM
	setSensorsResolution();
	gettemp.next(gettemp_request);
}	

void gettemp_request()
{
	sensors.requestTemperatures();
	gettemp.next(gettemp_wait);
}

void gettemp_wait()
{	//we need to wait that time for conversion to finish
	if (gettemp.elapsed(750/(1<<(12-TEMPERATURE_RESOLUTION))))
		gettemp.next(gettemp_read);
}

void gettemp_read()
{
	byte i=gettemp.runCount();
	if (i<SENSOR_NBR)
	{
		if(!TForce[i])
			T[i] = sensors.getTempC(SensorAddress[i]);
	}
	else gettemp.next(gettemp_request);
}

/////////////////////////datalog state machine//////////////////////////////////
void datalog_wait()
{
	if(datalog.elapsed(P[0][0]*1E4)) //delay unit is x10s so we *1000 to get ms
		datalog.next(datalog_write);
}

void datalog_wait_card()
{
	digitalWrite(PIN_SD_ALIM,LOW);
	
	if(datalog.elapsed(DELAY_SDINIT))
		datalog.next(datalog_start);
}

void datalog_start()
{
	digitalWrite(PIN_SD_ALIM,HIGH);	//turn on card power
	if(!datalog.elapsed(500)) return; //leave some time for the card to power up
	
	SdOK=Sd.begin(PIN_SD_CS, SPI_HALF_SPEED);
	if(!SdOK) //something gone wrong so retry later
	{
		datalog.next(datalog_wait_card);
		return;
	}
 	
/*	FileOK=Logfile.open(LOGFILENAME, O_RDWR | O_CREAT | O_AT_END);
	if(!FileOK)
	{
		datalog.next(datalog_wait_card);
		return;
	}

/* 	Logfile<<_endl<<F("date;");
	for(byte i=0;i<SENSOR_NBR;i++) Logfile << F("F;T")<<i<<F(";");
	Logfile<<F("Pwm;");
	for(byte i=0;i<RELAYS_NBR;i++) Logfile << F("F;R")<<i<<F(";"); 
	Logfile<<_endl;
	Logfile.close();*/
 	datalog.next(datalog_write);				
}



void datalog_write()
{
 	String logfilename;
 	
	logfilename+=LOGFILENAME;
	logfilename+=DateTime.year;
	logfilename+='-';
	logfilename+=DateTime.month;
	logfilename+=LOGFILEEXT;
	FileOK=Logfile.open(logfilename.c_str(), O_RDWR | O_CREAT | O_AT_END);
	if(!FileOK)
	{
		SdOK=false;
		datalog.next(datalog_wait_card);
		return;
	}
	
	Logfile<<RTC.getS(DS1307_STR_DATE,0)<<F(" ")<<RTC.getS(DS1307_STR_TIME,0)<<F(";");
	
	for(byte i=0;i<SENSOR_NBR;i++) 
	{
		if(TForce[i]) Logfile<<F("F");
		Logfile << F(";")<<T[i] <<F(";");
	}
	
	Logfile<<(byte)Pwm0<<F(";");
	
	(RF&BIT_R0)?Logfile<<F("F;"):Logfile<<F(";");
	(R&BIT_R0)?Logfile<<F("1;"):Logfile<<F("0;");
	(RF&BIT_R1)?Logfile<<F("F;"):Logfile<<F(";");
	(R&BIT_R1)?Logfile<<F("1;"):Logfile<<F("0;");
	(RF&BIT_R2)?Logfile<<F("F;"):Logfile<<F(";");
	(R&BIT_R2)?Logfile<<F("1;"):Logfile<<F("0;");	
	(RF&BIT_R3)?Logfile<<F("F;"):Logfile<<F(";");
	(R&BIT_R3)?Logfile<<F("1;"):Logfile<<F("0;");
	(RF&BIT_R4)?Logfile<<F("F;"):Logfile<<F(";");
	(R&BIT_R4)?Logfile<<F("1;"):Logfile<<F("0;");

	Logfile<<_endl;
	Logfile.close();
	
	datalog.next(datalog_wait);
}      

	

//////////////////////////menu state machine////////////////////////////////////
char Pos,Page;

void menu_start()
{
	
	if (menu.isFirstRun())
	{
		lcd.clear();
		lcd.print(F("Ballon"));
		lcd.setCursor(0,1); lcd.print(F("Capteur"));
		lcd.setCursor(0,2); lcd.print(F("Int       Ext"));
		lcd.setCursor(0,3); lcd.print(F("Chauf."));
		
	}
 	
	if(menu.periodic(DELAY_MENU_REFRESH)) 
	{
		lcd.setCursor(7,0); printT(1);
		lcd.setCursor(13,0); printT(3);

		lcd.setCursor(7,1); printT(0);
		lcd.setCursor(15,1); if(RF&BIT_R0) lcd<<F("F"); lcd<<(byte)Pwm0<<F("% ");
		
		lcd.setCursor(3,2); printT(7);
		lcd.setCursor(13,2); printT(6);
		
		lcd.setCursor(7,3); C[0][0]?lcd<<F("ON "):lcd<<F("OFF");
		
		lcd.setCursor(12,3); lcd<<RTC.getS(DS1307_STR_TIME,0);
		
		
		if(!FileOK)
		{
			lcd.setCursor(19,0);
			lcd<<F("E");
		}
		else
		{
			lcd.setCursor(19,0);
			lcd<<F(" ");
		}
		
		//lcd.setCursor(15,3); lcd<<freeRam();
	}
 
	if(encoderCount()>0)
		menu.next(menu_start2);
	
	if(btn.state(BTN_CLICK)) 
		menu.next(menu_param);	
	
	if(btn.state(BTN_LONGCLICK)) 
		menu.next(menu_start);
}

void menu_start2()
{
	
	if (menu.isFirstRun())
	{
		lcd.clear();
		lcd.print(F("T0:       T9:"));
		lcd.setCursor(0,1); lcd.print(F("T1:       T2:"));
		lcd.setCursor(0,2); lcd.print(F("T3:"));
		lcd.setCursor(0,3); lcd.print(F("R0:"));
		
	}	
	
	if(menu.periodic(DELAY_MENU_REFRESH)) 
	{
		lcd.setCursor(3,0); printT(0); lcd.setCursor(13,0); printT(9);
		lcd.setCursor(3,1); printT(1); lcd.setCursor(13,1); printT(2); 
		lcd.setCursor(3,2); printT(3);
		lcd.setCursor(3,3); printR(BIT_R0); lcd<<" "<<(byte)Pwm0<<"%"; 
	}
	
	if(Counter<0 || menu.elapsed(DELAY_MENU_BACK))
		menu.next(menu_start);
	
	if(encoderCount()>0)
		menu.next(menu_start3);	
	
	if(btn.state(BTN_CLICK)) 
		menu.next(menu_param);
	
	if(btn.state(BTN_LONGCLICK)) 
		menu.next(menu_forceT);

}

void menu_start3()
{
	
	if (menu.isFirstRun())
	{
		lcd.clear();
		lcd.print(F("T4:       T5:"));
		lcd.setCursor(0,1); lcd.print(F("T6:       T7:"));
		lcd.setCursor(0,2); lcd.print(F("R1:   R2:   R3:"));
		lcd.setCursor(0,3); lcd.print(F("T8:         R4:"));
		
	}
	
	if(menu.periodic(DELAY_MENU_REFRESH)) 
	{
		lcd.setCursor(3,0); printT(4); lcd.setCursor(13,0); printT(5);
		lcd.setCursor(3,1); printT(6); lcd.setCursor(13,1); printT(7);
		lcd.setCursor(3,2); printR(BIT_R1); 
		lcd.setCursor(9,2); printR(BIT_R2); 
		lcd.setCursor(15,2); printR(BIT_R3);
		lcd.setCursor(3,3); printT(8); 
		lcd.setCursor(15,3); printR(BIT_R4);
	}
	
	if(encoderCount()<0)
		menu.next(menu_start2);	

	if(menu.elapsed(DELAY_MENU_BACK))
		menu.next(menu_start);	
	
	if(btn.state(BTN_CLICK)) 
		menu.next(menu_start3);	
	
	if(btn.state(BTN_LONGCLICK)) 
		menu.next(menu_forceR);
}

	
void menu_param()
{	
	if (menu.isFirstRun()) 
	{
		lcd.clear();
		lcd<<(char)126<<F("Reglages horloge");
		lcd.setCursor(1,1); lcd<<F("Param. sondes");
		lcd.setCursor(1,2); lcd<<F("Param. solaire");
		lcd.setCursor(1,3); lcd<<F("Param. chauffage");
		Pos=0;
	}


	if (Counter!=0)
	{
		lcd.setCursor(0,Pos); lcd.print(F(" "));
		Pos+=encoderCount();
		if (Pos>3) menu.next(menu_param2);
		if (Pos<0) Pos=0;
		lcd.setCursor(0,Pos); lcd<<(char)126;
	}
	if(btn.state(BTN_CLICK))
		switch (Pos)
		{
			case 0 : { menu.next(menu_setclock); break; }
			case 1 : { menu.next(menu_setsensors); break; }
			case 2 : { menu.next(menu_setS); break; }
			case 3 : { menu.next(menu_setC); break; }
		}
				
	if(btn.state(BTN_LONGCLICK) || menu.elapsed(DELAY_MENU_EXIT_PARAM))
		menu.next(menu_start);
}

void menu_param2()
{	
	if (menu.isFirstRun()) 
	{
		lcd.clear();
		lcd<<(char)126<<F("Param. bouilleur");
		lcd.setCursor(1,1); lcd<<F("Param. systeme");
		lcd.setCursor(1,2); lcd<<F("xxx");
		lcd.setCursor(1,3); lcd<<F("Manuel");
		Pos=0;
	}


	if (Counter!=0)
	{
		lcd.setCursor(0,Pos); lcd.print(F(" "));
		Pos+=encoderCount();
		if (Pos>3) Pos=3;
		if (Pos<0) menu.next(menu_param);
		lcd.setCursor(0,Pos); lcd<<(char)126;
	}
	if(btn.state(BTN_CLICK))
		switch (Pos)
		{
			case 0 : { menu.next(menu_setB); Pos=0; break; }
			case 1 : { menu.next(menu_setP); Pos=0; break; }
			case 2 : { Pos=0; break; }
			case 3 : { menu.next(menu_param_manu); Pos=0; break; }
		}
				
	if(btn.state(BTN_LONGCLICK) || menu.elapsed(DELAY_MENU_EXIT_PARAM))
		menu.next(menu_start);
}

void menu_param_manu()
{	
	if (menu.isFirstRun()) 
	{
		lcd.clear();
		lcd<<(char)126<<F("Forcage relais");
		lcd.setCursor(1,1); lcd<<F("Forcage temperature");
		lcd.setCursor(1,2); lcd<<F("xxx");
		lcd.setCursor(1,3); lcd<<F("test PWM");
		Pos=0;
	}


	if (Counter!=0)
	{
		lcd.setCursor(0,Pos); lcd.print(F(" "));
		Pos+=encoderCount();
		if (Pos>3) Pos=3;
		if (Pos<0) Pos=0;
		lcd.setCursor(0,Pos); lcd<<(char)126;
	}
	if(btn.state(BTN_CLICK))
		switch (Pos)
		{
			case 0 : { menu.next(menu_forceR); Pos=0; break; }
			case 1 : { menu.next(menu_forceT); Pos=0; break; }
			case 2 : { Pos=0; break; }
			case 3 : { menu.next(menu_testPWM); Pos=0; break; }
		}
				
	if(btn.state(BTN_LONGCLICK))
		menu.next(menu_param2);
	
	if(menu.elapsed(DELAY_MENU_EXIT_PARAM))
		menu.next(menu_start);
}

void outset(byte bit, char count)
{
	if(count==0) return;
	
	char mode;
	mode=(((RF&bit)&&1)<<1)|((R&bit)&&1);//0=auto,0 1=auto,1 2=forced,0 3=forced,1
	
	if(mode==0) mode=1;
	mode+=count;
	if(mode>3) mode=1;
	if(mode<1) mode=3;

	switch (mode)
	{
		case 1 : { RF &= ~bit; break;}
		case 2 : { RF |= bit; R &= ~bit; break;}
		case 3 : { RF |= bit; R |= bit; break;}
	}
}

void Rprintset()
{
	lcd.setCursor(3,1); 
	if(RF&BIT_R0) {lcd<<F("    ");lcd.setCursor(3,1); lcd<<(R&BIT_R0);}
	else lcd << F("AUTO");
	lcd.setCursor(8,1); lcd<<(byte)Pwm0<<F("%  ");
	lcd.setCursor(3,2); 
	if(RF&BIT_R1) {lcd<<F("    ");lcd.setCursor(3,2); lcd<<((R&BIT_R1)&&1);}
	else lcd << F("AUTO");
	lcd.setCursor(3,3); 
	if(RF&BIT_R2) {lcd<<F("    ");lcd.setCursor(3,3); lcd<<((R&BIT_R2)&&1);}
	else lcd << F("AUTO");
	lcd.setCursor(15,2); 
	if(RF&BIT_R3) {lcd<<F("    ");lcd.setCursor(15,2); lcd<<((R&BIT_R3)&&1);}
	else lcd << F("AUTO");
	lcd.setCursor(15,3); 
	if(RF&BIT_R4) {lcd<<F("    ");lcd.setCursor(15,3); lcd<<((R&BIT_R4)&&1);}
	else lcd << F("AUTO");
	
	switch (Pos)
	{
		case 0 : { lcd.setCursor(3,1); outset(BIT_R0, encoderCount()); break; }
		case 1 : { 
			lcd.setCursor(8,1); 
			if(RF&BIT_R0) 
			{
				Pwm0+=(encoderCount()*5);
				if(Pwm0<0) Pwm0=0;
				if(Pwm0>100) Pwm0=100;
			}
			else encoderCount();
			break; }
		case 2 : { lcd.setCursor(3,2); outset(BIT_R1, encoderCount()); break; }
		case 3 : { lcd.setCursor(3,3); outset(BIT_R2, encoderCount()); break; }
		case 4 : { lcd.setCursor(15,2); outset(BIT_R3, encoderCount()); break; }
		case 5 : { lcd.setCursor(15,3); outset(BIT_R4, encoderCount()); break; }
	}
}
//TODO: changerle comportement pour etre consistant avec le menu et le forcage
//des températures: tourner pour défiler, click pour éditer et doubleclick auto
void menu_forceR()
{
 	if (menu.isFirstRun()) 
	{
		lcd.clear();
		Pos=0;
		encoderCount();
		lcd<<F("Forcage relais");
		lcd.setCursor(0,1); lcd<<F("R0:");
		lcd.setCursor(0,2); lcd<<F("R1:         R3:");
		lcd.setCursor(0,3); lcd<<F("R2:         R4:");
		lcd.blink();
		Rprintset();
	}
	
	if(btn.state(BTN_LONGCLICK))
	{
		lcd.noBlink();
		menu.next(menu_start);
	}
	
	if(btn.state(BTN_CLICK))
	{
		Pos++;
		if (Pos>5) Pos=0;
		Rprintset();
	}

	//redraw twice to get correct value on screen, because value is changed after
	//redraw in Rprintset()
	if(Counter!=0) {Rprintset();Rprintset();} 
}

void Tprintset()
{
	lcd.clear();
	//-33 is ° on this lcd
	lcd<<F("Forcage temp.(")<<(char)-33<<F("C)")<<(Page+1)<<F("/");
	lcd<<(SENSOR_NBR/6+((SENSOR_NBR%6)&&1)); 
	
	for(byte i=0;i<6;i++)
	{
		if(Page*6+i>=SENSOR_NBR) break;
		if(i<3) lcd.setCursor(0,i+1); 
		else lcd.setCursor(10,i-2);
		lcd<<F("T")<<(Page*6+i)<<F(":"); 
		if(!TForce[Page*6+i]) lcd<<F("AUTO "); 
		else printT(Page*6+i);
	}
	
	if((Pos-Page*6)<3) lcd.setCursor(1,Pos+1-Page*6);
	else lcd.setCursor(11,Pos-2-Page*6);
	if(Pos>8) lcd.moveCursorRight();
}

void menu_forceT_edit()
{
	if(menu.isFirstRun())
	{
		TForce[Pos] |= 1; //set Force bit 
		if(T[Pos]==-127) T[Pos]=0;
		Tprintset(); lcd.moveCursorRight(); lcd.moveCursorRight();		
	}
		
	switch (btn.state())
	{
		case BTN_CLICK : { menu.next(menu_forceT); return; }
		case BTN_LONGCLICK : { lcd.noBlink(); menu.next(menu_param2); return; }
		case BTN_DOUBLECLICK : { TForce[Pos] &= 0; //unset the Force bit, back to AUTO
			menu.next(menu_forceT);	return;	}
	}

	//if no button action just edit the value
	if(Counter!=0) 
	{
		T[Pos]+=(encoderCount()*0.5);
		Tprintset(); lcd.moveCursorRight(); lcd.moveCursorRight();
	}
}


void menu_forceT()
{
 	if (menu.isFirstRun()) 
	{
		//Pos=Page=0; 
		encoderCount();
		lcd.blink();
		Tprintset();
	}

	switch (btn.state())
	{
		case BTN_CLICK : { menu.next(menu_forceT_edit); break; }
		case BTN_LONGCLICK : { lcd.noBlink(); menu.next(menu_start); break; }
		//case BTN_DOUBLECLICK : break;
	}

	if(Counter!=0) 
	{
		Pos+=encoderCount();
		if (Pos>=SENSOR_NBR) Pos=SENSOR_NBR-1;
		if (Pos<0) Pos=0;
		Page=(int)Pos/6;
		Tprintset();
	}
} 

void timeprintset()
{
	lcd.setCursor(6,1); 
	if(DateTime.hour<10) lcd << F("0");
	lcd << DateTime.hour << F(":");
	if(DateTime.minute<10) lcd << F("0");
	lcd<<DateTime.minute<<F(":");
	if(DateTime.second<10) lcd << F("0");
	lcd<<DateTime.second;
	
	lcd.setCursor(5,2);
	if(DateTime.day<10) lcd << F("0");
	lcd<<DateTime.day<<F("/");
	if(DateTime.month<10) lcd << F("0");
	lcd<<DateTime.month<<F("/")<<DateTime.year;
	
	switch (Pos)
	{
		case 0 : { 	
			lcd.setCursor(6,1); 
			RTC.set(DS1307_HR,RTC.get(DS1307_HR, 0)+encoderCount()); 
			break; }
		case 1 : { 
			lcd.setCursor(9,1); 
			RTC.set(DS1307_MIN,RTC.get(DS1307_MIN, 0)+encoderCount());
			break; }
		case 2 : { 
			lcd.setCursor(12,1); 
			RTC.set(DS1307_SEC,RTC.get(DS1307_SEC,1)+encoderCount());
			break; }
		case 3 : { 
			lcd.setCursor(5,2); 
			RTC.set(DS1307_DOM,RTC.get(DS1307_DOM,0)+encoderCount()); 
			break; }
		case 4 : { 
			lcd.setCursor(8,2); 
			RTC.set(DS1307_MTH,RTC.get(DS1307_MTH,0)+encoderCount()); 
			break; }	
		case 5 : { 
			lcd.setCursor(13,2); 
			RTC.set(DS1307_YR,((RTC.get(DS1307_YR,0)+encoderCount())-2000) );
			break; }
	}
}
void menu_setclock()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
		lcd.print(F("Reglage de l'heure :"));
		Pos=0;
		encoderCount();
		lcd.blink();
		timeprintset();
	}
	
	//refresh display when no user input only each 4sec because faster redraw
	//takes too much time and then encoder is not responsive
	if (menu.periodic(4E3)) timeprintset();
	
	if(btn.state(BTN_LONGCLICK))
	{
		lcd.noBlink();
		menu.next(menu_param);
	}
	
	if(btn.state(BTN_CLICK))
	{
		Pos++;
		if (Pos>5) Pos=0;
		timeprintset();
	}
	
	//redraw twice to get correct value on screen, because value is changed after
	//redraw in timeprintset()
	if(Counter!=0) {timeprintset();timeprintset();}
}

void menu_setsensors()
{
	if (menu.isFirstRun()) 
	{
		gettemp.stop(); //stops main temp read process so it will not interfere
		lcd.clear();

		lcd<<F("Enregistrement sonde");
		lcd.setCursor(5,1); lcd<< F("T")<<(byte)Pos;
		lcd.setCursor(0,2); lcd<< F("Raccorder uniquement");
		lcd.setCursor(0,3); lcd<< F("la sonde a modifier");
		
		lcd.setCursor(6,1); lcd.blink();
		
	}	
	
	if (Counter!=0) 
	{
		Pos+=encoderCount();
		if(Pos<0) Pos=0;
		if(Pos>SENSOR_NBR-1) Pos=SENSOR_NBR-1;
		lcd.setCursor(6,1); lcd<<(byte)Pos;
		if(Pos<10) lcd<<F(" ");
	}
	
	if(btn.state(BTN_CLICK))
		menu.next(menu_setsensors2);	
	
	if(btn.state(BTN_LONGCLICK))
	{
		lcd.noBlink();
		menu.next(menu_param);
		gettemp.next(gettemp_start);
	}
}

void menu_setsensors2()
{
	if(menu.periodic(1500))
	{
		lcd.clear();
		lcd<<F("Sonde T")<<(byte)Pos;
		lcd.setCursor(0,1);
		sensors.getDeviceCount();
		if (!sensors.getAddress(SensorAddress[Pos], 0)) lcd<<F("erreur sonde");
		else {
			sensors.setResolution(SensorAddress[Pos], TEMPERATURE_RESOLUTION);
			sensors.requestTemperatures();
					
			//print sensor address
			for (uint8_t i = 0; i < 8; i++){
				// zero pad the address if necessary
				if (SensorAddress[Pos][i] < 16) lcd<<F("0");
				lcd<<_HEX(SensorAddress[Pos][i]);
			}
			delay(750/(1<<(12-TEMPERATURE_RESOLUTION)));
			lcd.setCursor(12,0);
			lcd<<sensors.getTempC(SensorAddress[Pos]);
		}
	}
	
	if(btn.state(BTN_CLICK))
		menu.next(menu_setsensors3);	
	
	if(btn.state(BTN_LONGCLICK))
		menu.next(menu_setsensors);	
}

void menu_setsensors3()
{
	if (menu.isFirstRun()) 	
	{
		lcd.setCursor(0,2); 
		lcd<<F("ENREGISTRER ?");
	}
	
	if(btn.state(BTN_CLICK))
	{
		lcd.setCursor(0,3); lcd<<F("ENREGISTREMENT...");
		delay(700);
		EEPROM.put(EEPROM_SENSOR_ADR+(sizeof(DeviceAddress)*(Pos)), SensorAddress[Pos]);
		menu.next(menu_setsensors);
	}
	
	if(btn.state(BTN_LONGCLICK))
		menu.next(menu_setsensors);
}
		
			

void arrow(byte max=6)
{
		if (Pos>2) lcd.setCursor(10,Pos-2);
		else lcd.setCursor(0,Pos+1); 
		lcd.print(F(" "));
		Pos+=encoderCount();
		if (Pos>(max-1)) Pos=0;
		if (Pos<0) Pos=max-1;
		if (Pos>2) lcd.setCursor(10,Pos-2); 
		else lcd.setCursor(0,Pos+1); 
		lcd<<(char)126;
}

void menu_setS()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
		lcd<<F("Parametres solaire");
		lcd.setCursor(1,1); lcd<< F("S0 base");
		lcd.setCursor(1,2); lcd<< F("S1 pompe");
		lcd.setCursor(1,3); lcd<< F("S2 avance");
		lcd.setCursor(11,1); lcd<<F("S3 protec");
		Pos=0;Page=0;
		arrow(4);		
	}


	if (Counter!=0) arrow(4);
	
	if(btn.state(BTN_CLICK))
	{
		Page=Pos;
		Pos=0;
		menu.next(menu_setSxx);
	}
				
	if(btn.state(BTN_LONGCLICK) )
		menu.next(menu_param);
}

void menu_setC()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
		lcd<<F("Parametres chauffage");
		lcd.setCursor(1,1); lcd<< F("C0 base");
		lcd.setCursor(1,2); lcd<< F("C1 vanne");
		//lcd.setCursor(1,3); lcd<< F("S3 avance");
		//lcd.setCursor(11,1); lcd<<F("S4 protec");
		Pos=0;Page=0;
		arrow(2);		
	}


	if (Counter!=0) arrow(2);
	
	if(btn.state(BTN_CLICK))
	{
		Page=Pos;
		Pos=0;
		menu.next(menu_setCxx);
	}
				
	if(btn.state(BTN_LONGCLICK) )
		menu.next(menu_param);
}

void menu_setP()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
		lcd<<F("Parametres systeme");
		lcd.setCursor(1,1); lcd<< F("P0 enreg.");
		lcd.setCursor(1,2); lcd<< F("P1 interface");
		//lcd.setCursor(1,3); lcd<< F("S3 avance");
		//lcd.setCursor(11,1); lcd<<F("S4 protec");
		Pos=0;Page=0;
		arrow(2);		
	}


	if (Counter!=0) arrow(2);
	
	if(btn.state(BTN_CLICK))
	{
		Page=Pos;
		Pos=0;
		menu.next(menu_setPxx);
	}
				
	if(btn.state(BTN_LONGCLICK) )
		menu.next(menu_param);
}

void menu_setB()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
		lcd<<F("Parametres bouilleur");
		lcd.setCursor(1,1); lcd<< F("B0 base");
		Pos=0;Page=0;
		arrow(1);		
	}

	if (Counter!=0) arrow(1);
	
	if(btn.state(BTN_CLICK))
	{
		Page=Pos;
		Pos=0;
		menu.next(menu_setBxx);
	}
				
	if(btn.state(BTN_LONGCLICK) )
		menu.next(menu_param);
}

void explainS()
{
	lcd.setCursor(0,0);
	lcd<<F("                    ");
	lcd.setCursor(0,0);
	lcd<<St[Page][Pos];
}

void explainC()
{
	lcd.setCursor(0,0);
	lcd<<F("                    ");
	lcd.setCursor(0,0);
	lcd<<Ct[Page][Pos];
}

void explainP()
{
	lcd.setCursor(0,0);
	lcd<<F("                    ");
	lcd.setCursor(0,0);
	lcd<<Pt[Page][Pos];
}

void explainB()
{
	lcd.setCursor(0,0);
	lcd<<F("                    ");
	lcd.setCursor(0,0);
	lcd<<Bt[Page][Pos];
}

void menu_setSxx()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
				
		for(byte i=0;i<Sn[Page];i++)
		{
			if(i<3) lcd.setCursor(1,i+1); 
			else lcd.setCursor(11,i-2);
			lcd<<F("S")<<(int)Page<<F(".")<<i<<F(":"); 
			lcd<<(int)S[Page][i];
		}
		arrow(Sn[Page]);
		explainS();
	}


	if (Counter!=0) 
	{
		arrow(Sn[Page]);
		explainS();
	}
		
	
	if(btn.state(BTN_CLICK))
		menu.next(menu_editSxx);
				
	if(btn.state(BTN_LONGCLICK))
		menu.next(menu_setS);
}

void menu_setCxx()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
				
		for(byte i=0;i<Cn[Page];i++)
		{
			if(i<3) lcd.setCursor(1,i+1); 
			else lcd.setCursor(11,i-2);
			lcd<<F("C")<<(int)Page<<F(".")<<i<<F(":"); 
			lcd<<(int)C[Page][i];
		}
		arrow(Cn[Page]);
		explainC();
	}


	if (Counter!=0) 
	{
		arrow(Cn[Page]);
		explainC();
	}
		
	
	if(btn.state(BTN_CLICK))
		menu.next(menu_editCxx);
				
	if(btn.state(BTN_LONGCLICK))
		menu.next(menu_setC);
}

void menu_setPxx()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
				
		for(byte i=0;i<Pn[Page];i++)
		{
			if(i<3) lcd.setCursor(1,i+1); 
			else lcd.setCursor(11,i-2);
			lcd<<F("P")<<(int)Page<<F(".")<<i<<F(":"); 
			lcd<<(int)P[Page][i];
		}
		arrow(Pn[Page]);
		explainP();
	}


	if (Counter!=0) 
	{
		arrow(Pn[Page]);
		explainP();
	}
		
	
	if(btn.state(BTN_CLICK))
		menu.next(menu_editPxx);
				
	if(btn.state(BTN_LONGCLICK))
		menu.next(menu_setP);
}

void menu_setBxx()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
				
		for(byte i=0;i<Bn[Page];i++)
		{
			if(i<3) lcd.setCursor(1,i+1); 
			else lcd.setCursor(11,i-2);
			lcd<<F("B")<<(int)Page<<F(".")<<i<<F(":"); 
			lcd<<(int)B[Page][i];
		}
		arrow(Bn[Page]);
		explainB();
	}


	if (Counter!=0) 
	{
		arrow(Bn[Page]);
		explainB();
	}
		
	
	if(btn.state(BTN_CLICK))
		menu.next(menu_editBxx);
				
	if(btn.state(BTN_LONGCLICK))
		menu.next(menu_setB);
}

void menu_editSxx()
{
	if (menu.isFirstRun()) 
	{
		arrow();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.blink();
	}	
	
	if (Counter!=0) {
		S[Page][Pos]+=encoderCount();
		arrow();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd<<F("    ");
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd<<(int)S[Page][Pos];
	}
	
	if(btn.state(BTN_CLICK))
	{
		menu.next(menu_setSxx);
		lcd.noBlink();
		saveParams(); //store the modified value
	}
	
	if(btn.state(BTN_LONGCLICK))
	{
		menu.next(menu_setSxx);
		lcd.noBlink();
		loadParams(); //discard the modified value : get the old one from eeprom
	}
}

void menu_editCxx()
{
	if (menu.isFirstRun()) 
	{
		arrow();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.blink();
	}	
	
	if (Counter!=0) {
		C[Page][Pos]+=encoderCount();
		arrow();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd<<F("    ");
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd<<(int)C[Page][Pos];
	}
	
	if(btn.state(BTN_CLICK))
	{
		menu.next(menu_setCxx);
		lcd.noBlink();
		saveParams(); //store the modified value
	}
	
	if(btn.state(BTN_LONGCLICK))
	{
		menu.next(menu_setCxx);
		lcd.noBlink();
		loadParams(); //discard the modified value : get the old one from eeprom
	}
}

void menu_editPxx()
{
	if (menu.isFirstRun()) 
	{
		arrow();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.blink();
	}	
	
	if (Counter!=0) {
		P[Page][Pos]+=encoderCount();
		arrow();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd<<F("    ");
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd<<(int)P[Page][Pos];
	}
	
	if(btn.state(BTN_CLICK))
	{
		menu.next(menu_setPxx);
		lcd.noBlink();
		saveParams(); //store the modified value
	}
	
	if(btn.state(BTN_LONGCLICK))
	{
		menu.next(menu_setPxx);
		lcd.noBlink();
		loadParams(); //discard the modified value : get the old one from eeprom
	}
}

void menu_editBxx()
{
	if (menu.isFirstRun()) 
	{
		arrow();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.blink();
	}	
	
	if (Counter!=0) {
		B[Page][Pos]+=encoderCount();
		arrow();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd.moveCursorRight();
		lcd<<F("    ");
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd.moveCursorLeft();
		lcd<<(int)B[Page][Pos];
	}
	
	if(btn.state(BTN_CLICK))
	{
		menu.next(menu_setBxx);
		lcd.noBlink();
		saveParams(); //store the modified value
	}
	
	if(btn.state(BTN_LONGCLICK))
	{
		menu.next(menu_setBxx);
		lcd.noBlink();
		loadParams(); //discard the modified value : get the old one from eeprom
	}
}

void testPWMrefresh()
{
	lcd.setCursor(4,1); lcd<<F("            "); 
	lcd.setCursor(3,2); printR(BIT_R0);
	lcd.setCursor(4,1); lcd<<map(Pwm0,0,100,0,255)<<"  "<<(byte)Pwm0<<"%";
}

void menu_testPWM()
{
	if (menu.isFirstRun())
	{
		solar.stop();
		RF|=BIT_R0; R &= ~BIT_R0; //force R0 relay to LOW
		
		lcd.clear();
		lcd.blink();
		lcd << F("test PWM");
		lcd.setCursor(0,1); lcd<< F("PWM   ");
		lcd.setCursor(0,2);	lcd<< F("R0   ");
		testPWMrefresh();
	}
	
	//if(menu.periodic(300)) testPWMrefresh();
	
	
	if(btn.state(BTN_LONGCLICK))
	{
		lcd.noBlink();
		menu.next(menu_start);
		solar.resume();
		RF &= ~BIT_R0; //release the force bit
	}
	
	if(btn.state(BTN_CLICK))
	{
		if(R&BIT_R0) R&=~BIT_R0;
		else R|=BIT_R0;		//toggle R0 output
		testPWMrefresh();
	}

	if(Counter)
	{
		Pwm0+=encoderCount();
		if(Pwm0<0) Pwm0=100;
		if(Pwm0>100) Pwm0=0;
		testPWMrefresh();
	}
	
}
	
	
	


///////////////////////////solar state machine//////////////////////////////////
void solar_off()
{		
	setOutput(BIT_R0, 0);
	Pwm0=0;
	
	if( S[3][0] && T[0]>S[3][1] )
		solar.next(solar_protection);
	
	if( T[0]>(T[1]+S[0][1]) && T[1]<(S[0][0]-S[0][3]) )
		solar.next(solar_run);
	
	if( S[2][0] && T[0]>S[2][1] && solar.elapsed(S[2][3]*60E3) )
		solar.next(solar_try);

}

void solar_wait()
{
	setOutput(BIT_R0, 1);
	Pwm0=0;
	
/* 	if(solar.isFirstRun())
		Serial<<"solar_wait"<<T[0]<<" "<<T[1]<<_endl; */
	
	if( S[3][0] && T[0]>S[3][1] )
		solar.next(solar_protection);
	
	if( T[0]>(T[1]+S[0][1]) && T[1]<(S[0][0]-S[0][3]) )
		solar.next(solar_run);
	
	if( S[2][0] && T[0]>S[2][1] && solar.elapsed(S[2][3]*60E3) )
		solar.next(solar_try);
	
	if( solar.elapsed(DELAY_SOLAR_OFF) ) solar.next(solar_off);
	 
}

void solar_protection()
{
	setOutput(BIT_R0, 1);
	Pwm0=S[1][0];	
	
/* 	if(solar.isFirstRun())
		Serial<<"solar_protection"<<_endl; */
	
	if( T[0]<(S[3][1]-S[3][2]) )
		solar.next(solar_wait);
	
	if( T[1]>S[3][3] )
		solar.next(solar_protection_err);
}

void solar_protection_err()
{
/* 	if(solar.isFirstRun())
	{
		Serial<<"solar_protection error"<<_endl;//message tank overheat
	} */
	
	setOutput(BIT_R0, 1);
	Pwm0=0;
	
	if( T[0]<(S[3][1]-S[3][2]) || T[1]<S[3][3] )
		solar.next(solar_wait);
}

void solar_run()
{
	if(solar.isFirstRun())
	{
		solarRun.next(solarRun_start);
	//	Serial<<"solar_run"<<_endl;
	}

	solarRun.run();
	
	if( T[0]<(T[1]+S[0][2]) || T[1]>S[0][0] )
		solar.next(solar_wait);
}

void solar_try()
{
	setOutput(BIT_R0, 1);
	Pwm0=S[1][1];	
	
/* 	if(solar.isFirstRun())
		Serial<<"solar_try "<<_endl; */
	
	if( solar.elapsed(S[2][2]*1000) )
		solar.next(solar_wait);
}

////////////////////////solarRun state machine//////////////////////////////////
void solarRun_start()
{
	setOutput(BIT_R0, 1);
	Pwm0=S[1][1];
		
/* 	if(solarRun.isFirstRun())
		Serial<<"solarRun_start "<<_endl; */
	solarRun.next(solarRun_wait);
}

void solarRun_wait()
{
	setOutput(BIT_R0, 1);
	
/* 	if(solarRun.isFirstRun())
		Serial<<"solarRun_wait "<<_endl; */
	
	if( solarRun.elapsed(S[1][3]*1000))
	{
		if( (T[0]-T[1])<S[1][4] )
			solarRun.next(solarRun_dec);
		
		if( (T[0]-T[1])>S[1][4] )
			solarRun.next(solarRun_inc);
	}		
}

void solarRun_dec()
{
	setOutput(BIT_R0, 1);
	Pwm0-=S[1][2];
	
	if( Pwm0<S[1][1] ) 
		Pwm0=S[1][1];
	
	solarRun.next(solarRun_wait);
}

void solarRun_inc()
{
	setOutput(BIT_R0, 1);
	Pwm0+=S[1][2];
	
	if( Pwm0>S[1][0] ) 
		Pwm0=S[1][0];
	
	solarRun.next(solarRun_wait);
}

////////////////////////heat state machine//////////////////////////////////////
void heat_close()
{
	moveMixValve(MVCLOSE);
	
	if(heat.elapsed(C[1][0]))
		heat.next(heat_off);
}

void heat_off()
{
}

////////////////////////boiler state machine////////////////////////////////////
void boiler_wait()
{
	if(T[8]>B[0][0])
		boiler.next(boiler_run);
}

void boiler_run()
{
	setOutput(BIT_R4, 1);
	
	if(T[8]<(B[0][0]-B[0][1]))
		boiler.next(boiler_wait);
	
	if(T[1]>B[0][2])
		boiler.next(boiler_err);
}

void boiler_err()
{
	if(boiler.isFirstRun())
		{} //message erreur tank overheat
	
	setOutput(BIT_R4, 1);
	
	if((T[1]<B[0][2])||(T[8]<(B[0][0]-B[0][1])))
		boiler.next(boiler_run);
}
