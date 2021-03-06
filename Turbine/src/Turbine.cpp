#include <Arduino.h>
#include <heltec.h>
#include <PID_v1.h>
#include <Fsm.h>
#include <Adafruit_I2CDevice.h>
#include <Preferences.h>
#include <Adafruit_INA260.h>
#include <Adafruit_INA219.h>
#include <ArduinoJSON.h>
#include <Adafruit_ADS1X15.h>

#include "Turbine.h"
#include "digitalInput.h"
#include "digitalOutput.h"
#include "Moteur.h"
#include "TurbineEtangLib.h"
#include "motion.h"
#include "menu.h"
#include "configuration.h"



//Sortie
byte pinMoteurO = 12;
byte pinMoteurF = 13;

// Entrée 
byte pinEncodeurA = 36;
byte pinEncodeurB = 37;
#define pinTaqui 32

digitalInput FCVanneOuverte(39,INPUT_PULLUP); 
digitalInput FCVanneFermee(38,INPUT_PULLUP);
digitalInput* PrgButton= new digitalInput(0,INPUT_PULLUP);

//encodeur
volatile long countEncodA = 0;
unsigned long dernieredetectionEncodA  = 0;

volatile long countEncodB = 0;
unsigned long dernieredetectionEncodB  = 0;

bool pom_success = false;
bool do_send_msg = false;

//generatrice
float generatrice_voltage = 0;
float generatrice_current = 0;

bool do_ouvertureTotale = false;
bool do_fermetureTotale = false;
//Define Variables we'll be connecting to
double  Output;
RTC_DATA_ATTR double posMoteur;
RTC_DATA_ATTR double Setpoint;

double Kp=2.2, Ki=0.0, Kd=0.4;
PID myPID(&posMoteur, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

int maxAxel = 10;
int vmin = 80;
int previousVitesse = 0;
int MoteurPWM = 0;
/**
 * @brief ouverture max en tick
 * 
 */
long ouvertureMax = 2000;
int incCodeuse = 400;
float tourMoteurVanne = 14 / float(44); 

// Taquimetre
unsigned long previousMillisCalculTaqui = 0;
unsigned long previousMillisDetectionTaqui = 0;
volatile unsigned long countTaqui = 0;
float rpmTurbine = 0;
unsigned long previousCalculTaqui = 0;

long msgCount = 0;
bool newMessage = false;
Message receivedMessage;

SSD1306Wire* display = Heltec.display;

byte localAddress = TURBINE;

Preferences preferences;

Adafruit_INA260 ina260 = Adafruit_INA260();
Adafruit_INA219 ina219 = Adafruit_INA219(0x44);

struct ina_T
{
  float current_mA = 0;
  float busVoltage = 0;

};
ina_T ina219Data;
ina_T ina260Data;

Adafruit_ADS1115 ads;
uint16_t rawTension_ads = 0;
float Tension_ads = 0;
uint16_t rawCurrent_ads = 0;
float Current_ads = 0;

double currentValue260 = 0;
double currentValue219 = 0;
unsigned long previousMesureIntensite = 0;
int maxIntensite = 9000; //mA

byte displayMode = 0;

//menus

String  menu_param[] = {"AP", "STA","ScanWifi"};
menu Menu_param(3,3,NULL,NULL);
#if defined(LORA_ASYNC  )
bool reactivationReception = false;
bool needReactivationReception = true;
unsigned long delaireactivation = 0;
#endif
void INIT(){
  static unsigned long ti = millis();

  display->clear();

  display->drawString(0,0,"INIT: ");
  display->drawString(10,15,String(millis() - ti));
  display->display();
}
void AUTO(){
  
  myPID.SetMode(AUTOMATIC);

  posMoteur += countEncodA;
  countEncodA = 0;

  MoteurPWM = Output;
  if (MoteurPWM >255) MoteurPWM = 255;

  if (MoteurPWM < -255) MoteurPWM = -255;



  if (MoteurPWM - previousVitesse > maxAxel)
  {
    MoteurPWM = previousVitesse + maxAxel;
    //display->drawString(100,40,"+");
  } else if (MoteurPWM -previousVitesse <-maxAxel)
  {
    //display->drawString(100,50,"-");
    MoteurPWM = previousVitesse - maxAxel;
  }

  previousVitesse = MoteurPWM;
  if (MoteurPWM >0 && MoteurPWM <vmin) MoteurPWM = 0;

  if (MoteurPWM < 0 && MoteurPWM > -vmin) MoteurPWM = -0;

  // display->drawString(0,0,"countA: " + (String) countEncodA);
  // display->drawString(60,0,"countB: " + (String) countEncodB);
  // display->drawString(0,15,"posMoteur: " + (String) posMoteur);
  // display->drawString(0,30,"Output: " + (String) Output);
  // display->drawString(0,45,"Setpoint: " + (String) Setpoint);
  displayData();
  
}
//FSM
State state_INIT(NULL,&INIT,NULL,"INIT");
State state_AUTO(NULL,&AUTO,NULL,"AUTO");

Fsm fsm(&state_INIT);
State state_SendMessage(NULL,[](){
  display->clear();
  display->drawString(0,0,"sendMSG");
  display->display();
  delay(2000);
  do_send_msg = false;
},NULL,"SendMsg");

State state_POM(NULL,[](){
  static int state = -1;
  unsigned long timerPOM = 0;
  display->clear();
  if (state == -1){
    timerPOM = millis();
    state =0;
  } 
   
  if (state == 3)
  {
    state = 4;
  }
  if (state ==0 && FCVanneFermee.isReleased())
  {
    state = 1;
  }
  
  if (FCVanneFermee.isPressed() && state !=0 && state  !=4 && state !=-1)
  {
    state = 3;
    
  }
  if (((currentValue260 > maxIntensite) || (millis() - timerPOM > 45000)) && state !=0 && (state !=-1))
  {
    state = 5;
  }
  
  
  
  switch (state)
  {
    case -1:
    timerPOM = millis();
    state = 0;
    break;
  case 0:
    MoteurPWM = 255;
    break;
  case 1:
    MoteurPWM = -200;
    break;
  case 3:
    MoteurPWM = 0;
    //state = 4;
    break;
  case 4:
    pom_success = true;
    delay(200);
    posMoteur = 0;
    countEncodA = 0;
    countEncodB = 0;
    Serial.println(millis());
    break;
  case 5:
    MoteurPWM = 0;
    Heltec.display->drawString(0,40,"Erreur POM");
    break;
  default:
    Serial.println("defaul");
    break;
  }
  display->drawString(10,50,(String)FCVanneFermee.getState());
  display->drawString(60,50,(String)FCVanneOuverte.getState());
  display->drawString(0,0,"State, "+ String(state));
  display->drawString(0,20,"Intensite: " + (String)currentValue260);
  display->display();
},NULL,"POM");
State state_OuvertureTotale(NULL,[](){
  posMoteur += countEncodA;
  countEncodA = 0;
  
  myPID.SetMode(MANUAL);
  MoteurPWM = 255;
},NULL,"OuvertureTotale");
State state_FermetureTotale(NULL,[](){
  posMoteur += countEncodA;
  countEncodA = 0;
  if (FCVanneFermee.isPressed())
  {
    MoteurPWM = 0;

    Setpoint = posMoteur;
    return;
  }
  myPID.SetMode(MANUAL);
  MoteurPWM = -255;
},NULL,"FermetureTotale");
State state_StopIntensite(NULL,[](){
  myPID.SetMode(MANUAL);
  display->clear();
  display->drawString(10,10,"Stop intensite");
  display->display();
  MoteurPWM = 0;
},NULL,"StopI");

State state_param(NULL,[](){
  display->clear();
  display->drawString(0,0,"Param:");
  display->drawString(60,0,(String)countEncodA);
  static unsigned lastcountEncodA = 0;
  if (digitalRead(pinEncodeurA)== LOW  && millis()-lastcountEncodA >500)
  {
    lastcountEncodA = millis();
    Menu_param.selectNext();
  }
  

  Menu_param.loop();
  Menu_param.render();

  display->display();
},NULL,"param");

void initTransition(){
  fsm.add_timed_transition(&state_INIT,&state_POM,2000, NULL);
  fsm.add_transition(&state_INIT, &state_AUTO,[](unsigned long duration){
    return PrgButton->isPressed();
  },NULL);
  fsm.add_transition(&state_POM,&state_AUTO,[](unsigned long duration){
    return pom_success;
  },NULL);
  fsm.add_transition(&state_AUTO ,&state_SendMessage,[](unsigned long duration){
    return false;
  }, NULL);
  fsm.add_transition(&state_SendMessage ,&state_AUTO,[](unsigned long duration){
    return !do_send_msg;
  }, NULL);
  fsm.add_transition(&state_INIT,&state_AUTO,[](unsigned long duration){
    return esp_sleep_get_wakeup_cause()== ESP_SLEEP_WAKEUP_TIMER;
  },NULL);
  fsm.add_transition(&state_AUTO,&state_OuvertureTotale,[](unsigned long duration){
    return do_ouvertureTotale;
  },[](){
    do_ouvertureTotale = false;
  });
  fsm.add_transition(&state_AUTO,&state_FermetureTotale,[](unsigned long duration){
    return do_fermetureTotale;
  },[](){
    do_fermetureTotale = false;
  });
  fsm.add_transition(&state_OuvertureTotale,&state_AUTO, [](unsigned long duration){
    return FCVanneOuverte.isPressed();
  },[](){
    MoteurPWM = 0;
    Setpoint = posMoteur;

  });
  fsm.add_transition(&state_FermetureTotale,&state_AUTO, [](unsigned long duration){
    return FCVanneFermee.isPressed();
  },[](){
    MoteurPWM = 0;
    posMoteur = 0;
    Setpoint = posMoteur;

  });
  fsm.add_transition(&state_AUTO,&state_StopIntensite,[](unsigned long duration){
    return currentValue260 > maxIntensite;
  },NULL);
  fsm.add_transition(&state_OuvertureTotale,&state_StopIntensite,[](unsigned long duration){
    return currentValue260 > maxIntensite;
  },NULL);
  fsm.add_transition(&state_FermetureTotale,&state_StopIntensite,[](unsigned long duration){
    return currentValue260 > maxIntensite;
  },NULL);

  fsm.add_transition(&state_AUTO,&state_param,[](unsigned long duration){
    return PrgButton->pressedTime() > 2000;
  },NULL);
}
void IRAM_ATTR isrEncodA(void){
  if (millis() - dernieredetectionEncodA > 0)
  {
    if (digitalRead(pinEncodeurB) == HIGH)
    {
      //Serial.println("v");
      countEncodA--;
    }else
    {
      countEncodA++;
    }
    
  
  }
  
}
void IRAM_ATTR isrEncodB(void){
  if (millis() - dernieredetectionEncodB > 0)
  {
    countEncodB++;
    
  }
  
}

void TraitementCommande(String c){
	Serial.println("TraitementCommande: " + (String)c);
	if (c == "DemandeStatut")
	{
		StaticJsonDocument<256> doc;
		String json; 


		doc["Ouverture"] = String(pPosMoteur(),2);
		doc["Setpoint"] = pSetpoint();
		doc["Taqui"] = rpmTurbine;
		//doc["StatutVanne"] = StatutVanne;
		doc["OuvCodeur"] = posMoteur;
		doc["OuvMaxCodeur"] = ouvertureMax;
    doc["Tension"] = String(generatrice_voltage,2);
    doc["Intensite"] = String(generatrice_current,2);
    doc["MPwm"] = MoteurPWM;
    doc["State"] = fsm.getActiveState()->Name;


		serializeJson(doc,json);

    
    
		Serial.println("Ce que j'envoi: " + String(json));
    #if defined(LORA_ASYNC)
    
      sendMessage(MASTER, json,true);
      delaireactivation = millis();
      needReactivationReception  = true;
      
      
    #endif // LORA_ASYNC
    #if !defined(LORA_ASYNC)
   
    sendMessage(MASTER, json);
    LoRa.receive();
    
    #endif // LORA_ASYNC
    
		
		
	} else
	{
    #if defined(LORA_ASYNC)
      sendMessageConfirmation(receivedMessage.msgID,10U,true);
      delaireactivation = millis();
      needReactivationReception  = true;
    #else
      sendMessageConfirmation(receivedMessage.msgID);
      LoRa.receive();
    #endif // LORA_ASYNC
    
		
	}
	
	
	if (c.startsWith("M"))
	{
		c.remove(0,1);
		
		Setpoint = c.toInt();
		
	}
	if (c.startsWith("DEGV"))
	{
		c.remove(0, 4);
		
		Serial.println(c.toInt());
		Setpoint += degvanneToInc(c.toInt());
		Serial.println(Setpoint);
				
	}
	if (c.startsWith("DEG"))
	{
		c.remove(0, 3);
		
		Setpoint += degToInc(c.toInt());
		Serial.println(Setpoint);
	
	}
	
	if (c.startsWith("OT"))
	{
		c.remove(0, 2);
		do_ouvertureTotale = true;
	
	}
	if (c.startsWith("FT"))
	{
		c.remove(0, 2);
		do_fermetureTotale = true;

	}
	if (c == "SMIN")
	{
		posMoteur = 0;
	}
	if (c == "SMAX")
	{
		ouvertureMax = posMoteur;
		preferences.putInt("ouvertureMax",ouvertureMax);
	}
	if (c.startsWith("SetMaxI="))
	{
		c.replace("SetMaxI=","");
		maxIntensite = c.toInt();
		preferences.putInt("maxIntensite",maxIntensite);
		
	}
	if (c == "ouvertureMax")
	{
		c.remove(0,12);
		
		ouvertureMax = c.toInt();
		preferences.putInt("ouvertureMax",ouvertureMax);
		
	}
	if (c.startsWith("DeepSleep="))
	{
		c.replace("DeepSleep=","");
		Serial.println("DeepSleep " + String(c.toDouble()));
		Heltec.display->clear();
		Heltec.display->drawString(0,0,"DeepSleep");
		Heltec.display->drawString(0,15,String(c.toDouble()));
		Heltec.display->display();
		delay(2000);
		esp_sleep_enable_timer_wakeup(c.toDouble()*1000);
		esp_deep_sleep_start();
	}
	if (c.startsWith("LightSleep="))
	{
		c.replace("LightSleep=","");
		Serial.println("LightSleep " + String(c.toInt()));
		esp_sleep_enable_timer_wakeup(c.toInt()*1000);
		esp_light_sleep_start();
	}
	if (c.startsWith("P="))
	{
		/* ouverture vanne pourcentage */
		c.replace("P=","");
		
		float cibleOuverture = c.toFloat() / 100;
		int cibleTick = ouvertureMax * cibleOuverture;
		Setpoint += cibleTick-Setpoint;
		
		
		
	}
		
  


}

void displayData() {
	Heltec.display->clear();
  if (PrgButton->frontDesceandant())
  {
    displayMode ++;
    if (displayMode >5 )
    {
      displayMode = 0;
    }
    
  }
  
		switch (displayMode)
		{
		case 0:
			Heltec.display->drawString(0, 0, "EA: " + String(countEncodA));

			Heltec.display->drawString(64, 0, "EB: " + String(countEncodB));

			Heltec.display->drawString(0, 15, "posMoteur: " + String(posMoteur));
			Heltec.display->drawString(0, 25, "consigne: " + String(Setpoint));
			
			
			
		
			Heltec.display->drawString(5, 55, FCVanneFermee.isPressed() ? "*" : "");
			Heltec.display->drawString(110, 55, FCVanneOuverte.isPressed() ? "*" : "");
			break;
		case 1:
			Heltec.display->drawString(0, 0, "EA: " + String(countEncodA));

			Heltec.display->drawString(64, 0, "EB: " + String(countEncodB));

			Heltec.display->drawString(0,12,"posM "+String(posMoteur));
			
			Heltec.display->drawString(64,12,"Omax " + String(ouvertureMax));
			
			
			Heltec.display->drawString(80,46,String(pPosMoteur()*100)+"%");
			Heltec.display->drawString(0, 46, "consigne: " + String(Setpoint));
			Heltec.display->drawString(5+((Setpoint)*120) /ouvertureMax, 42, "^");
			
			Heltec.display->drawProgressBar(5,30,120,10,pPosMoteur()*100);
			Heltec.display->drawString(5, 55, FCVanneFermee.isPressed() ? "*" : "");
			Heltec.display->drawString(110, 55, FCVanneOuverte.isPressed() ? "*" : "");
			break;
		case 2:
			Heltec.display->drawLogBuffer(0,0);
			break;

		case 3:
			#ifdef pinTaqui
				Heltec.display->drawString(0,0,"Taqui");
				Heltec.display->drawString(40,0,String(rpmTurbine));
				Heltec.display->drawString(100,0,"RPM");
				
			#endif
			
			Heltec.display->drawString(0,15,"Intensite: ");
			Heltec.display->drawString(60,15,String(currentValue260));
			Heltec.display->drawString(100,15,"mA");
			Heltec.display->drawString(0,28,"MaxI: ");
			Heltec.display->drawString(55,28,String(maxIntensite));
			Heltec.display->drawString(100,28,"mA");
			Heltec.display->drawString(0,41,"Conso ");
			Heltec.display->drawString(60,41,String(currentValue219));
			Heltec.display->drawString(100,28,"mA");
      
			break;
    case 4:
      Heltec.display->drawString(0,0,"Conso Batterie:");
      Heltec.display->drawString(0,15,(String)ina219Data.current_mA + "mA") ;
      Heltec.display->drawString(0,25,(String)ina219Data.busVoltage + "V");
      break;
    case 5:
      Heltec.display->drawString(0,0,"Puissance generé");
      Heltec.display->drawString(0,15,"U " +String(rawTension_ads) + " | "  +String(Tension_ads));
      
      Heltec.display->drawString(0,30,"I " +String(rawCurrent_ads) + " | " + String(Current_ads));
      

      break;
		default:
			Heltec.display->drawString(0,0,"Erreur displayData");
			break;
		}
		
	 
 	
	Heltec.display->drawString(120,50,(String)displayMode) ;
	Heltec.display->display();
}

 
IRAM_ATTR void isrTaqui(void){
 if (millis() - previousMillisDetectionTaqui > 5 )
 {
    previousMillisDetectionTaqui = millis();
    countTaqui++;
 }
 
 
}
float mesureTaqui(void){
  detachInterrupt(pinTaqui);
	float rpm = countTaqui * 60000 / float((millis() - previousMillisCalculTaqui));
  attachInterrupt(pinTaqui, isrTaqui,RISING);
	countTaqui = 0;
	previousMillisCalculTaqui = millis();

	return rpm;
}
/**
 * @brief Acquisition des Entrées
 * 
 */
void acquisitionEntree(void) {
	FCVanneFermee.loop();
	FCVanneOuverte.loop();
	PrgButton->loop();

  if (millis()> previousMesureIntensite + 200)
	{
		previousMesureIntensite = millis();
		currentValue260 = ina260.readCurrent();
		currentValue219 = ina219.getCurrent_mA();

    ina260Data.current_mA = ina260.readCurrent();
    ina260Data.busVoltage = ina260.readBusVoltage();

    ina219Data.current_mA = ina219.getCurrent_mA();
    ina219Data.busVoltage = ina219.getBusVoltage_V();

    rawTension_ads = ads.readADC_SingleEnded(0);
    Tension_ads = ads.computeVolts(rawTension_ads);
    rawCurrent_ads = ads.readADC_SingleEnded(2);
    Current_ads = (ads.computeVolts(rawTension_ads)-1840) * 0.032;
	}

  if (millis()> previousCalculTaqui + 2000)
	{
		previousCalculTaqui = millis();
		rpmTurbine = mesureTaqui();
	}
  
 
  
}

/**
 * @brief Mise a jour des sorties
 * 
 */
void miseAjourSortie(void) {
	
	if (MoteurPWM>0)
   {
     ledcWrite(1,MoteurPWM);
     ledcWrite(2,0);
   }  else if (MoteurPWM < 0)
   {
     ledcWrite(1,0);
     ledcWrite(2,abs(MoteurPWM));
   } else
   
   {
     ledcWrite(1,0);
     ledcWrite(2,0);
   }
	
}
#if defined(LORA_ASYNC  )

void onTxDone(){
  reactivationReception = true;
}

#endif // LORA_ASYNC  

void onReceive(int packetSize){
  	if (packetSize == 0) return;          // if there's no packet, return

	//// read packet header bytes:
	byte recipient = LoRa.read();          // recipient address
	byte sender = LoRa.read();            // sender address
	byte incomingMsgId = LoRa.read();     // incoming msg ID
	byte incomingLength = LoRa.read();    // incoming msg length
	
	String Content = "";                 // payload of packet

	while (LoRa.available())             // can't use readString() in callback
	{
		Content += (char)LoRa.read();      // add bytes one by one
	}

	if (incomingLength != Content.length())   // check length for error
	{
		Serial.println("error: message length does not match length");
		return;                             // skip rest of function
	}

	// if the recipient isn't this device or broadcast,
	if (recipient != localAddress && recipient != 0xFF)
	{
		Serial.println("This message is not for me. " + String(recipient));
		return;                             // skip rest of function
	}
  receivedMessage.recipient = recipient;
  receivedMessage.sender = sender;
  receivedMessage.msgID = incomingMsgId;
  receivedMessage.Content = Content;
  
	newMessage = true;
	//// if message is for this device, or broadcast, print details:
	Serial.println("Received from: 0x" + String(receivedMessage.sender, HEX));
	Serial.println("Sent to: 0x" + String(receivedMessage.recipient, HEX));
	Serial.println("Message ID: " + String(incomingMsgId));
	Serial.println("Message length: " + String(incomingLength));
	Serial.println("Message: " + receivedMessage.Content);
	Serial.println("RSSI: " + String(LoRa.packetRssi()));
	//Serial.println("Snr: " + String(LoRa.packetSnr()));
	Serial.println();
	// Heltec.display->println("0x" + String(receivedMessage.sender,HEX) + " to 0x" + String(receivedMessage.recipient, HEX) + " " + String(receivedMessage.Content));
	Heltec.display->println(String(receivedMessage.Content));



	
	
}
void EvolutionGraphe(void){
  
  
}

void TraitementSerial2(String s){
  while (s.indexOf(";")!= -1)
  {
    String part1;
    String part2;
    
    part1 = s.substring(0,s.indexOf("="));
    part2 = s.substring(s.indexOf("=")+1 , s.indexOf(";"));
    
    Serial.println("p1:   "+ part1);
    Serial.println("p2:   "+ part2);
    //s.replace(part1+"="+part2+";","");
    s.remove(0,s.indexOf(";")+1);
    Serial.println("restant +" + s + "+");
    
    if (part1.equalsIgnoreCase("voltage"))
    {
      generatrice_voltage = part2.toFloat();
    }
    if (part1.equalsIgnoreCase("current"))
    {
      generatrice_current = part2.toFloat();
    }
    
    
  }
  
  Serial.println("finf" + s);
}
void setup() {
  // put your setup code here, to run once:
  Heltec.begin(true,false,true,true,868E6);

  SPI.begin(SCK,MISO,MOSI,SS);
	LoRa.setPins(SS,RST_LoRa,DIO0);
	while (!LoRa.begin(868E6))
	{ 
    display->clear();
		Serial.println("erreur lora");
    display->drawString(0,0,"Init Lora Failed");
    display->display();
		delay(1000);
	}
  display->clear();
	Serial.println("init lora successful");
  display->drawString(0,0,"Init Lora success");
  display->display();

  //Serial1.begin(115200, SERIAL_8N1, 2, 17);

  //Sortie Moteur
  pinMode(pinMoteurO,OUTPUT);
  pinMode(pinMoteurF,OUTPUT);
  ledcSetup(1, 5000, 8);
  ledcSetup(2, 5000, 8);

  // attach the channel to the GPIO to be controlled
  ledcAttachPin(pinMoteurF, 1);
  ledcAttachPin(pinMoteurO, 2);

  //Encodeur Rotation
  pinMode(pinEncodeurA,INPUT);
  pinMode(pinEncodeurB,INPUT_PULLUP);

  attachInterrupt(pinEncodeurA,isrEncodA,RISING);
  attachInterrupt(pinEncodeurB,isrEncodB,RISING);


#ifdef pinTaqui
	pinMode(pinTaqui, INPUT_PULLUP);
	attachInterrupt(pinTaqui, isrTaqui,RISING);
#endif

  while(!ina260.begin(0x40, &Wire)) {
    Heltec.display->drawString(0,24,"Couldn't find INA260 chip");
    Serial.println("Couldn't find INA260 chip");
    Heltec.display->display();
    delay(1000);
  }
  
  
	Heltec.display->drawString(0,24,"INA260 ok !");
	Heltec.display->display();
	Serial.println("Found INA260 chip");

  while (!ina219.begin(&Wire))
  {
    Heltec.display->drawString(0,36,"Couldn't find INA219 chip");
    Serial.println("Couldn't find INA219 chip");
    Heltec.display->display();
    delay(1000);
  }
  Heltec.display->drawString(0,36,"INA219 ok !");
	Heltec.display->display();
	Serial.println("Found INA219 chip");

  while (!ads.begin(0x48, &Wire))
  {
    Heltec.display->drawString(0,48,"Couldn't find ADS chip");
    Serial.println("Failed to initialize ADS.");
    Heltec.display->display();
    delay(1000);
  }
  Heltec.display->drawString(0,48,"ADS ok !");
	Heltec.display->display();
	Serial.println("Found ADS chip");
  ads.startADCReading(ADS1X15_REG_CONFIG_MUX_SINGLE_0, /*continuous=*/false);
  ads.startADCReading(ADS1X15_REG_CONFIG_MUX_SINGLE_2, /*continuous=*/false);
  
  FCVanneFermee.loop();
  FCVanneOuverte.loop();
  initTransition();

  if (preferences.begin("Turbine",false))
	{
		ouvertureMax = preferences.getLong("ouvertureMax",ouvertureMax);
		maxIntensite = preferences.getLong("maxIntensite", maxIntensite);
	}

  if (esp_sleep_get_wakeup_cause()== ESP_SLEEP_WAKEUP_TIMER)
	{

  }
  

  myPID.SetMode(MANUAL);
  myPID.SetOutputLimits(-255, 255);
  Serial.print("Time(s), ");
  Serial.print("Consigne, ");
  Serial.print("posMoteur, ");
  Serial.print("Output, ");
  Serial.print("vitesseM, ");
  Serial.println();

  LoRa.setSpreadingFactor(8);
	LoRa.setSyncWord(0x12);
	LoRa.setSignalBandwidth(125E3);

	LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
	LoRa.receive();

  //menus

  Menu_param.onRender([](int num, int numel,bool hover){
		//Serial.println("onrendermodeWifi " + (String)num + (String)numel);
		Heltec.display->drawString(12,num*12+14,menu_param[numel]);
		if (hover)
		{
			Heltec.display->fillCircle(6,num*12+14+6,3);
		}
		
	});
}

void loop() {
  // put your main code here, to run repeatedly:

  acquisitionEntree();
  miseAjourSortie();
  EvolutionGraphe();
  fsm.run_machine();

  display->clear();

  
  
  if(Serial1.available()){
    Serial.print("[Se1]: ");
    String s = Serial1.readStringUntil('\r');
    TraitementSerial2(s);
  }
 

  myPID.Compute();
   
   if (newMessage)
	{
		newMessage = false;
		TraitementCommande(receivedMessage.Content);
		
	}
  if (reactivationReception )
  {
    reactivationReception = false;
    needReactivationReception = false;
    Serial.println("Reactivation Reception");
    LoRa.receive();
  }
  if (needReactivationReception && millis() - delaireactivation >10000)
  {
    needReactivationReception = false;
    LoRa.receive();
  }
  
  

  // Serial.print(millis());
  // Serial.print(" ");
  // Serial.print(Setpoint);
  // Serial.print(" ");
  // Serial.print(posMoteur);
  // Serial.print(" ");
  // Serial.print(Output);
  // Serial.print(" ");
  // Serial.print(MoteurPWM);
  // Serial.print(" ");
  
  // Serial.println();

  delay(20);
}