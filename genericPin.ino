#include <EEPROM.h>
//#define SERIAL_TRACE // Comment to disable trace to serial port

#if defined SERIAL_TRACE
#define serialTrace(message) (Serial.println(message))
#else
#define serialTrace(message)
#endif

#define PIN_CONFIG 0x40 // Pin required to be configured
#define PIN_DIGITAL_OUTPUT 0x20 // Pin is digital output
#define PIN_PWM_OUTPUT 0x10 // Pin is PWM output
#define PIN_DIGITAL_INPUT 0x08 // Pin is digital input
#define PIN_DIGITAL_INPUT_PULLUP 0x04 // Enable internal Pull-up resistor (digital input)

#include <avr/wdt.h>
#ifndef _SOFT_RESTART_H
#define _SOFT_RESTART_H
#define soft_restart() \
do \
{ \
	wdt_enable(WDTO_15MS); \
	for(;;) { \
	} \
} while(0)
#endif

// Function Pototype
void wdt_init(void) __attribute__((naked)) __attribute__((section(".init3")));

// Function Implementation
void wdt_init(void)
{
	MCUSR = 0;
 	wdt_disable();
 	return;
}

const char FIRMWARE_VERSION[] = "1.0.0";
const char FIRMWARE_NAME[] = "GenericPin";

// these values are saved in EEPROM
const byte EEPROM_ID1 = 'G'; // used to identify if valid data in EEPROM
const byte EEPROM_ID2 = 'e'; // used to identify if valid data in EEPROM
const byte EEPROM_ID3 = 'n'; // used to identify if valid data in EEPROM
const byte LED_PIN = 14; // the number of the LED pin

const int ID_ADDR = 0x0000; // the EEPROM address used to store the ID
const int VER_ADDR = 0x0008; // the EEPROM address used to store the Firmware version
const int DEVICE_NAME_ADDR = 0x0040;
const int DEVICE_NAME_ADDR_END = 0x0080;

const int DIGITAL_PINS = 20;
const int ANALOG_PINS = 6;

// Each byte configs the digital pins 0-19 (14-19 are analog pins, if needed)
const int CONFIG_DIGITAL_ADDR = DEVICE_NAME_ADDR_END; // start address of digital pins
const int CONFIG_DIGITAL_ADDR_END = CONFIG_DIGITAL_ADDR + DIGITAL_PINS; // exlusive

const int STATE_DIGITAL_ADDR = CONFIG_DIGITAL_ADDR_END; // the state of digital pins stored here
const int STATE_DIGITAL_ADDR_END = STATE_DIGITAL_ADDR + (DIGITAL_PINS * 2); // exclusive

const int PWM_PINS[] = {3, 5, 6, 9, 10, 11};
const int PWM_PINS_SIZE = (sizeof(PWM_PINS) / sizeof(int));
const int STATE_ANALOG_ADDR = STATE_DIGITAL_ADDR_END; // state of PWM pins, 
const int STATE_ANALOG_ADDR_END = STATE_ANALOG_ADDR + DIGITAL_PINS; // state of PWM pins, 

const int MAX_COMMAND_LEN = 200;

byte digitalConfig[DIGITAL_PINS];
int digitalState[DIGITAL_PINS];

class CommandBuffer {
public:
	CommandBuffer() : p(0), ready(false) {
	}
	boolean append(char c) {
		if(c == '\n') {
			ready = true;
		} else if(p < MAX_COMMAND_LEN) {
			buffer[p++] = c;
		} else {
			ready = true;
		}
		return ready;
	}
	int length() const {
		return p;
	}
	char charAt(int pos) const {
		return ((pos < p) ? buffer[pos] : '\n');
	}
	void clean() {
		p = 0;
		ready = false;
	}
	boolean isReady() const {
		return ready;
	}
	int getDebugCommandLength() const {
		return ((length() >= 5) && (strncmp("DEBUG", buffer, 5) == 0)) ? 5 : 0;
	}
private:
	int p;
	boolean ready;
	char buffer[MAX_COMMAND_LEN];
};

CommandBuffer command;

class CommandProcessor {
private:	
	CommandBuffer& command;
	int p;
	boolean debug;
	int pin;
public:
	CommandProcessor(CommandBuffer& aCommand)
	: command(aCommand), p(command.getDebugCommandLength()), debug(p > 0), pin(-1) {
	} 

	boolean isDebug() const {
		return debug;
	}
	
	boolean process() {
		if(isDebug() && !skipSpaces()) {
			sendError("Whitespace required after DEBUG command");
			return false;
		}
		switch (getChar()) {
		case 'C':
			return processConfigCommand();
		case 'D':
			return processDigitalCommand();
		case 'P':
			return processPWMCommand();
		case 'A':
			return processAnalogCommand();
		case 'N':
			return processNameCommand();
		case 'F':
			return processFirmwareCommand();
		default:
			return processUnknownCommand();
		}
	}
	
protected:
	char getChar() {
		return command.charAt(p++);
	}

	char peekChar() {
		return command.charAt(p);
	}

	void forwardChar() {
		++p;
	}

	// TODO: ? make stricter, error if space not found where it is required in command
	boolean skipSpaces() {
		int result = 0;
		for(char c = peekChar(); c == ' ' || c == '\t'; c = peekChar()) {
			++result;
			forwardChar();
		}
		return (result > 0);
	}
	
	boolean skipSpacesAndP() {
		skipSpaces();
		return (getChar() == 'P');
	}

	char getAfterSpace() {
		skipSpaces();
		return getChar();
	}

	boolean parsePin() {
		if(!skipSpacesAndP()) {
			sendError("Pin not found");
			return false;
		}
		pin = parseInt();
		if(pin < 0) {
			sendError("Pin number not found");
			return false;
		} else if(pin < 2) {
			sendError("Access denied to serial pins");
			return false;
		} else if(pin >= DIGITAL_PINS) {
			sendError("There are no such many pins");
			return false;
		}
		return true;
	}

	boolean assertPWM() {
		for(int i = 0; i < PWM_PINS_SIZE; ++i) {
			if(PWM_PINS[i] == pin) return true;
		}
		sendError("Pin can't be used for PWM");
		return false;
	}

	boolean isDigit(char c) {
		return (c >= '0' && c <= '9');
	}

	int parseInt() {
		if(!isDigit(peekChar())) {
			sendError("Can't parse number");
			return -1;
		}
		int value = 0;
		int digits = 0;
		for(char c = peekChar(); c >= '0' && c <= '9'; c = peekChar()) {
			if(++digits > 4) {
				sendError("Number is too big");
				return -1;
			} 
			value = value * 10 + (c - '0');
			forwardChar();
		}
		return value;
	}

	void sendError(char* message) {
		if(!debug) {
			//Serial.println(); // ESP8266 doesn't like it too (infinite loop)
			return;
		}
		Serial.print("ERROR at ");
		if(!message) {
			Serial.println(p);
		} else {
			Serial.print(p);
			Serial.print(": ");
			Serial.println(message);
		}
	}

	void sendOkPart() {
		Serial.print("OK ");
	}	

	boolean processConfigCommand() {
		char c = getChar();
		switch(c) {
		case 'S':
		case 'E':
			if(!parsePin()) return false;
			return processConfigSetCommand(c == 'E');
		case 'G':
			if(!parsePin()) return false;
			return processConfigGetCommand();
		default:
			sendError("Unknown Config command");
			return false;
		}
	}

	boolean processConfigSetCommand(boolean saveToEEPROM) {
		byte config = PIN_CONFIG;
		int state = 0;
		char c;
		switch(getAfterSpace()) {
		case 'I': // Pin is configured to be INPUT
			config |= PIN_DIGITAL_INPUT;
			c = getChar();
			if(c == '1') { // Turn on pullup resistor
				config |= PIN_DIGITAL_INPUT_PULLUP;
				pinMode(pin, INPUT_PULLUP);
			} else {
				pinMode(pin, INPUT);
			}
			state = digitalRead(pin);
			break;
		case 'O': // Pin is configured to be OUTPUT
			c = getChar();
			pinMode(pin, OUTPUT);
			if(c == 'D') {
				config |= PIN_DIGITAL_OUTPUT;
			} else if(c = 'P') {
				config |= PIN_PWM_OUTPUT;
				if(!assertPWM()) return false;
			} else {
				config |= PIN_DIGITAL_OUTPUT;
				sendError("Unknown output pin configuration");
				return false; // be strict
			}
			break;
		default:
			sendError("Unknown pin configuration");
			return false;
		}
		digitalConfig[pin] = config;
		digitalState[pin] = state;
		if(saveToEEPROM) {
			EEPROM.write(CONFIG_DIGITAL_ADDR + pin, config);
			saveInt(STATE_DIGITAL_ADDR + pin * 2, state);
		}
		return processConfigGetCommand();
	}

	boolean processConfigGetCommand() {
		byte config = digitalConfig[pin];
		sendOkPart();
		if(config & PIN_CONFIG) {
			if(config & PIN_DIGITAL_INPUT) {
				Serial.print("I");
				if(config & PIN_DIGITAL_INPUT_PULLUP) {
					Serial.print('1');
				} else {
					Serial.print('0');
				}
			} else if(config & PIN_PWM_OUTPUT) {
				Serial.print("OP");
			} else if(config & PIN_DIGITAL_OUTPUT) {
				Serial.print("OD");
			}
		}
		Serial.println();
		return true;
	}

	boolean processDigitalCommand() {
		char c = getChar();
		switch(c) {
		case 'S':
		case 'E':
			if(!parsePin()) return false;
			return processDigitalSetCommand(c == 'E');
		case 'G':
			if(!parsePin()) return false;
			return processDigitalGetCommand();
		default:
			sendError("Unknown Digital command");
			return false;
		}
	}

	boolean processDigitalSetCommand(boolean saveToEEPROM) {
		char c = getAfterSpace();
		if(c != 'V') {
			sendError("Digital value to set not specified");
			return false;
		}
		int state = parseInt();
		if(state < 0) return false;
		if(state > 0) {
			state = 1;
		}
		pinMode(pin, OUTPUT);
		digitalWrite(pin, (state > 0) ? HIGH : LOW);
		byte config = PIN_CONFIG | PIN_DIGITAL_OUTPUT;
		digitalConfig[pin] = config;
		digitalState[pin] = state;
		if(saveToEEPROM) {
			EEPROM.write(CONFIG_DIGITAL_ADDR + pin, config);
			saveInt(STATE_DIGITAL_ADDR + pin * 2, state);
		}
		return processDigitalGetCommand();
	}

	boolean processDigitalGetCommand() {
		byte config = digitalConfig[pin];
		sendOkPart();
		Serial.print("V");
		int state = 0;
		if(config & PIN_CONFIG) {
			if(config & PIN_DIGITAL_INPUT) {
				state = digitalRead(pin);
			} else if(config & (PIN_PWM_OUTPUT | PIN_DIGITAL_OUTPUT)) {
				state = digitalState[pin];
			}
		} else {
			pinMode(pin, INPUT);
			state = digitalRead(pin);
			digitalState[pin] = state;
		}
		Serial.println(state);
		return true;
	}

	boolean processPWMCommand() {
		char c = getChar();
		switch(c) {
		case 'S':
		case 'E':
			if(!parsePin()) return false;
			if(!assertPWM()) return false;
			return processPWMSetCommand(c == 'E');
		case 'G':
			if(!parsePin()) return false;
			return processDigitalGetCommand();
		default:
			sendError("Unknown PWM command");
			return false;
		}
	}

	boolean processPWMSetCommand(boolean saveToEEPROM) {
		char c = getAfterSpace();
		if(c != 'V') {
			sendError("PWM value to set not specified");
			return false;
		}
		int state = parseInt();
		if(state < 0) return false;
		if(state > 1023) state = 1023;
		analogWrite(pin, state / 4);
		byte config = PIN_CONFIG | PIN_PWM_OUTPUT;
		digitalConfig[pin] = config;
		digitalState[pin] = state;
		if(saveToEEPROM) {
			EEPROM.write(CONFIG_DIGITAL_ADDR + pin, config);
			saveInt(STATE_DIGITAL_ADDR + pin * 2, state);
		}
		return processDigitalGetCommand();
		
	}

	boolean processAnalogCommand() {
		char c = getChar();
		if(c != 'G') {
			sendError("Unknown Analog command");
			return false;
		}
		if(!parsePin()) return false;
		if(pin >= ANALOG_PINS) {
			sendError("Analog pin number is too big");
		}
		int state = analogRead(pin);
		sendOkPart();
		Serial.print("V");
		Serial.println(state);
	}

	boolean processNameCommand() {
		char c = getChar();
		switch(c) {
		case 'S':
		case 'E':
			return processNameSetCommand();
		case 'G':
			return processNameGetCommand();
		default:
			sendError("Unknown Name command");
			return false;
		}
	}

	boolean processNameSetCommand() {
		skipSpaces();
		int addr = DEVICE_NAME_ADDR;
		for(char c = getChar(); addr < DEVICE_NAME_ADDR_END && c != '\0' && c != '\n' ; c = getChar(), ++addr) {
			saveChar(addr, c);
		}
		if(addr < DEVICE_NAME_ADDR_END) saveChar(addr, '\0');
		return processNameGetCommand();
	}

	boolean processNameGetCommand() {
		sendOkPart();
		int addr = DEVICE_NAME_ADDR;
		for(char c = loadChar(addr); addr < DEVICE_NAME_ADDR_END && c != '\0'; c = loadChar(++addr)) {
			Serial.print(c);
		}
		Serial.println();
	}

	boolean processFirmwareCommand() {
		char c = getChar();
		switch(c) {
		case 'S':
		case 'E':
			return processFirmwareSetCommand();
		case 'G':
			return processFirmwareGetCommand();
		default:
			sendError("Unknown Firmware command");
			return false;
		}
	}

	boolean processFirmwareGetCommand() {
		skipSpaces();
		sendOkPart();
		char c = getChar();
		switch(c) {
		case 'V':
			Serial.print(FIRMWARE_VERSION);
			break;
		case 'N':
			Serial.print(FIRMWARE_NAME);
			break;
		default:
			Serial.print(FIRMWARE_NAME);
			Serial.print(' ');
			Serial.print(FIRMWARE_VERSION);
		}
		Serial.println();
		return true;
	}

	boolean processFirmwareSetCommand() {
		skipSpaces();
		char c = getChar();
		switch(c) {
		case 'A':
			sendOkPart();
			initEEPROM();
			Serial.println("All configuration reset");
			break;
		case 'P':
			sendOkPart();
			initConfigEEPROM();
			Serial.println("Pin configuration reset");
			break;
		case 'R':
			sendOkPart();
			Serial.println("Restart");
			break;
		default:
			sendError("Unknown Firmware command");
			return false;
		}
		//soft_restart(); //infinite restart loop
		resetState();
		loadEEPROM();
	}

	boolean processUnknownCommand() {
		sendError("Unknown Command");
		return false;
	}
};

boolean checkEEPROM() {
	return (EEPROM.read(ID_ADDR) == EEPROM_ID1
		&& EEPROM.read(ID_ADDR + 1) == EEPROM_ID2
		&& EEPROM.read(ID_ADDR + 2) == EEPROM_ID3);
}

void initEEPROM() {
	serialTrace("Writing default data to EEPROM");
	EEPROM.write(ID_ADDR,EEPROM_ID1);
	EEPROM.write(ID_ADDR + 1,EEPROM_ID2);
	EEPROM.write(ID_ADDR + 2,EEPROM_ID3);
	for(int i = ID_ADDR + 3; i < CONFIG_DIGITAL_ADDR; ++i) {
		EEPROM.write(i, 0x00);
	}
	initConfigEEPROM();
}

void initConfigEEPROM() {
	for(int i = CONFIG_DIGITAL_ADDR; i < STATE_ANALOG_ADDR_END; ++i) {
		EEPROM.write(i, 0x00);
	}
	for(int i = 0; i < DIGITAL_PINS; ++i) {
		digitalConfig[i] = 0x00;
	}
	for(int i = 0; i < DIGITAL_PINS; ++i) {
		digitalState[i] = 0;
	}
}

void loadEEPROM() {
	serialTrace("Using data from EEPROM");
	loadPinConfig();
}

void resetState() {
	for(int i = 2; i < DIGITAL_PINS; ++i) {
		pinMode(i, INPUT);
	}
}

void loadPinConfig() {
	for(int i = CONFIG_DIGITAL_ADDR; i < CONFIG_DIGITAL_ADDR_END; ++i) {
		byte b = EEPROM.read(i);
		int pin = i - CONFIG_DIGITAL_ADDR;
		int stateAddr = STATE_DIGITAL_ADDR + pin * 2;
		digitalConfig[pin] = b;
		if(b & PIN_CONFIG) {
			int state = loadInt(stateAddr);
			digitalState[pin] = state;
			if(b & PIN_DIGITAL_OUTPUT) {
				pinMode(pin, OUTPUT);
				digitalWrite(pin, state);
			} else if(b & PIN_PWM_OUTPUT) {
				analogWrite(pin, state);
			} else if(b & PIN_DIGITAL_INPUT_PULLUP) {
				pinMode(pin, INPUT_PULLUP);
			} else if(b & PIN_DIGITAL_INPUT) {
				pinMode(pin, INPUT);
			}
			if(b & PIN_DIGITAL_INPUT) {
				digitalState[pin] = digitalRead(pin);
			}
		} else {
			digitalState[pin] = 0;
		}
	}
}

int loadInt(int addr) {
	byte hiByte = EEPROM.read(addr);
	byte lowByte = EEPROM.read(addr+1);
	return word(hiByte, lowByte);
}

void saveInt(int addr, int value) {
	byte hiByte = highByte(value);
	byte loByte = lowByte(value);
	EEPROM.write(addr, hiByte);
	EEPROM.write(addr + 1, loByte);
}

char loadChar(int addr) {
	return (char) EEPROM.read(addr);
}

void saveChar(int addr, char value) {
	EEPROM.write(addr, (byte) value);
}

void setup() {
	Serial.begin(115200);
	if(checkEEPROM()) {
		loadEEPROM();
	} else {
		initEEPROM();
	}
}

void loop() {
	if(command.isReady()) {
		CommandProcessor processor(command);
		processor.process();
		command.clean();
	}
}

void serialEvent() {
	while(Serial.available()) {
		if(command.append((char)Serial.read())) {
			if(Serial.available() && (((char)Serial.peek()) == '\n')) {
				// Workaround for esp8266 sending welcome gibberish on start.
				// Side effect of the workaround - if "\n\n" happened
				// after entering command, the command is ignored.
				Serial.read();
				command.clean();
			}
			break;
		}
	}
}
