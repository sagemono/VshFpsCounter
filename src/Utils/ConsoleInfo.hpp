#pragma once
#include <string>
#include <stdint.h>
#include "SystemCalls.hpp"


struct memInfo_s
{ // in bytes;
   uint32_t total;
   uint32_t available;
};

struct memUsage_s
{ // in kilobytes
	float total;
	float free;
	float used;
	float percent;
};

float GetTemperatureCelsius(int dev_id);
float GetTemperatureFahrenheit(int dev_id);
float GetFanSpeed();
memUsage_s GetMemoryUsage();
float GetFirmwareVersion();
std::string GetFirmwareType();