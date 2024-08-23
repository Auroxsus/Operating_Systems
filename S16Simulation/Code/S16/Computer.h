//----------------------------------------------------------------
// S16 Simulation (COMPUTER abstract data type)
// Computer.h
//----------------------------------------------------------------
#ifndef COMPUTER_H
#define COMPUTER_H

#include <stdio.h>
#include <stdbool.h>

#define NULLWORD        0

/*
   S16 is a 2-BYTE/WORD machine that uses 16-bit logical addresses. Each memory page is 
      2^9 = 512 bytes big; 2^16/2^9 = 128 pages of logical addresses per process.

      Each memory segment (code-segment, data-segment, and stack-segment) have extents
      that are integral number of logical 512-byte pages and each memory segment base is
      the logical address with offset 0 (the address of the first byte of the first page
      of the memory segment).
*/

#define MEMORY_PAGES                             2048
#define MEMORY_PAGE_SIZE_IN_BYTES                512
#define MEMORY_SIZE_IN_BYTES                     (MEMORY_PAGES*MEMORY_PAGE_SIZE_IN_BYTES)
#define SIZE_IN_PAGES(bytes)                     (((bytes)/MEMORY_PAGE_SIZE_IN_BYTES) + (((bytes)%MEMORY_PAGE_SIZE_IN_BYTES == 0) ? 0 : 1) )
#define PAGE_NUMBER(address)                     ((address)/MEMORY_PAGE_SIZE_IN_BYTES)
#define LOGICAL_ADDRESS_SPACE_IN_BYTES           (128*MEMORY_PAGE_SIZE_IN_BYTES)
#define IO_ADDRESS_SPACE_IN_BYTES                65536
#define BEGINNING_ADDRESS_OF_NEXT_PAGE(address)  ((PAGE_NUMBER(address)+1)*MEMORY_PAGE_SIZE_IN_BYTES)

typedef unsigned short int WORD;
typedef unsigned char      BYTE;

/*
   S16 is a big-endian, 2-BYTE/WORD machine, therefore, when a WORD's (H)igh (O)rder (B)YTE is
      in main memory location (address) then the WORD's (L)ow (O)rder (B)YTE is in main 
      memory location (address+1)
*/
#define HIBYTE(word)                     ((BYTE) ((word) >> 8))
#define LOBYTE(word)                     ((BYTE) ((word) % 256))
#define MAKEWORD(HOB,LOB)                ((WORD) (((HOB) << 8) + (LOB)))

#define HEADS_PER_DISK                   2
#define TRACKS_PER_HEAD                  128
#define SECTORS_PER_TRACK                32
#define SECTORS_PER_DISK                 (HEADS_PER_DISK*TRACKS_PER_HEAD*SECTORS_PER_TRACK)
#define BYTES_PER_SECTOR                 128

//================================================================
// S16 Computer and Central Processing Unit (CPU)
//================================================================
typedef enum 
{
   IDLE,                               // S16clock is ticking and CPU is not executing HW instructions
   RUN,                                // S16clock is ticking and CPU is     executing HW instructions
   HALT                                // S16clock is stopped and CPU is not executing HW instructions
} CPU_STATE;

typedef enum
{
   USER,
   KERNEL
} CPU_MODE;

typedef struct COMPUTER
{
// CPU state
   CPU_STATE CPUState;
// CPU mode
   CPU_MODE  CPUMode;
// CPU registers
   WORD PC;
   WORD SP;
   WORD FB;
   WORD R[16];                       // R[0] = R0, R[1] = R1,...,R[15] = R15
   WORD MMURegisters[LOGICAL_ADDRESS_SPACE_IN_BYTES/MEMORY_PAGE_SIZE_IN_BYTES];
// main memory
   BYTE mainMemory[MEMORY_SIZE_IN_BYTES];
// IO ports
   BYTE IOPorts[IO_ADDRESS_SPACE_IN_BYTES];
// disk
   FILE *DISK;
} COMPUTER;

void ConstructComputer();
void DestructComputer();
FILE *GetComputerDISK();
CPU_STATE GetCPUState();
void SetCPUState(const CPU_STATE CPUState);
WORD GetPC();
void SetPC(const WORD PC);
WORD GetSP();
void SetSP(const WORD SP);
WORD GetFB();
void SetFB(const WORD FB);
WORD GetRn(const int n);
void SetRn(const int n,const WORD Rn);

//================================================================
// Memory-Management Unit (MMU)
//================================================================
void WritePhysicalMainMemory(const int physicalAddress,const BYTE byte);
void ReadPhysicalMainMemory(const int physicalAddress,BYTE *byte);
void WriteDataLogicalMainMemory(const WORD MMURegisters[],const WORD logicalAddress,const BYTE byte);
void ReadDataLogicalMainMemory(const WORD MMURegisters[],const WORD logicalAddress,BYTE *byte);
void ReadCodeLogicalMainMemory(const WORD MMURegisters[],const WORD logicalAddress,BYTE *byte);

//================================================================
// Interrupt architecture
//================================================================
typedef enum
{
// internal interrupts
   MEMORY_ACCESS_INTERRUPT   = 0X01,
   PAGE_FAULT_INTERRUPT      = 0X02,
   OPERATION_CODE_INTERRUPT  = 0X03,
   DIVISION_BY_0_INTERRUPT   = 0X04,
   STACK_UNDERFLOW_INTERRUPT = 0X05,
   STACK_OVERFLOW_INTERRUPT  = 0X06,
// device interrupts
   TIMER_INTERRUPT           = 0X10,
   DISK_INTERRUPT            = 0X20
} HWINTERRUPT;

typedef enum
{
   PID_HWINTERRUPT_NUMBER    = 0X1000,
   TIMER_ENABLED             = 0X2000,
   TIMER_COUNT_HOB           = 0X2001,
   TIMER_COUNT_LOB           = 0X2002,
   DISK_COMMAND              = 0X3000,   // disk controller command byte
   DISK_BUFFER_HOB           = 0X3001,   //                 buffer main memory address
   DISK_BUFFER_LOB           = 0X3002,   //
   DISK_SECTOR_HOB           = 0X3003,   //                 sector address
   DISK_SECTOR_LOB           = 0X3004,   //
   DISK_COUNT_HOB            = 0X3005,   //                 sector byte count
   DISK_COUNT_LOB            = 0X3006    //
} IO_PORT;

void PollDevicesForHWInterrupt();
bool HWInterruptRaised();
void RaiseHWInterrupt(const HWINTERRUPT interruptNumber);
void ClearHWInterrupt();
// private void PollTimerForHWInterrupt();
// private void PollDiskControllerForHWInterrupt();

//================================================================
// Instruction Set Architecture (ISA)
//================================================================
// execute non-privileged HW instructions
void ExecuteHWInstruction(const WORD SSBase,WORD *memoryEA,WORD *memoryWORD);
// execute privileged HW instructions (used only by S16OS code)
void DoINR(WORD *Rn,const WORD IOPort);
void DoOUTR(const WORD Rn,const WORD IOPort);
void DoLDMMU(const WORD MMURegisters[]);
void DoSTMMU(WORD MMURegisters[]);
void DoSTOP();
// private void ReadO16Operand(WORD *O16);
// private void ReadRnOperand(BYTE *Rn);
// private void ReadModeOperand(BYTE *mode);
// private void PushWord(const WORD word);
// private void PopWord(const WORD SSBase,WORD *word);
// private void ReadDataWord(const WORD address,WORD *word);
// private void WriteDataWord(const WORD address,const WORD word);
// private bool IsNegative(const WORD word);
// private bool IsPositive(const WORD word);

#endif
