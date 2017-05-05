/*
	Thermoduino
	
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

//sensor addresses EEPROM storage address
#define EEPROM_SENSOR_ADR 	0
#define SENSOR_NBR 			10
#define EEPROM_PARAM_ADR	EEPROM_SENSOR_ADR + sizeof(sensorAddress)
// arrays to hold sensor addresses and temperatures
DeviceAddress sensorAddress[SENSOR_NBR];
float T[SENSOR_NBR];
float Offset[SENSOR_NBR];
bool TForce[SENSOR_NBR];
 
#define TEMPERATURE_RESOLUTION 9 //0,5°C sensor acccuracy.

#define PIN_ONE_WIRE_BUS A3

#define DELAY_MENU_BACK			600E3	//10 minutes
#define DELAY_MENU_EXIT_PARAM 	40E3	//40 seconds
#define DELAY_MENU_REFRESH		1500
#define DELAY_LOG_PERIOD		5E3	
#define DELAY_SDINIT			20E3

//encoder pins
#define PIN_A	6
#define PIN_B	7
#define PIN_BTN 8
char Counter=0;


//SD card CS pin on the robotdyn shield is pin 9
#define PIN_SD_CS	9
#define LOGFILENAME	"datalog.txt"
// File system object.
SdFat sd;
// Log file.
SdFile logfile;
bool SdOK=false,FileOK=false;

//outputs
#define PIN_PWM1	10	//solar pump speed signal
#define PIN_R1		5	//solar pump
#define PIN_R2		4	//heating pump
#define PIN_R3		3	//heating water temp servo +/-
#define PIN_R4		2	//heating water temp servo on
#define PIN_R5		A0	//boiler pump

#define RELAYS_NBR 5
byte R=0;
byte RF=0;
#define BIT_R1	1
#define BIT_R2	2
#define BIT_R3	4
#define BIT_R4	8
#define BIT_R5	16
byte Pwm1=0;


#define NUM_S1	3
#define NUM_S2	5
#define NUM_S3	4
#define NUM_S4	4

#define NUM_C1	5
#define NUM_C2	3
//arrays wich store the Sx parameters, one for each section
char S1[NUM_S1],S2[NUM_S2],S3[NUM_S3],S4[NUM_S4];
//array with the numbers of elements in each of the parameter arrays
const char Sn[]={NUM_S1,NUM_S2,NUM_S3,NUM_S4};
//same thing for the Cx params
char C1[NUM_C1],C2[NUM_C2];
const char Cn[]={NUM_C1,NUM_C2};
//pointer to the array of arrays storing the parameters
char *S[]={S1,S2,S3,S4};
char *C[]={C1,C2};
//arrays of strings with details for each parameter
const char *St1[]={
	"Consigne bas ballon",
	"Differentiel demarra",
	"Differentiel arret"};
const char *St2[]={	
	"PWM max %",
	"PWM min %",
	"Increment %",
	"Periode cycle x10s",
	"Difference voulue"}; 
const char *St3[]={	
	"Demarrage periodique",
	"T mini capteur",
	"Duree impulsions (s)",
	"Periode (x10s)"}; 
const char *St4[]={	
	"Protection surchauff",
	"T max capteur",
	"Differential arret",
	"T max bas ballon"}; 	
const char **St[]={St1,St2,St3,St4};
const char *Ct1[]={
	"ON/OFF",
	"T jour",
	"T nuit",
	"H jour",
	"H nuit"};
const char *Ct2[]={	
	"Tps mvmt complet s",
	"Tps mvmt cycle s",
	"Periode cycle x10s"}; 
const char **Ct[]={Ct1,Ct2};

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
YASM heat;
YASM boiler;
BTN btn;

DS1307_t DateTime;

void setup(void)
{
  //Serial.begin(9600); 
  
  lcd.begin(4,20);
  lcd.backlight();
  //lcd.clear();
  //lcd.print(F("Initialisation..."));
  
  RTC.start();
  
  pinMode(PIN_A,INPUT_PULLUP);  //encoder input
  pinMode(PIN_B,INPUT_PULLUP);  
  pinMode(PIN_BTN,INPUT_PULLUP);
  
  pinMode(PIN_R1,OUTPUT);
  pinMode(PIN_R2,OUTPUT);
  pinMode(PIN_R3,OUTPUT);
  pinMode(PIN_R4,OUTPUT);
  pinMode(PIN_R5,OUTPUT);
  pinMode(PIN_PWM1,OUTPUT);
  outputsWrite();
  
  sensors.begin(); // Start up the dallas library
  sensors.setWaitForConversion(false); //don't wait !
  
  loadSensorsAddresses(); //fill the addresses array with the values in EEPROM
  setSensorsResolution();
  
  loadParams();//get all the parameters from eeprom
  
  menu.next(menu_start); 
  gettemp.next(gettemp_request);
  datalog.next(datalog_start);
}

void loop(void) 
{
	R &= RF; //erase all output bits exept forced ones
	RTC.get(&DateTime,true);
	btn.update(!digitalRead(PIN_BTN)); // ! because btn switch to gnd
	encoderRead();
	gettemp.run();
	menu.run();
	solar.run();
	heat.run();
	boiler.run();
	datalog.run();
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


void outputsWrite()
{
	// ! because relays are active low
	digitalWrite(PIN_R1,!(R&BIT_R1));
	digitalWrite(PIN_R2,!(R&BIT_R2));
	digitalWrite(PIN_R3,!(R&BIT_R3));
	digitalWrite(PIN_R4,!(R&BIT_R4));
	digitalWrite(PIN_R5,!(R&BIT_R5));
	analogWrite(PIN_PWM1,Pwm1);
}

void loadSensorsAddresses()
{
	EEPROM.get(EEPROM_SENSOR_ADR,sensorAddress);
}

void saveParams()
{
	int address = EEPROM_PARAM_ADR;
	EEPROM.put(address,S1);
	address += sizeof(S1);
	EEPROM.put(address,S2);
	address += sizeof(S2);
	EEPROM.put(address,S3);
	address += sizeof(S3);
	EEPROM.put(address,S4);
	address += sizeof(S4);
	EEPROM.put(address,C1);
	address += sizeof(C1);	
	EEPROM.put(address,C2);
	//address += sizeof(C2);
}
void loadParams()
{
	int address = EEPROM_PARAM_ADR;
	EEPROM.get(address,S1);
	address += sizeof(S1);
	EEPROM.get(address,S2);
	address += sizeof(S2);
	EEPROM.get(address,S3);
	address += sizeof(S3);
	EEPROM.get(address,S4);
	address += sizeof(S4);
	EEPROM.get(address,C1);
	address += sizeof(C1);	
	EEPROM.get(address,C2);
	//address += sizeof(C2);
}

void setSensorsResolution()
{
	for (byte i=0;i<SENSOR_NBR; i++)
	{
		sensors.setResolution(sensorAddress[i], TEMPERATURE_RESOLUTION);
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
//
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
			T[i] = sensors.getTempC(sensorAddress[i]);
	}
	else gettemp.next(gettemp_request);
}

/////////////////////////datalog state machine//////////////////////////////////
void datalog_wait()
{
	if(datalog.elapsed(DELAY_LOG_PERIOD))
		datalog.next(datalog_write);
}
void datalog_wait_card()
{
	if(datalog.elapsed(DELAY_SDINIT))
		datalog.next(datalog_write);
}
void datalog_start()
{
	SdOK=sd.begin(PIN_SD_CS, SPI_HALF_SPEED);
	if(!SdOK)
	{
		datalog.next(datalog_wait_card);
		return;
	}
 	
	FileOK=logfile.open(LOGFILENAME, O_RDWR | O_CREAT | O_AT_END);
	if(!FileOK)
	{
		datalog.next(datalog_wait_card);
		return;
	}

	logfile<<_endl<<F("date;");
	for(byte i=0;i<SENSOR_NBR;i++) logfile << F("F;T")<<i+1<<F(";");
	logfile<<F("Pwm;");
	for(byte i=0;i<RELAYS_NBR;i++) logfile << F("F;R")<<i+1<<F(";");
	logfile<<_endl;
	logfile.flush();
	logfile.close();
	datalog.next(datalog_write);
				
}

void datalog_write()
{
 	FileOK=logfile.open(LOGFILENAME, O_RDWR | O_CREAT | O_AT_END);
	if(!FileOK)
	{
		SdOK=false;
		datalog.next(datalog_start);
		return;
	}
	
	logfile<<RTC.getS(DS1307_STR_DATE,0)<<F(" ")<<RTC.getS(DS1307_STR_TIME,0)<<F(";");
	for(byte i=0;i<SENSOR_NBR;i++) 
	{
		if(TForce[i]) logfile<<F("F");
		logfile << F(";")<<T[i] <<F(";");
	}
	logfile<<Pwm1<<F(";");
	for(byte i=0;i<RELAYS_NBR;i++) 
	{
		if(RF&(2^i)) logfile<<F("F");
		logfile << F(";")<< ((R&(2^i))&&1) << F(";");
	}
	logfile<<_endl;
	logfile.flush();
	logfile.close();
	datalog.next(datalog_wait);

}
	

//////////////////////////menu state machine////////////////////////////////////
char Pos,Page;

void menu_start()
{
	bool flag=false;
	if (menu.isFirstRun())
	{
		lcd.clear();
		lcd.print(F("Ballon"));
		lcd.setCursor(0,1); lcd.print(F("Capteur"));
		lcd.setCursor(0,2); lcd.print(F("Int       Ext"));
		lcd.setCursor(0,3); lcd.print(F("Chauffage"));
		flag=true;
	}
 	
	if(menu.periodic(DELAY_MENU_REFRESH)||flag) 
	{
		lcd.setCursor(7,0); printT(1);
		lcd.setCursor(13,0); printT(3);

		lcd.setCursor(7,1); printT(0);
		lcd.setCursor(15,1); if(RF&BIT_R1) lcd<<F("F"); lcd<<Pwm1<<F("%");
		
		lcd.setCursor(3,2); printT(7);
		lcd.setCursor(13,2); printT(6);
		
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
		
		lcd.setCursor(15,3); lcd<<freeRam();
	}
 
	if(encoderCount()>0)
		menu.next(menu_start2);
	
	if(btn.state(BTN_CLICK)) 
		menu.next(menu_param);
}

void menu_start2()
{
	bool flag=false;
	if (menu.isFirstRun())
	{
		lcd.clear();
		lcd.print(F("T1:      T10:"));
		lcd.setCursor(0,1); lcd.print(F("T2:       T3:"));
		lcd.setCursor(0,2); lcd.print(F("T4:"));
		lcd.setCursor(0,3); lcd.print(F("R1:"));
		flag=true;
	}	
	
	if(menu.periodic(DELAY_MENU_REFRESH)||flag) 
	{
		lcd.setCursor(3,0); printT(0); lcd.setCursor(13,0); printT(9);
		lcd.setCursor(3,1); printT(1); lcd.setCursor(13,1); printT(2); 
		lcd.setCursor(3,2); printT(3);
		lcd.setCursor(3,3); printR(BIT_R1); lcd<<" "<<Pwm1<<"%"; 
	}
	
	if(Counter<0 || menu.elapsed(DELAY_MENU_BACK))
		menu.next(menu_start);
	if(encoderCount()>0)
		menu.next(menu_start3);	
	if(btn.state(BTN_LONGCLICK)) 
		menu.next(menu_forceT);

}

void menu_start3()
{
	bool flag=false;
	if (menu.isFirstRun())
	{
		lcd.clear();
		lcd.print(F("T5:       T6:"));
		lcd.setCursor(0,1); lcd.print(F("T7:       T8:"));
		lcd.setCursor(0,2); lcd.print(F("R2:   R3:   R4:"));
		lcd.setCursor(0,3); lcd.print(F("T9:         R5:"));
		flag=true;
	}
	
	if(menu.periodic(DELAY_MENU_REFRESH)||flag) 
	{
		lcd.setCursor(3,0); printT(4); lcd.setCursor(13,0); printT(5);
		lcd.setCursor(3,1); printT(6); lcd.setCursor(13,1); printT(7);
		lcd.setCursor(3,2); printR(BIT_R2); 
		lcd.setCursor(9,2); printR(BIT_R3); 
		lcd.setCursor(15,2); printR(BIT_R4);
		lcd.setCursor(3,3); printT(8); 
		lcd.setCursor(15,3); printR(BIT_R5);
	}
	
	if(encoderCount()<0)
		menu.next(menu_start2);	

	if(menu.elapsed(DELAY_MENU_BACK))
		menu.next(menu_start);	
	if(btn.state(BTN_LONGCLICK)) 
		menu.next(menu_forceR);
}

	
void menu_param()
{	
	if (menu.isFirstRun()) 
	{
		lcd.clear();
		lcd<<(char)126<<F("Reglages horloge");
		lcd.setCursor(1,1); lcd<<F("Reglages sondes");
		lcd.setCursor(1,2); lcd<<F("Reglages solaire");
		lcd.setCursor(1,3); lcd<<F("Reglages chauffage");
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
		lcd<<(char)126<<F("Forcage relais");
		lcd.setCursor(1,1); lcd<<F("Forcage temperature");
		lcd.setCursor(1,2); lcd<<F("xxx");
		lcd.setCursor(1,3); lcd<<F("xxxx");
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
			case 0 : { menu.next(menu_forceR); Pos=0; break; }
			case 1 : { menu.next(menu_forceT); Pos=0; break; }
			case 2 : { break; }
			case 3 : { break; }
		}
				
	if(btn.state(BTN_LONGCLICK) || menu.elapsed(DELAY_MENU_EXIT_PARAM))
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
	if(RF&BIT_R1) {lcd<<F("    ");lcd.setCursor(3,1); lcd<<(R&BIT_R1);}
	else lcd << F("AUTO");
	lcd.setCursor(8,1); lcd<<Pwm1<<F("%  ");
	lcd.setCursor(3,2); 
	if(RF&BIT_R2) {lcd<<F("    ");lcd.setCursor(3,2); lcd<<((R&BIT_R2)&&1);}
	else lcd << F("AUTO");
	lcd.setCursor(3,3); 
	if(RF&BIT_R3) {lcd<<F("    ");lcd.setCursor(3,3); lcd<<((R&BIT_R3)&&1);}
	else lcd << F("AUTO");
	lcd.setCursor(15,2); 
	if(RF&BIT_R4) {lcd<<F("    ");lcd.setCursor(15,2); lcd<<((R&BIT_R4)&&1);}
	else lcd << F("AUTO");
	lcd.setCursor(15,3); 
	if(RF&BIT_R5) {lcd<<F("    ");lcd.setCursor(15,3); lcd<<((R&BIT_R5)&&1);}
	else lcd << F("AUTO");
	
	switch (Pos)
	{
		case 0 : { lcd.setCursor(3,1); outset(BIT_R1, encoderCount()); break; }
		case 1 : { 
			lcd.setCursor(8,1); 
			if(RF&BIT_R1) 
			{
				Pwm1+=(encoderCount()*5);
				if(Pwm1<0) Pwm1=0;
				if(Pwm1>100) Pwm1=100;
			}
			else encoderCount();
			break; }
		case 2 : { lcd.setCursor(3,2); outset(BIT_R2, encoderCount()); break; }
		case 3 : { lcd.setCursor(3,3); outset(BIT_R3, encoderCount()); break; }
		case 4 : { lcd.setCursor(15,2); outset(BIT_R4, encoderCount()); break; }
		case 5 : { lcd.setCursor(15,3); outset(BIT_R5, encoderCount()); break; }
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
		lcd.setCursor(0,1); lcd<<F("R1:");
		lcd.setCursor(0,2); lcd<<F("R2:         R4:");
		lcd.setCursor(0,3); lcd<<F("R3:         R5:");
		lcd.blink();
		Rprintset();
	}
	
	if(btn.state(BTN_LONGCLICK))
	{
		lcd.noBlink();
		menu.next(menu_param2);
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
		lcd<<F("T")<<(Page*6+i+1)<<F(":"); 
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
		case BTN_LONGCLICK : { lcd.noBlink(); menu.next(menu_param2); break; }
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
	static char truc;
	if (menu.isFirstRun()) 
	{
		lcd.clear(); 
	}
	
	if(Counter!=0) encoderCount();
	
	
	
	
	
	if(btn.state(BTN_LONGCLICK))
		menu.next(menu_param);	
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
		lcd.setCursor(1,1); lcd<< F("S1 base");
		lcd.setCursor(1,2); lcd<< F("S2 pompe");
		lcd.setCursor(1,3); lcd<< F("S3 avance");
		lcd.setCursor(11,1); lcd<<F("S4 protec");
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
		lcd.setCursor(1,1); lcd<< F("C1 base");
		lcd.setCursor(1,2); lcd<< F("C2 vanne");
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

void menu_setSxx()
{
	if (menu.isFirstRun()) 
	{
		lcd.clear();
				
		for(byte i=0;i<Sn[Page];i++)
		{
			if(i<3) lcd.setCursor(1,i+1); 
			else lcd.setCursor(11,i-2);
			lcd<<F("S")<<(int)Page+1<<F(".")<<(i+1)<<F(":"); 
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
			lcd<<F("C")<<(int)Page+1<<F(".")<<(i+1)<<F(":"); 
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



