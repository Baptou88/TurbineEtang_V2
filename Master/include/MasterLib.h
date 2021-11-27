#ifndef _MasterLib_h
#define _MasterLib_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include "TurbineEtangLib.h"
#include "Heltec.h"

typedef enum {
	Manuel,
	Auto
}EmodeTurbine;

String EmodeTurbinetoString(size_t m);

class Command
{
public:
	String Name;
	String Type;
	String Action;

private:

};
class board
{
public:
	board(String N, byte Address) {
		Name = N;
		localAddress = Address;
		
	}
	String Name;
	byte localAddress;


	/**
	 * @brief indique le millis du dernier message recu
	 * 
	 */
	unsigned long lastmessage = 0;

	/**
	 * @brief stock le dernier message recu
	 * 
	 */
	Message LastMessage;

	/**
	 * @brief retourne les infos sous forme de json
	 * 
	 * @return String 
	 */
	String toJson() {
		if (this->localAddress == MASTER)
		{
			this->lastmessage = millis();
		}
		
		return "{ \"Name\" : \"" + Name + "\"," +
			"\"localAddress\" : " + localAddress + "," +
			"\"lastMessage\" : " + this->LastMessage.toJson() + "," +
			"\"lastUpdate\" : " + lastmessage + "" + 
			"}";
	};

	/**
	 * @brief indique si un nouveau message est recu
	 * 
	 */
	bool newMessage = false;

	bool waitforResponse = false;
	unsigned long lastDemandeStatut = 0;
	

	Command Commands[10];
	void AddCommand(String Name, int id, String Type, String Action) {
		Commands[id].Action = Action;
		Commands[id].Name = Name;
		Commands[id].Type = Type;
	}

	bool isConnected(){
		if ((millis() - lastmessage < 600000) && lastmessage !=0 )
		{
			return true;
		} else
		{
			return false;
		}
		
		
	}
	void sendMessage(byte destination, String outgoing)
	{
		LoRa.beginPacket();                   // start packet
		LoRa.write(destination);              // add destination address
		LoRa.write(localAddress);             // add sender address
		LoRa.write(0);                 // add message ID
		LoRa.write(outgoing.length());        // add payload length
		LoRa.print(outgoing);                 // add payload
		LoRa.endPacket();                     // finish packet and send it
		//msgCount++;                           // increment message ID
	}

	void demandeStatut (){
		if (millis()- lastDemandeStatut > 30000)
		{
			Serial.println("Classe demandestatut 0x" + String(localAddress,HEX));
			lastDemandeStatut = millis();
			sendMessage(localAddress, "DemandeStatut");
			LoRa.receive();
			waitforResponse = true;
		}
	}

private:
	
};




#endif
