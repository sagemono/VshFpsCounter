#pragma once
#include <string>
#include <stdint.h>
#include <sys/process.h>
#include "Utils/SystemCalls.hpp"

#define MARK_AS_EXECUTABLE __attribute__((section(".text")))

struct opd_s
{
   uint32_t func;
   uint32_t toc;
};

struct importStub_s
{
   int16_t ssize;
   int16_t header1;
   int16_t header2;
   int16_t imports;
   int32_t zero1;
   int32_t zero2;
   const char* name;
   uint32_t* fnid;
   opd_s** stub;
   int32_t zero3;
   int32_t zero4;
   int32_t zero5;
   int32_t zero6;
};

struct exportStub_s
{
   int16_t ssize;
   int16_t header1;
   int16_t header2;
   int16_t exports;
   int32_t zero1;
   int32_t zero2;
   const char* name;
   uint32_t* fnid;
   opd_s** stub;
};

opd_s* FindExportByName(const char* module, uint32_t fnid);
opd_s* FindImportByName(const char* module, uint32_t fnid);

class Detour
{
public:
   Detour();
   Detour(Detour const&) = delete;
   Detour(Detour&&) = delete;
   Detour& operator=(Detour const&) = delete;
   Detour& operator=(Detour&&) = delete;
   virtual ~Detour();

   virtual void Hook(uintptr_t fnAddress, uintptr_t fnCallback, uintptr_t tocOverride = 0);
   virtual bool UnHook();

   template <typename R, typename... TArgs>
   R GetOriginal(TArgs... args)
   {
      R(*original)(TArgs...) = (R(*)(TArgs...))m_TrampolineOpd;
      return original(args...);
   }

private:
   size_t Jump(void* destination, const void* branchTarget, bool linked, bool preserveRegister);
   size_t JumpWithOptions(void* destination, const void* branchTarget, bool linked, bool preserveRegister,
      uint32_t branchOptions, uint8_t conditionRegisterBit, uint8_t registerIndex);
   size_t RelocateBranch(uint32_t* destination, uint32_t* source);
   size_t RelocateCode(uint32_t* destination, uint32_t* source);
   size_t GetHookSize(const void* branchTarget, bool linked, bool preserveRegister);

protected:
   const void*  m_HookTarget;
   void*        m_HookAddress;
   uint8_t*     m_TrampolineAddress;
   uint32_t     m_TrampolineOpd[2];
   uint8_t      m_OriginalInstructions[30];
   size_t       m_OriginalLength;

   MARK_AS_EXECUTABLE static uint8_t   s_TrampolineBuffer[2048];
   static size_t                       s_TrampolineSize;
};

// list of fnids https://github.com/aerosoul94/ida_gel/blob/master/src/ps3/ps3.xml
class ImportExportDetour : public Detour
{
public:
   enum HookType { Import = 0, Export = 1 };

   ImportExportDetour(HookType type, const std::string& libaryName, uint32_t fnid, uintptr_t fnCallback);
   virtual ~ImportExportDetour();

private:
   void HookByFnid(HookType type, const std::string& libaryName, uint32_t fnid, uintptr_t fnCallback);

   std::string m_LibaryName;
   uint32_t m_Fnid;
};
