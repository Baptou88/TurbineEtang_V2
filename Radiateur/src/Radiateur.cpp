#include <Arduino.h>
#include <heltec.h>
#include <Adafruit_INA219.h>
#include <Adafruit_ADS1X15.h>



#include "digitalOutput.h"
#include "TurbineEtangLib.h"



Adafruit_ADS1115 ads;


digitalOutput rad1(12);
digitalOutput rad2(13);

#define pinSCT013 36
long rawCurrencSCT013 = 0;
float SCT013multiplier = 0.0625F;
float CurrentSCT013 = 0;

Message receivedMessage;
byte localAddress = RADIATEUR;
bool newMessage = false;
unsigned long lastMessage = 0;
int msgCount = 0;
Adafruit_INA219 ina219;
Adafruit_INA219 ina219_2(0x44);

float current_mA;
float current_mA_2;

float offset_A = 0;

bool reactiveReception = false;

void onTxDone(void){
  reactiveReception = true;
}
void onReceive(int packetSize){
  if (packetSize == 0) return;          // if there's no packet, return
	
	//// read packet header bytes:
	receivedMessage.recipient = LoRa.read();          // recipient address
	receivedMessage.sender = LoRa.read();            // sender address
	receivedMessage.msgID = LoRa.read();
	//byte incomingMsgId = LoRa.read();     // incoming msg ID
	byte incomingLength = LoRa.read();    // incoming msg length

	receivedMessage.Content = "";                 // payload of packet

	while (LoRa.available())             // can't use readString() in callback
	{
		receivedMessage.Content += (char)LoRa.read();      // add bytes one by one
	}

	if (incomingLength != receivedMessage.length())   // check length for error
	{
		Serial.println("error: message length does not match length");
		return;                             // skip rest of function
	}
	Serial.println("msg recu: " + String(receivedMessage.Content));
	// if the recipient isn't this device or broadcast,
  Serial.println("RSSI: " + String(LoRa.packetRssi()));
	if (receivedMessage.recipient != localAddress && receivedMessage.recipient != 0xFF)
	{
		Serial.println("message pas pour moi");
		return;                             // skip rest of function
	}
  newMessage = true;
  lastMessage = millis();
}

void mesureSysteme(){
   current_mA = ina219.getCurrent_mA();   
   current_mA_2 = ina219_2.getCurrent_mA();

  rawCurrencSCT013 = analogRead(pinSCT013);
  CurrentSCT013 = (rawCurrencSCT013-2060)* SCT013multiplier;
}

void TraitementCommande(String cmd){
  Serial.println("TraitementCMD: " + String(cmd));
  if (cmd.startsWith("rad"))
  {
    Serial.println("rad");
    cmd.replace("rad","");
    Serial.println(cmd);
    if (cmd.startsWith("1"))
    {
      Serial.println("toggle 1");
      cmd.replace("1","");
      rad1.toggle();
    }
    if (cmd.startsWith("2"))
    {
      Serial.println("toggle 2");
      cmd.replace("2","");
      rad2.toggle();
    }
    
  }
  
}
void gestionSorties(void){
  rad1.loop();
  rad2.loop();
}

void setup() {
  // put your setup code here, to run once:
  Heltec.begin(true,false,true,true,868E6);
  SPI.begin(SCK,MISO,MOSI,SS);
	
  LoRa.setPins(SS,RST_LoRa,DIO0);
	while (!LoRa.begin(868E6))
	{
		Serial.println("erreur lora");
		delay(1000);
	}

  LoRa.setSpreadingFactor(8);
	LoRa.setSyncWord(0x12);
	LoRa.setSignalBandwidth(125E3);
  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);
  
  LoRa.receive();

  byte error, address;
	int nDevices;

	Serial.println("Scanning...");

	nDevices = 0;
	for(address = 1; address < 127; address++ )
	{
		Wire.beginTransmission(address);
		error = Wire.endTransmission();

//		Wire1.beginTransmission(address);
//		error = Wire1.endTransmission();

		if (error == 0)
		{
			Serial.print("I2C device found at address 0x");
			if (address<16)
			Serial.print("0");
			Serial.print(address,HEX);
			Serial.println("  !");

			nDevices++;
		}
		else if (error==4)
		{
			Serial.print("Unknown error at address 0x");
			if (address<16)
				Serial.print("0");
			Serial.println(address,HEX);
		}
	}
	if (nDevices == 0)
	{
    Serial.println("No I2C devices found\n");
  }
	else {
	  Serial.println("done\n");
  }
  if (! ina219.begin()) {
    Serial.println("Failed to find INA219 chip 1");
    while (1) { delay(10); }
  }
  if (! ina219_2.begin()) {
    Serial.println("Failed to find INA219 chip 2");
    //while (1) { delay(10); }
  }
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS.");
    while (1);
  }
  ads.startADCReading(ADS1X15_REG_CONFIG_MUX_SINGLE_0, /*continuous=*/false);

  pinMode(2,INPUT);
  for (size_t i = 0; i < 10; i++)
  {
  Serial.println(offset_A);
    offset_A += analogRead(2);
  }

  Serial.println(offset_A);
  offset_A = ((offset_A/10)*3.3)/4095;
  Serial.println(offset_A);


pinMode(pinSCT013,INPUT);
}

void loop() {
  mesureSysteme();

  
  
  if (newMessage)
  {
    newMessage = false;
    TraitementCommande(receivedMessage.Content);

    sendMessage(MASTER,"ok",true);
    Serial.println("envoi msg");
    //LoRa.receive();
    //reactiveReception = true;
  }
  if (reactiveReception )
  {
    reactiveReception = false;
    LoRa.receive();
    Serial.println("Reactivation Reception");
  }
  
  
  float wcs_vol = (analogRead(2)*3.3)/4095;
  float wcs_a =  (wcs_vol - offset_A)/0.0269;

  Heltec.display->clear();
  // Heltec.display->drawString(0,0,(String)current_mA + " mA");
  // Heltec.display->drawString(0,15,"SCT013 "+(String)rawCurrencSCT013 + " " +(String)CurrentSCT013);
  
  // Heltec.display->drawString(0,30,(String)current_mA_2 + " mA");
  // Heltec.display->drawString(50,50,(String)wcs_a + " A");
  // Heltec.display->drawString(0,50,(String)wcs_vol + " V");
  
  // Heltec.display->drawString(60,0,"1 "+ String(rad1.getState()));
  // Heltec.display->drawString(60,10,"2 "+ String(rad2.getState()));

  Heltec.display->drawString(0,0,"Ina219");
  Heltec.display->drawString(0,10,"shunt vol "+(String)ina219.getShuntVoltage_mV()+ " mV");
  Heltec.display->drawString(0,20,"bus vol   "+(String)ina219.getBusVoltage_V()+ " V");
  Heltec.display->drawString(0,30,"current   "+(String)ina219.getCurrent_mA()+ " mA");

  
  
  Heltec.display->display();

 
  

  gestionSorties();

  if (!ads.conversionComplete()) {
    return;
  }

  int16_t results = ads.getLastConversionResults();

  Serial.print("Single : : "); Serial.print(results); Serial.print("("); Serial.print(ads.computeVolts(results)); Serial.println("mV)");

  // Start another conversion.
  ads.startADCReading(ADS1X15_REG_CONFIG_MUX_SINGLE_0, /*continuous=*/false);

  
  delay(100);
}