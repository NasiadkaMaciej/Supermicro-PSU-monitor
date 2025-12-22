#include <Arduino.h>
#include <Wire.h>

// Cables: SDA to A4, SCL to A5 on Arduino Nano
// Red - VCC (5V), Black - GND

// Configuration
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t I2C_CLOCK = 100000;
constexpr uint32_t UPDATE_INTERVAL = 2000;

// Register Map
namespace Reg {
constexpr uint8_t TEMPERATURE = 0x09;
constexpr uint8_t FAN1_SPEED = 0x0A;
constexpr uint8_t FAN2_SPEED = 0x0B;
constexpr uint8_t POWER_STATUS = 0x0C;
constexpr uint8_t AC_CURRENT = 0x14;
constexpr uint8_t INPUT_VOLTAGE = 0xF4;
constexpr uint8_t INPUT_POWER_L = 0xF5;
constexpr uint8_t INPUT_POWER_H = 0xF6;
}

// Constants
// Formula: RPM = (1/0.262) * (Count * 60 / 2)
constexpr float FAN_RPM_FACTOR = 60.0f / (2.0f * 0.262f);

// Data structures
struct PSUData {
	bool online;
	bool dcGood;
	uint8_t temperature;
	uint16_t fan1_rpm;
	uint16_t fan2_rpm;
	float input_voltage;
	float input_current;
	uint16_t input_power;
};

class SupermicroPSU {
  private:
	uint8_t _i2cAddress;
	uint8_t _psuIndex;
	PSUData _data;

	bool readByte(uint8_t reg, uint8_t& value) {
		Wire.beginTransmission(_i2cAddress);
		Wire.write(reg);
		if (Wire.endTransmission(false) != 0) return false;

		if (Wire.requestFrom(_i2cAddress, (uint8_t)1) != 1) return false;
		value = Wire.read();
		return true;
	}

	bool readWord(uint8_t regLow, uint8_t regHigh, uint16_t& value) {
		uint8_t low, high;
		if (!readByte(regLow, low)) return false;
		if (!readByte(regHigh, high)) return false;
		value = (uint16_t(high) << 8) | low;
		return true;
	}

	uint16_t rawFanToRPM(uint8_t raw) {
		if (raw == 0) return 0;
		return static_cast<uint16_t>(raw * FAN_RPM_FACTOR);
	}

  public:
	SupermicroPSU(uint8_t index, uint8_t address)
	  : _i2cAddress(address)
	  , _psuIndex(index) {
		_data.online = false;
	}

	bool update() {
		// 1. Check connectivity
		Wire.beginTransmission(_i2cAddress);
		Wire.write(Reg::TEMPERATURE);
		if (Wire.endTransmission(false) != 0) {
			_data.online = false;
			return false;
		}

		_data.online = true;
		uint8_t rawVal;

		// Temperature & Status
		if (readByte(Reg::TEMPERATURE, rawVal)) _data.temperature = rawVal;
		if (readByte(Reg::POWER_STATUS, rawVal)) _data.dcGood = (rawVal & 0x01);

		// Fans (Read both 0x0A and 0x0B)
		if (readByte(Reg::FAN1_SPEED, rawVal)) _data.fan1_rpm = rawFanToRPM(rawVal);
		if (readByte(Reg::FAN2_SPEED, rawVal)) _data.fan2_rpm = rawFanToRPM(rawVal);

		// Input Metrics
		if (readByte(Reg::INPUT_VOLTAGE, rawVal)) _data.input_voltage = (float)rawVal;
		if (readByte(Reg::AC_CURRENT, rawVal)) _data.input_current = rawVal / 16.0f;

		uint16_t watts;
		if (readWord(Reg::INPUT_POWER_L, Reg::INPUT_POWER_H, watts)) _data.input_power = watts;

		return true;
	}

	void print() {
		char buffer[128];
		unsigned long timestamp = millis() / 1000;

		Serial.print(F("["));
		Serial.print(timestamp);
		Serial.print(F("s] PSU #"));
		Serial.print(_psuIndex);
		Serial.print(F(": "));

		if (!_data.online) {
			Serial.println(F("--- OFFLINE ---"));
			return;
		}

		// Output Format: Temp | Fan1 / Fan2 | Status | Volts / Amps / Watts
		snprintf(buffer,
				 sizeof(buffer),
				 "%dC | %d/%d rpm | %s | ",
				 _data.temperature,
				 _data.fan1_rpm,
				 _data.fan2_rpm,
				 _data.dcGood ? "DC OK" : "FAULT");
		Serial.print(buffer);

		Serial.print(_data.input_voltage, 0);
		Serial.print(F("V / "));
		Serial.print(_data.input_current, 2);
		Serial.print(F("A / "));
		Serial.print(_data.input_power);
		Serial.println(F("W"));
	}
};

SupermicroPSU psus[] = { SupermicroPSU(1, 0x38), SupermicroPSU(2, 0x39) };

void setup() {
	Serial.begin(SERIAL_BAUD);
	while (!Serial)
		delay(10);

	Wire.begin();
	Wire.setClock(I2C_CLOCK);

	Serial.println(F("\nSupermicro PSU monitor"));
}

void loop() {
	static unsigned long lastUpdate = 0;

	if (millis() - lastUpdate >= UPDATE_INTERVAL) {
		lastUpdate = millis();

		for (int i = 0; i < 2; i++) {
			psus[i].update();
			psus[i].print();
		}
	}
}