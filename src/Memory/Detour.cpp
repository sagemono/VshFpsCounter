#include "Detour.hpp"

#define POWERPC_REGISTERINDEX_R0      0
#define POWERPC_REGISTERINDEX_R1      1
#define POWERPC_REGISTERINDEX_RTOC    2

#define MASK_N_BITS(N) ( ( 1 << ( N ) ) - 1 )

#define POWERPC_HI(X) ( ( X >> 16 ) & 0xFFFF )
#define POWERPC_LO(X) ( X & 0xFFFF )

// PowerPC most significant bit is addressed as bit 0 in documentation.
#define POWERPC_BIT32(N) ( 31 - N )

// Opcode is bits 0-5.
// Allowing for op codes ranging from 0-63.
#define POWERPC_OPCODE(OP)       (uint32_t)( OP << 26 )
#define POWERPC_OPCODE_ADDI      POWERPC_OPCODE( 14 )
#define POWERPC_OPCODE_ADDIS     POWERPC_OPCODE( 15 )
#define POWERPC_OPCODE_BC        POWERPC_OPCODE( 16 )
#define POWERPC_OPCODE_B         POWERPC_OPCODE( 18 )
#define POWERPC_OPCODE_BCCTR     POWERPC_OPCODE( 19 )
#define POWERPC_OPCODE_ORI       POWERPC_OPCODE( 24 )
#define POWERPC_OPCODE_EXTENDED  POWERPC_OPCODE( 31 )
#define POWERPC_OPCODE_LD        POWERPC_OPCODE( 58 )
#define POWERPC_OPCODE_STD       POWERPC_OPCODE( 62 )
#define POWERPC_OPCODE_MASK      POWERPC_OPCODE( 63 )

#define POWERPC_EXOPCODE(OP)     ( OP << 1 )
#define POWERPC_EXOPCODE_BCCTR   POWERPC_EXOPCODE( 528 )
#define POWERPC_EXOPCODE_MTSPR   POWERPC_EXOPCODE( 467 )

// SPR field is encoded as two 5 bit bitfields.
#define POWERPC_SPR(SPR) (uint32_t)( ( ( SPR & 0x1F ) << 5 ) | ( ( SPR >> 5 ) & 0x1F ) )

#define POWERPC_ADDI(rD, rA, SIMM)  (uint32_t)( POWERPC_OPCODE_ADDI | ( rD << POWERPC_BIT32( 10 ) ) | ( rA << POWERPC_BIT32( 15 ) ) | SIMM )
#define POWERPC_ADDIS(rD, rA, SIMM) (uint32_t)( POWERPC_OPCODE_ADDIS | ( rD << POWERPC_BIT32( 10 ) ) | ( rA << POWERPC_BIT32( 15 ) ) | SIMM )
#define POWERPC_LIS(rD, SIMM)       POWERPC_ADDIS( rD, 0, SIMM )
#define POWERPC_MTSPR(SPR, rS)      (uint32_t)( POWERPC_OPCODE_EXTENDED | ( rS << POWERPC_BIT32( 10 ) ) | ( POWERPC_SPR( SPR ) << POWERPC_BIT32( 20 ) ) | POWERPC_EXOPCODE_MTSPR )
#define POWERPC_MTCTR(rS)           POWERPC_MTSPR( 9, rS )
#define POWERPC_ORI(rS, rA, UIMM)   (uint32_t)( POWERPC_OPCODE_ORI | ( rS << POWERPC_BIT32( 10 ) ) | ( rA << POWERPC_BIT32( 15 ) ) | UIMM )
#define POWERPC_BCCTR(BO, BI, LK)   (uint32_t)( POWERPC_OPCODE_BCCTR | ( BO << POWERPC_BIT32( 10 ) ) | ( BI << POWERPC_BIT32( 15 ) ) | ( LK & 1 ) | POWERPC_EXOPCODE_BCCTR )
#define POWERPC_STD(rS, DS, rA)     (uint32_t)( POWERPC_OPCODE_STD | ( rS << POWERPC_BIT32( 10 ) ) | ( rA << POWERPC_BIT32( 15 ) ) | ( (int16_t)DS & 0xFFFF ) )
#define POWERPC_LD(rS, DS, rA)      (uint32_t)( POWERPC_OPCODE_LD | ( rS << POWERPC_BIT32( 10 ) ) | ( rA << POWERPC_BIT32( 15 ) ) | ( (int16_t)DS & 0xFFFF ) )

#define POWERPC_BRANCH_LINKED    1
#define POWERPC_BRANCH_ABSOLUTE  2
#define POWERPC_BRANCH_OPTIONS_ALWAYS ( 20 )

// --- Helpers inlined from Memory.cpp ---

static uint32_t GetCurrentToc()
{
   uint32_t* entry_point = *reinterpret_cast<uint32_t**>(0x1001C); // ElfHeader->e_entry
   return entry_point[1];
}

static int WriteProcessMemory(uint32_t pid, void* address, const void* data, size_t size)
{
   int ret = sys_dbg_write_process_memory(pid, address, data, size);
   if (ret == SUCCEEDED)
      return ret;

   return PS3MAPISetMemory(pid, address, data, size);
}

// --- Detour ---

uint8_t Detour::s_TrampolineBuffer[]{};
size_t Detour::s_TrampolineSize = 0;

Detour::Detour()
   : m_HookTarget(nullptr), m_HookAddress(nullptr), m_TrampolineAddress(nullptr), m_OriginalLength(0)
{
   memset(m_TrampolineOpd, 0, sizeof(m_TrampolineOpd));
   memset(m_OriginalInstructions, 0, sizeof(m_OriginalInstructions));
}

Detour::~Detour()
{
   UnHook();
}

size_t Detour::GetHookSize(const void* branchTarget, bool linked, bool preserveRegister)
{
   return JumpWithOptions(nullptr, branchTarget, linked, preserveRegister, POWERPC_BRANCH_OPTIONS_ALWAYS, 0, POWERPC_REGISTERINDEX_R0);
}

size_t Detour::Jump(void* destination, const void* branchTarget, bool linked, bool preserveRegister)
{
   return JumpWithOptions(destination, branchTarget, linked, preserveRegister, POWERPC_BRANCH_OPTIONS_ALWAYS, 0, POWERPC_REGISTERINDEX_R0);
}

size_t Detour::JumpWithOptions(void* destination, const void* branchTarget, bool linked, bool preserveRegister, uint32_t branchOptions, uint8_t conditionRegisterBit, uint8_t registerIndex)
{
   uint32_t BranchFarAsm[] = {
       POWERPC_LIS(registerIndex, POWERPC_HI((uint32_t)branchTarget)),
       POWERPC_ORI(registerIndex, registerIndex, POWERPC_LO((uint32_t)branchTarget)),
       POWERPC_MTCTR(registerIndex),
       POWERPC_BCCTR(branchOptions, conditionRegisterBit, linked)
   };

   uint32_t BranchFarAsmPreserve[] = {
       POWERPC_STD(registerIndex, -0x30, POWERPC_REGISTERINDEX_R1),
       POWERPC_LIS(registerIndex, POWERPC_HI((uint32_t)branchTarget)),
       POWERPC_ORI(registerIndex, registerIndex, POWERPC_LO((uint32_t)branchTarget)),
       POWERPC_MTCTR(registerIndex),
       POWERPC_LD(registerIndex, -0x30, POWERPC_REGISTERINDEX_R1),
       POWERPC_BCCTR(branchOptions, conditionRegisterBit, linked)
   };

   uint32_t* BranchAsm = preserveRegister ? BranchFarAsmPreserve : BranchFarAsm;
   size_t BranchAsmSize = preserveRegister ? sizeof(BranchFarAsmPreserve) : sizeof(BranchFarAsm);

   if (destination)
      WriteProcessMemory(sys_process_getpid(), destination, BranchAsm, BranchAsmSize);

   return BranchAsmSize;
}

size_t Detour::RelocateBranch(uint32_t* destination, uint32_t* source)
{
   uint32_t Instruction = *source;
   uint32_t InstructionAddress = (uint32_t)source;

   if (Instruction & POWERPC_BRANCH_ABSOLUTE)
   {
      WriteProcessMemory(sys_process_getpid(), destination, &Instruction, sizeof(Instruction));
      return sizeof(Instruction);
   }

   int32_t  BranchOffsetBitSize = 0;
   int32_t  BranchOffsetBitBase = 0;
   uint32_t BranchOptions = 0;
   uint8_t  ConditionRegisterBit = 0;

   switch (Instruction & POWERPC_OPCODE_MASK)
   {
   case POWERPC_OPCODE_B:
      BranchOffsetBitSize = 24;
      BranchOffsetBitBase = 2;
      BranchOptions = POWERPC_BRANCH_OPTIONS_ALWAYS;
      ConditionRegisterBit = 0;
      break;
   case POWERPC_OPCODE_BC:
      BranchOffsetBitSize = 14;
      BranchOffsetBitBase = 2;
      BranchOptions = (Instruction >> POWERPC_BIT32(10)) & MASK_N_BITS(5);
      ConditionRegisterBit = (Instruction >> POWERPC_BIT32(15)) & MASK_N_BITS(5);
      break;
   }

   int32_t BranchOffset = Instruction & (MASK_N_BITS(BranchOffsetBitSize) << BranchOffsetBitBase);

   if (BranchOffset >> ((BranchOffsetBitSize + BranchOffsetBitBase) - 1))
      BranchOffset |= ~MASK_N_BITS(BranchOffsetBitSize + BranchOffsetBitBase);

   void* BranchAddress = reinterpret_cast<void*>(InstructionAddress + BranchOffset);

   return JumpWithOptions(destination, BranchAddress, Instruction & POWERPC_BRANCH_LINKED, true, BranchOptions, ConditionRegisterBit, POWERPC_REGISTERINDEX_R0);
}

size_t Detour::RelocateCode(uint32_t* destination, uint32_t* source)
{
   uint32_t Instruction = *source;

   switch (Instruction & POWERPC_OPCODE_MASK)
   {
   case POWERPC_OPCODE_B:
   case POWERPC_OPCODE_BC:
      return RelocateBranch(destination, source);
   default:
      WriteProcessMemory(sys_process_getpid(), destination, &Instruction, sizeof(Instruction));
      return sizeof(Instruction);
   }
}

void Detour::Hook(uintptr_t fnAddress, uintptr_t fnCallback, uintptr_t tocOverride)
{
   m_HookAddress = reinterpret_cast<void*>(fnAddress);
   m_HookTarget = reinterpret_cast<void*>(*reinterpret_cast<uintptr_t*>(fnCallback));

   size_t HookSize = GetHookSize(m_HookTarget, false, false);

   WriteProcessMemory(sys_process_getpid(), m_OriginalInstructions, m_HookAddress, HookSize);

   m_OriginalLength = HookSize;

   m_TrampolineAddress = &s_TrampolineBuffer[s_TrampolineSize];

   for (size_t i = 0; i < (HookSize / sizeof(uint32_t)); i++)
   {
      uint32_t* InstructionAddress = reinterpret_cast<uint32_t*>((uint32_t)m_HookAddress + (i * sizeof(uint32_t)));
      s_TrampolineSize += RelocateCode((uint32_t*)&s_TrampolineBuffer[s_TrampolineSize], InstructionAddress);
   }

   void* AfterBranchAddress = reinterpret_cast<void*>((uint32_t)m_HookAddress + HookSize);
   s_TrampolineSize += Jump(&s_TrampolineBuffer[s_TrampolineSize], AfterBranchAddress, false, true);

   Jump(m_HookAddress, m_HookTarget, false, false);

   m_TrampolineOpd[0] = reinterpret_cast<uint32_t>(m_TrampolineAddress);
   m_TrampolineOpd[1] = tocOverride != 0 ? tocOverride : GetCurrentToc();
}

bool Detour::UnHook()
{
   if (m_HookAddress && m_OriginalLength)
   {
      WriteProcessMemory(sys_process_getpid(), m_HookAddress, m_OriginalInstructions, m_OriginalLength);

      m_OriginalLength = 0;
      m_HookAddress = nullptr;

      return true;
   }

   return false;
}

// --- ImportExportDetour ---

ImportExportDetour::ImportExportDetour(HookType type, const std::string& libaryName, uint32_t fnid, uintptr_t fnCallback)
   : Detour(), m_LibaryName(libaryName), m_Fnid(fnid)
{
   HookByFnid(type, libaryName, fnid, fnCallback);
}

ImportExportDetour::~ImportExportDetour()
{
   UnHook();
}

void ImportExportDetour::HookByFnid(HookType type, const std::string& libaryName, uint32_t fnid, uintptr_t fnCallback)
{
   opd_s* fnOpd = nullptr;

   switch (type)
   {
      case HookType::Import:
         fnOpd = FindImportByName(libaryName.c_str(), fnid);
         break;
      case HookType::Export:
         fnOpd = FindExportByName(libaryName.c_str(), fnid);
         break;
   }

   if (fnOpd == nullptr)
      return;

   Detour::Hook(fnOpd->func, fnCallback, fnOpd->toc);
}

// --- Stub table lookup ---

opd_s* FindExportByName(const char* module, uint32_t fnid)
{
   uint32_t* segment15 = *reinterpret_cast<uint32_t**>(0x1008C);
   uint32_t exportAdressTable = segment15[0x984 / sizeof(uint32_t)];
   exportStub_s* exportStub = reinterpret_cast<exportStub_s*>(exportAdressTable);

   while (exportStub->ssize == 0x1C00)
   {
      if (!strcmp(module, exportStub->name))
      {
         for (int16_t i = 0; i < exportStub->exports; i++)
         {
            if (exportStub->fnid[i] == fnid)
               return exportStub->stub[i];
         }
      }
      exportStub++;
   }

   return nullptr;
}

opd_s* FindImportByName(const char* module, uint32_t fnid)
{
   uint32_t* segment15 = *reinterpret_cast<uint32_t**>(0x1008C);
   uint32_t exportAdressTable = segment15[0x984 / sizeof(uint32_t)];
   importStub_s* importStub = reinterpret_cast<importStub_s*>(exportAdressTable);

   while (importStub->ssize == 0x2C00)
   {
      if (!strcmp(module, importStub->name))
      {
         for (int16_t i = 0; i < importStub->imports; i++)
         {
            if (importStub->fnid[i] == fnid)
               return importStub->stub[i];
         }
      }
      importStub++;
   }

   return nullptr;
}
