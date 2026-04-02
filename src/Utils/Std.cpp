#include "Std.hpp"

template<>
std::string to_string(uint64_t value)
{
   char buf[25];
   vsh::memset(buf, 0, 25);
   int len = vsh::snprintf(buf, sizeof(buf), "%llu", value);
   return std::string(buf);
}

std::string to_string(float value, int decimalPlaces)
{
   char buf[25];
   vsh::memset(buf, 0, 25);
   int len = vsh::snprintf(buf, sizeof(buf), "%.*f", decimalPlaces, value);
   return std::string(buf);
}

template<>
std::string to_string(double value)
{
   char buf[25];
   vsh::memset(buf, 0, 25);
   int len = vsh::snprintf(buf, sizeof(buf), "%.2f", value);
   return std::string(buf);
}

template<>
std::wstring to_wstring(uint64_t value)
{
   wchar_t buf[25];
   vsh::memset(buf, 0, 25);
   int len = vsh::swprintf(buf, sizeof(buf), L"%llu", value);
   return std::wstring(buf);
}

std::wstring to_wstring(float value, int decimalPlaces)
{
   wchar_t buf[25];
   vsh::memset(buf, 0, 25);
   int len = vsh::swprintf(buf, sizeof(buf), L"%.*f", decimalPlaces, value);
   return std::wstring(buf);
}

template<>
std::wstring to_wstring(double value)
{
   wchar_t buf[25];
   vsh::memset(buf, 0, 25);
   int len = vsh::swprintf(buf, sizeof(buf), L"%.2f", value);
   return std::wstring(buf);
}
