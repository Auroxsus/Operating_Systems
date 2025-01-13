//----------------------------------------------------------------
// S16 Simulation (COMPUTER abstract data type)
// Computer.c
//----------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>

#include ".\Computer.h"
#include ".\Random.h"

COMPUTER computer;

//----------------------------------------------------------------
void ConstructComputer()
//----------------------------------------------------------------
{
   SetCPUState(IDLE);

// initialize *ALL* main memory to random byte values
   for (int i = 0; i <= MEMORY_SIZE_IN_BYTES-1; i++)
      WritePhysicalMainMemory(i,(BYTE) RandomInt(0X00,0XFF));

// initialize IO ports to random byte values
   for (int i = 0; i <= IO_ADDRESS_SPACE_IN_BYTES-1; i++)
      computer.IOPorts[i] = (BYTE) RandomInt(0X00,0XFF);
/*
   open disk file; when file does not exist, write all sectors to "format" the S16 disk
                                                                                                   1         1         1       1
         1         2         3         4         5         6         7         8         9         0         1         2       2
12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678
Track 0XTT Sector 0XSS Head H
*/
   if ( (computer.DISK = fopen("Disk.","rb+")) == NULL )
   {
      computer.DISK = fopen("Disk.","wb");
      for (int track = 0; track <= TRACKS_PER_HEAD-1; track++)
         for (int sector = 0; sector <= SECTORS_PER_TRACK-1; sector++)
            for (int head = 0; head <= HEADS_PER_DISK-1; head++)
            {
               BYTE bytes[BYTES_PER_SECTOR];

               for (int byte = 0; byte <= BYTES_PER_SECTOR-1; byte++)
                  bytes[byte] = 0X00;
               sprintf((char *) bytes,"Track 0X%02X Sector 0X%02X Head %1d",track,sector,head);
               fwrite(bytes,BYTES_PER_SECTOR,1,computer.DISK);
            }
      fclose(computer.DISK);
      computer.DISK = fopen("Disk.","rb+");
   }
}

//----------------------------------------------------------------
void DestructComputer()
//----------------------------------------------------------------
{
// close disk file
   fclose(computer.DISK);
}

//----------------------------------------------------------------
FILE *GetComputerDISK()
//----------------------------------------------------------------
{
   return( computer.DISK );
}

//----------------------------------------------------------------
CPU_STATE GetCPUState()
//----------------------------------------------------------------
{
   return( computer.CPUState );
}

//----------------------------------------------------------------
void SetCPUState(const CPU_STATE CPUState)
//----------------------------------------------------------------
{
   computer.CPUState = CPUState;
}

//----------------------------------------------------------------
WORD GetPC()
//----------------------------------------------------------------
{
   return( computer.PC );
}

//----------------------------------------------------------------
void SetPC(WORD PC)
//----------------------------------------------------------------
{
   computer.PC = PC;
}

//----------------------------------------------------------------
WORD GetSP()
//----------------------------------------------------------------
{
   return( computer.SP );
}

//----------------------------------------------------------------
void SetSP(const WORD SP)
//----------------------------------------------------------------
{
   computer.SP = SP;
}

//----------------------------------------------------------------
WORD GetFB()
//----------------------------------------------------------------
{
   return( computer.FB );
}

//----------------------------------------------------------------
void SetFB(const WORD FB)
//----------------------------------------------------------------
{
   computer.FB = FB;
}

//----------------------------------------------------------------
WORD GetRn(const int n)
//----------------------------------------------------------------
{
   return( computer.R[n] );
}

//----------------------------------------------------------------
void SetRn(const int n,const WORD Rn)
//----------------------------------------------------------------
{
   computer.R[n] = Rn;
}

//----------------------------------------------------------------
void WritePhysicalMainMemory(const int physicalAddress,const BYTE byte)
//----------------------------------------------------------------
{
   computer.mainMemory[physicalAddress] = byte;
}

//----------------------------------------------------------------
void ReadPhysicalMainMemory(const int physicalAddress,BYTE *byte)
//----------------------------------------------------------------
{
   *byte = computer.mainMemory[physicalAddress];
}

//----------------------------------------------------------------
void WriteDataLogicalMainMemory(const WORD MMURegisters[],const WORD logicalAddress,const BYTE byte)
//----------------------------------------------------------------
{
/*
   16-bit logicalAddress
   =====================
      111111
      5432109876543210
      LLLLLLLOOOOOOOOO

      LLLLLLL = logical page number in [ 0,127 ]
      OOOOOOOOO = page offset in [ 0,511 ]

   16-bit MMURegister
   ==================
      111111
      54321098 76543210
      MVEWRPPP PPPPPPPP

      M = page in memory (to support virtual memory)
      V = page is valid
      E = execute permission (code-segment)
      W = write permission (data-segment, stack-segment)
      R = read permission (data-segment, stack-segment)
      PPPPPPPPPPP = physical page number in [ 0,2047 ]
*/
   int M,V,E,W,R,LLLLLLL,OOOOOOOOO,PPPPPPPPPPP;

   LLLLLLL   = logicalAddress >> 9;
   OOOOOOOOO = logicalAddress & 0X01FF;
   M = (MMURegisters[LLLLLLL] & 0X8000) >> 15;
   V = (MMURegisters[LLLLLLL] & 0X4000) >> 14;
   E = (MMURegisters[LLLLLLL] & 0X2000) >> 13;
   W = (MMURegisters[LLLLLLL] & 0X1000) >> 12;
   R = (MMURegisters[LLLLLLL] & 0X0800) >> 11;
   PPPPPPPPPPP = MMURegisters[LLLLLLL] & 0X07FF;

   if ( V != 1 )
      RaiseHWInterrupt(MEMORY_ACCESS_INTERRUPT);

   if ( M != 1 )
      RaiseHWInterrupt(PAGE_FAULT_INTERRUPT);

   if ( W == 1 )
   {
      int physicalAddress = (PPPPPPPPPPP << 9) + OOOOOOOOO; 
      
      WritePhysicalMainMemory(physicalAddress,byte);
   }
   else
      RaiseHWInterrupt(MEMORY_ACCESS_INTERRUPT);
}

//----------------------------------------------------------------
void ReadDataLogicalMainMemory(const WORD MMURegisters[],const WORD logicalAddress,BYTE *byte)
//----------------------------------------------------------------
{
   int M,V,E,W,R,LLLLLLL,OOOOOOOOO,PPPPPPPPPPP;

   LLLLLLL   = logicalAddress >> 9;
   OOOOOOOOO = logicalAddress & 0X01FF;
   M = (MMURegisters[LLLLLLL] & 0X8000) >> 15;
   V = (MMURegisters[LLLLLLL] & 0X4000) >> 14;
   E = (MMURegisters[LLLLLLL] & 0X2000) >> 13;
   W = (MMURegisters[LLLLLLL] & 0X1000) >> 12;
   R = (MMURegisters[LLLLLLL] & 0X0800) >> 11;
   PPPPPPPPPPP = MMURegisters[LLLLLLL] & 0X07FF;

   if ( V != 1 )
      RaiseHWInterrupt(MEMORY_ACCESS_INTERRUPT);

   if ( M != 1 )
      RaiseHWInterrupt(PAGE_FAULT_INTERRUPT);

   if ( R == 1 )
   {
      int physicalAddress = (PPPPPPPPPPP << 9) + OOOOOOOOO; 
      
      ReadPhysicalMainMemory(physicalAddress,byte);
   }
   else
      RaiseHWInterrupt(MEMORY_ACCESS_INTERRUPT);
}

//----------------------------------------------------------------
void ReadCodeLogicalMainMemory(const WORD MMURegisters[],const WORD logicalAddress,BYTE *byte)
//----------------------------------------------------------------
{
   int M,V,E,W,R,LLLLLLL,OOOOOOOOO,PPPPPPPPPPP;

   LLLLLLL   = logicalAddress >> 9;
   OOOOOOOOO = logicalAddress & 0X01FF;

   M = (MMURegisters[LLLLLLL] & 0X8000) >> 15;
   V = (MMURegisters[LLLLLLL] & 0X4000) >> 14;
   E = (MMURegisters[LLLLLLL] & 0X2000) >> 13;
   W = (MMURegisters[LLLLLLL] & 0X1000) >> 12;
   R = (MMURegisters[LLLLLLL] & 0X0800) >> 11;
   PPPPPPPPPPP = MMURegisters[LLLLLLL] & 0X07FF;

   if ( V != 1 )
      RaiseHWInterrupt(MEMORY_ACCESS_INTERRUPT);

   if ( M != 1 )
      RaiseHWInterrupt(PAGE_FAULT_INTERRUPT);

   if ( E == 1 )
   {
      int physicalAddress = (PPPPPPPPPPP << 9) + OOOOOOOOO; 

      ReadPhysicalMainMemory(physicalAddress,byte);
   }
   else
      RaiseHWInterrupt(MEMORY_ACCESS_INTERRUPT);
}

/*
========================================
Priority Interrupt Device (PID) IO port
========================================

IOPort  Description
------  ----------------------------------------
0X1000  HW interrupt number
        0X01 memory access interrupt
        0X02 page fault interrupt
        0X03 operation code interrupt
        0X04 division-by-0 interrupt
        0X05 stack underflow interrupt
        0X06 stack overflow interrupt
        0X10 timer
        0X20 disk
*/
//----------------------------------------------------------------
void PollDevicesForHWInterrupt()
//----------------------------------------------------------------
{
   bool HWInterruptRaised();
   void PollTimerForHWInterrupt();
   void PollDiskControllerForHWInterrupt();
/*
   External hardware devices are polled in priority-order (the lower 
      the HW interrupt number, the higher the priority) until a hardware 
      device registers an interrupt request (if any). The current external 
      hardware devices are (1) the Timer and (2) the Disk drive.

   It is possible that an internal interrupt may have been raised *before* 
      the external hardware devices are polled; namely, the internal devices 
      MMU (memory access interrupt, page fault interrupt) and the CPU (operation 
      code interrupt, division-by-0 interrupt, and stack underflow/overflow interrupts).

   External hardware devices are *not* polled when a higher-priority interrupt has 
      been raised, thus ensuring that the lower-priority interrupts which
      coincide-in-time with a higher-priority interrupt are not lost but
      are delayed until the next poll during the same S16clock tick.
*/
   if ( !HWInterruptRaised() ) PollTimerForHWInterrupt();
   if ( !HWInterruptRaised() ) PollDiskControllerForHWInterrupt();
}

/*
===============
Timer IO ports
===============

IOPort  Description
------  ----------------------------------------
0X2000  timer enabled
           0X00 = false
           0XFF = true
0X2001  HOB of count
0X2002  LOB of count
*/

//----------------------------------------------------------------
/* private */ void PollTimerForHWInterrupt()
//----------------------------------------------------------------
{
   void RaiseHWInterrupt(const HWINTERRUPT interruptNumber);
/*
   decrement timer count each S16clock tick while timer is enabled;
      raise TIMER_INTERRUPT when count becomes 0X0000
*/
   if ( computer.IOPorts[TIMER_ENABLED] == 0XFF )
   {
      BYTE HOB = computer.IOPorts[TIMER_COUNT_HOB];
      BYTE LOB = computer.IOPorts[TIMER_COUNT_LOB];
      int count = MAKEWORD(HOB,LOB);

      if ( count == 0X0000 )
         RaiseHWInterrupt(TIMER_INTERRUPT);
      else
      {
         computer.IOPorts[TIMER_COUNT_HOB] = HIBYTE(count-1);
         computer.IOPorts[TIMER_COUNT_LOB] = LOBYTE(count-1);
      }
   }
}

/*
===========================
Disk drive device IO ports
===========================

IOPort  description
------  ----------------------------------------
0X3000  disk controller command
           0X00 = disabled
           0X0F = readSector
           0XF0 = writeSector
0X3001  HOB of logical address of sector buffer in process
0X3002  LOB of logical address of sector buffer in process
0X3003  HOB of sector address
0X3004  LOB of sector address
0X3005  HOB of sector byte count
0X3006  LOB of sector byte count
*/

//----------------------------------------------------------------
/* private */ void PollDiskControllerForHWInterrupt()
//----------------------------------------------------------------
{
   void RaiseHWInterrupt(const HWINTERRUPT interruptNumber);
   void Memory(const WORD physicalAddress,BYTE *byte);
   void WriteMainMemory(const WORD physicalAddress,const BYTE byte);
/*
   Decrement sector byte count each S16clock tick while ReadSector or
      WriteSector command is executing; when sector byte count counts down
      to 0X0000 and the transfer is complete, raise DISK_INTERRUPT.

   *Note* Actual IO (that is, transfer of sector to/from the disk device) occurs
      in disk device interrupt handler in S16OS_GoToHWInterruptEntryPoint(),
      not in the disk hardware simulation. Does not truly represent realistic 
      direct memory access (DMA) transfer of a sector directly between the 
      disk device controller (simulated by this function) and the
      and the process-resident, sector-sized buffer.
*/
   switch ( computer.IOPorts[DISK_COMMAND] )
   {
      case 0X00: // disabled
            break;
      case 0X0F: // readSector
      {
         BYTE HOB = computer.IOPorts[DISK_COUNT_HOB];
         BYTE LOB = computer.IOPorts[DISK_COUNT_LOB];
         int count = MAKEWORD(HOB,LOB);

         if ( count == 0X0000 )
            RaiseHWInterrupt(DISK_INTERRUPT);
         else
         {
            computer.IOPorts[DISK_COUNT_HOB] = HIBYTE(count-1);
            computer.IOPorts[DISK_COUNT_LOB] = LOBYTE(count-1);
         }
         break;
      }
      case 0XF0: // writeSector
      {
         BYTE HOB = computer.IOPorts[DISK_COUNT_HOB];
         BYTE LOB = computer.IOPorts[DISK_COUNT_LOB];
         int count = MAKEWORD(HOB,LOB);

         if ( count == 0X0000 )
            RaiseHWInterrupt(DISK_INTERRUPT);
         else
         {
            computer.IOPorts[DISK_COUNT_HOB] = HIBYTE(count-1);
            computer.IOPorts[DISK_COUNT_LOB] = LOBYTE(count-1);
         }
         break;
      }
   }
}

//----------------------------------------------------------------
bool HWInterruptRaised()
//----------------------------------------------------------------
{
   return( computer.IOPorts[PID_HWINTERRUPT_NUMBER] != 0X00 );
}

//----------------------------------------------------------------
void RaiseHWInterrupt(const HWINTERRUPT interruptNumber)
//----------------------------------------------------------------
{
   computer.IOPorts[PID_HWINTERRUPT_NUMBER] = interruptNumber;
}

//----------------------------------------------------------------
void ClearHWInterrupt()
//----------------------------------------------------------------
{
   computer.IOPorts[PID_HWINTERRUPT_NUMBER] = 0X00;
}

/*----------------------------------------------------------------
Non-privileged Hardware Instructions (Notes 7,9)
OpCode Mnemonic and operands  HW instruction format   HW instruction semantics (how the instruction effects operands and/or S16 state)
------ ---------------------- ----------------------- ----------------------------------------------------------------------------------
0X00   NOOP                   OpCode                  no operation
0X01   JMP    O16             OpCode:O16              jump unconditionally to address O16 (Note 1)
0X02   JMPN   Rn,O16          OpCode:Rn:O16           jump conditionally to address O16 when Rn  < 0 (Note 7)
0X03   JMPNN  Rn,O16          OpCode:Rn:O16           jump conditionally to address O16 when Rn >= 0
0X04   JMPZ   Rn,O16          OpCode:Rn:O16           jump conditionally to address O16 when Rn  = 0 (alias JMPF) (Note 8)
0X05   JMPNZ  Rn,O16          OpCode:Rn:O16           jump conditionally to address O16 when Rn != 0 (alias JMPT) (Note 8)
0X06   JMPP   Rn,O16          OpCode:Rn:O16           jump conditionally to address O16 when Rn  > 0
0X07   JMPNP  Rn,O16          OpCode:Rn:O16           jump conditionally to address O16 when Rn <= 0

0X08   CALL   O16             OpCode:O16              push PC onto top-of-stack and jump to address O16 (Note 15)
0X09   RET                    OpCode                  pop top-of-stack return address into PC (Note 15)
0X0A   SVC    #O16            OpCode:O16              do software interrupt for SVC O16 (Note 2)
0X0B   DEBUG                  OpCode                  invoke S16 debug utility (Note 3)

0X20   ADDR   Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn+Rn2 (signed arithmetic)
0X21   SUBR   Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn-Rn2 (signed arithmetic)
0X22   INCR   Rn              OpCode:Rn               Rn <- Rn+1 (signed arithmetic)
0X23   DECR   Rn              OpCode:Rn               Rn <- Rn-1 (signed arithmetic)
0X24   ZEROR  Rn              OpCode:Rn               Rn <- 0
0X25   LSRR   Rn              OpCode:Rn               Rn <- logical shift-right Rn 1 bit  (unsigned arithmetic)
0X26   ASRR   Rn              OpCode:Rn               Rn <- arithmetic shift-right Rn 1 bit (signed arithmetic)
0X27   SLR    Rn              OpCode:Rn               Rn <- shift-left Rn 1 bit (unsigned arithmetic)
0X28   CMPR   Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn compareSigned   Rn2, -1 when Rn < Rn2, 0 when Rn = Rn2, 1 when Rn > Rn2
0X29   CMPUR  Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn compareUnsigned Rn2, -1 when Rn < Rn2, 0 when Rn = Rn2, 1 when Rn > Rn2
0X2A   ANDR   Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn bitwise-and Rn2 (unsigned arithmetic)
0X2B   ORR    Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn bitwise-or Rn2 (unsigned arithmetic)
0X2C   XORR   Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn bitwise-xor Rn2 (unsigned arithmetic)
0X2D   NOTR   Rn              OpCode:Rn               Rn <- one's complement of Rn (unsigned arithmetic) (Note 4)
0X2E   NEGR   Rn              OpCode:Rn               Rn <- two's complement of Rn (signed arithmetic) (Note 5)
0X2F   MULR   Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn*Rn2 (signed arithmetic)
0X30   DIVR   Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn/Rn2 (signed arithmetic) (Note 14)
0X31   MODR   Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn mod Rn2 (signed arithmetic) (Note 14)

0X40   LDR    Rn,operands     OpCode:mode:Rn:???      Rn  <- memory[EA]; that is, datum from memory[EA] loaded into Rn (Note 13)
              Rn,#O16         OpCode:0X10:Rn:O16      EA <- PC+3 (register,immediate) (Note 12)
              Rn,O16          OpCode:0X01:Rn:O16      EA <- O16 (register,direct)
              Rn,@Rn2         OpCode:0X02:Rn:Rn2      EA <- Rn2 (register,indirect)
              Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx  EA <- O16+Rnx (register,indexed)
              Rn,FB:O16       OpCode:0X21:Rn:O16      EA <- FB+2*O16 (register,FB-relative/direct)  (Notes 10,11)
              Rn,@FB:O16      OpCode:0X22:Rn:O16      EA <- memory[FB+2*O16] (register,FB-relative/indirect)  Notes 10,11)
              Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx  EA <- (FB+2*O16)+Rnx (register,FB-relative/direct/indexed) (Notes 10,11)
              Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx  EA <- memory[FB+2*O16]+Rnx (register,FB-relative/indirect/indexed) (Notes 10,11)

0X41   LDAR   Rn,operands     OpCode:mode:Rn:???      Rn  <- EA; that is, EA loaded into Rn (Note 13)
              Rn,#O16                                 *** NOT MEANINGFUL ***
              Rn,O16          OpCode:0X01:Rn:O16      EA <- O16 (register,direct)
              Rn,@Rn2         OpCode:0X02:Rn:Rn2      EA <- Rn2 (register,indirect)
              Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx  EA <- O16+Rnx (register,indexed)
              Rn,FB:O16       OpCode:0X21:Rn:O16      EA <- FB+2*O16 (register,FB-relative/direct)  (Notes 10,11)
              Rn,@FB:O16      OpCode:0X22:Rn:O16      EA <- memory[FB+2*O16] (register,FB-relative/indirect)  Notes 10,11)
              Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx  EA <- (FB+2*O16)+Rnx (register,FB-relative/direct/indexed) (Notes 10, 11)
              Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx  EA <- memory[FB+2*O16]+Rnx (register,FB-relative/indirect/indexed) (Notes 10,11)

0X42   STR    Rn,operands     OpCode:mode:Rn:???      memory[EA] <- Rn; that is, datum in Rn stored into memory[EA] (Note 13)
              Rn,#O16                                 *** NOT MEANINGFUL ***
              Rn,O16          OpCode:0X01:Rn:O16      EA <- O16 (register,direct)
              Rn,@Rn2         OpCode:0X02:Rn:Rn2      EA <- Rn2 (register,indirect)
              Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx  EA <- O16+Rnx (register,indexed)
              Rn,FB:O16       OpCode:0X21:Rn:O16      EA <- FB+2*O16 (register,FB-relative/direct) (Notes 10,11)
              Rn,@FB:O16      OpCode:0X22:Rn:O16      EA <- memory[FB+2*O16] (register,FB-relative/indirect) (Notes 10,11)
              Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx  EA <- (FB+2*O16)+Rnx (register,FB-relative/direct/indexed) (Notes 10,11)
              Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx  EA <- memory[FB+2*O16]+Rnx (register,FB-relative/indirect/indexed) (Notes 10,11)

0X43   COPYR  Rn,Rn2          OpCode:Rn:Rn2           Rn <- Rn2
0X44   PUSHR  Rn              OpCode:Rn               push Rn onto top-of-stack (Note 15)
0X45   POPR   Rn              OpCode:Rn               pop top-of-stack into Rn (Note 15)
0X46   SWAPR  Rn,Rn2          OpCode:Rn:Rn2           Rn <-> Rn2; that is, swap contents of Rn and Rn2

0X50   PUSHFB                 OpCode                  push FB onto top-of-stack (Note 15)
0X51   POPFB                  OpCode                  pop top-of-stack into FB (Note 15)
0X52   SETFB  #O16            OpCode:O16              FB <- SP+O16 (O16 signed)
0X53   ADJSP  #O16            OpCode:O16              SP <- SP+O16 (O16 signed)

Privileged Hardware Instructions (Note 6)
OpCode Mnemonic and operands  HW instruction format   HW instruction semantics (how the instruction effects operands and/or S16 state)
------ ---------------------- ----------------------- ----------------------------------------------------------------------------------
       INR   Rn,O16           OpCode:Rn:O16           input  byte from IO port into low order byte of Rn
       OUTR  Rn,O16           OpCode:Rn:O16           output byte to   IO port from low order byte of Rn
       LDMMU O16              OpCode:O16              load MMU registers from memory[O16]
       STMMU O16              OpCode:O16              store MMU registers to memory[O16]
       STOP                   OpCode                  stop execution

Note  1 The execution of every JMP or CALL HW instruction changes the contents of the PC register to the address operand O16.
        JMP and CALL O16 operands must be represented symbolically in the S16 assembly language using a <label>.

Note  2 "Do software interrupt" means flow-of-control is directed to the S16OS SVC entry point, S16OS_GoToSVCEntryPoint().
        On entry to the SVC entry point, S16OS saves the current S16 state in the SVC-ing process program control 
        block (PCB); because of this state-saving, the S16 hardware is *NOT* required to save the PC register on the run-time 
        stack (somthing that happens during the execution of a CALL HW instruction). S16OS "vectors" to the 
        appropriate service request handler as determined by the SVC HW instruction O16 operand.

Note  3 The DEBUG utility allows the user to interactively display S16 and S16OS state information (including 
        but not limited to) disk sectors, current process main memory contents, and current process CPU register values.

Note  4 Compute the "one's complement of Rn", that is, change every 0 bit of Rn to 1 and change every 
        1 bit of Rn to 0. Equivalently, compute 0XFFFF-Rn. Long live Appendix A!

Note  5 Compute the "two's complement of Rn", that is, increment the one's complement of Rn. 
        Equivalently, compute 0XFFFF-Rn+1. Long live Appendix A!

Note  6 A privileged HW instruction is *NOT* translated into machine language by the S16 assembler and therefore 
        is not assigned an OpCode.

Note  7 A Rn, Rn2, and Rnx operand specifies one of the 16 general-purpose registers R0, R1, R2,...,R15. A BYTE-sized, 
        unsigned integer n is used to encode Rn, Rn2, and Rnx operands in the HW instruction format. For example, 
        R10 is encoded 00001010 (base 2) or 0X0A (base 16).

        An O16 operand <label>, <integer>, <boolean>, and <character> is encoded as a 16-bit integer (a WORD) that is
        interpreted at run-time as either unsigned or signed (two's complement) integer depending on context. *ALL* 
        conditional JMP HW instructions treat Rn as a signed integer and O16 as a logical address in the code-segment.

        A mode operand is encoded as a BYTE.

Note  8 Because the integer value 0 is usually interpreted as the boolean value FALSE and any non-0 integer value 
        interpreted as the boolean value TRUE, the JMPZ mnemonic is aliased JMPF and the JMPNZ mnemonic is aliased JMPT.

Note  9 HW instructions are formatted as 1-BYTE, 2-BYTE, 3-BYTE, 4-BYTE, 5-BYTE, or 6-BYTE depending on the machine 
        HW instruction.

Note 10 When an activation record is correctly-formed, the frame-base register FB *MUST* point-to the 0-offset 
        element.

Note 11 O16 is the word-offset of the frame-resident datum for *ALL* FB-relative addressing modes. O16 for FB-relative
        HW instructions must be represented symbolically as an <integer>. Since S16 is a BYTE-addressable machine, O16 
        must be multiplied by 2 to compute the frame-resident datum logical address.

Note 12 The immediate mode LDR HW instruction operand O16 is the code-segment WORD stored at memory[PC+3]

Note 13 LDR, STR, and LDAR 1-BYTE addressing mode 0X??

        ?#1 0 = memory; 1 = PC-relative; 2 = FB-relative
        ?#2 0 = immediate; 1 = direct; 2 = indirect; 4 = indexed; 5 = 1+4, direct/indexed; 6 = 2+4, indirect/indexed

Note 14 DIVR and MODR raise DIVISION_BY_0_INTERRUPT when Rn2 = 0X0000.

Note 15 An attempted push onto top-of-full-stack raises STACK_OVERFLOW_INTERRUPT; an attempted pop top-of-empty-stack 
        raises STACK_UNDERFLOW_INTERRUPT.
----------------------------------------------------------------*/

//----------------------------------------------------------------
void ExecuteHWInstruction(const WORD SSBase,WORD *memoryEA,WORD *memoryWORD)
//----------------------------------------------------------------
{
/*
   *Notes*
      SSBase passed for use by PoPWord() to test for stack underflow
      LDR and STR are the *ONLY* HW instructions required to set both *memoryEA and *memoryWORD
      LDAR is the *ONLY* HW instruction required to set *memoryEA and *memoryWORD := NULL
*/
   void S16OS_GoToSVCEntryPoint(const WORD SVCRequestNumber);
   void S16OS_Debugger();
   void RaiseHWInterrupt(const HWINTERRUPT interruptNumber);
   void ReadO16Operand(WORD *O16);
   void ReadModeOperand(BYTE *mode);
   void ReadRnOperand(BYTE *Rn);
   void PushWord(const WORD word);
   void PopWord(const WORD SSBase,WORD *word);
   void ReadDataWord(const WORD address,WORD *word);
   void WriteDataWord(const WORD address,const WORD word);
   bool IsNegative(const WORD word);
   bool IsPositive(const WORD word);

   BYTE opCode;

   ReadCodeLogicalMainMemory(computer.MMURegisters,computer.PC,&opCode);
   computer.PC++;
   switch ( opCode )
   {
   /* ============= */
   /* CHOOSE        */
   /* ============= */
      case 0X00: // NOOP                   OpCode                   no operation
      {
         break;
      }
      case 0X01: // JMP    O16             OpCode:O16               jump unconditionally to address O16
      {
         WORD O16;

         ReadO16Operand(&O16);
         computer.PC = O16;
         break;
      }
      case 0X02: // JMPN   Rn,O16          OpCode:Rn:O16            jump conditionally to address O16 when Rn  < 0
      {
         BYTE Rn;
         WORD O16;
         
         ReadRnOperand(&Rn);
         ReadO16Operand(&O16);
         if ( IsNegative(computer.R[Rn]) ) computer.PC = O16;
         break;
      }
      case 0X03: // JMPNN  Rn,O16          OpCode:Rn:O16            jump conditionally to address O16 when Rn >= 0
      {
         BYTE Rn;
         WORD O16;
         
         ReadRnOperand(&Rn);
         ReadO16Operand(&O16);
         if ( !IsNegative(computer.R[Rn]) ) computer.PC = O16;
         break;
      }
      case 0X04: // JMPZ   Rn,O16          OpCode:Rn:O16            jump conditionally to address O16 when Rn  = 0 (alias JMPF because FALSE  = 0X0000)
      {
         BYTE Rn;
         WORD O16;
         
         ReadRnOperand(&Rn);
         ReadO16Operand(&O16);
         if ( computer.R[Rn] == 0 ) computer.PC = O16;
         break;
      }
      case 0X05: // JMPNZ  Rn,O16          OpCode:Rn:O16            jump conditionally to address O16 when Rn != 0 (alias JMPT because  TRUE != 0X0000)
      {
         BYTE Rn;
         WORD O16;

         ReadRnOperand(&Rn);
         ReadO16Operand(&O16);
         if ( computer.R[Rn] != 0 ) computer.PC = O16;
         break;
      }
      case 0X06: // JMPP   Rn,O16          OpCode:Rn:O16            jump conditionally to address O16 when Rn  > 0
      {
         BYTE Rn;
         WORD O16;

         ReadRnOperand(&Rn);
         ReadO16Operand(&O16);
         if ( IsPositive(computer.R[Rn]) ) computer.PC = O16;
         break;
      }
      case 0X07: // JMPNP  Rn,O16          OpCode:Rn:O16            jump conditionally to address O16 when Rn <= 0
      {
         BYTE Rn;
         WORD O16;

         ReadRnOperand(&Rn);
         ReadO16Operand(&O16);
         if ( !IsPositive(computer.R[Rn]) ) computer.PC = O16;
         break;
      }
      case 0X08: // CALL   O16             OpCode:O16               push PC onto top-of-stack and jump to address O16
      {
         WORD O16;
         
         ReadO16Operand(&O16);
         PushWord(computer.PC);
         computer.PC = O16;
         break;
      }
      case 0X09: // RET                    OpCode                   pop top-of-stack return address into PC
      {
         WORD O16;
         
         PopWord(SSBase,&O16);
         computer.PC = O16;
         break;
      }
      case 0X0A: // SVC    #O16            OpCode:O16               do software interrupt for SVC O16
      {
         WORD O16;
         
         ReadO16Operand(&O16);
         S16OS_GoToSVCEntryPoint(O16);
         break;
      }
      case 0X0B: // DEBUG                  OpCode                   invoke S16 debug utility
      {
         S16OS_Debugger();
         break;
      }
   /* ============= */
   /* COMPUTE       */
   /* ============= */
      case 0X20: // ADDR   Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn+Rn2
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         computer.R[Rn] = (WORD) ((signed short int) computer.R[Rn] + (signed short int) computer.R[Rn2]);
         break;
      }
      case 0X21: // SUBR   Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn-Rn2
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         computer.R[Rn] = (WORD) ((signed short int) computer.R[Rn] - (signed short int) computer.R[Rn2]);
         break;
      }
      case 0X22: // INCR   Rn              OpCode:Rn                Rn <- Rn+1
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         computer.R[Rn] = (WORD) ((signed short int) computer.R[Rn] + 1);
         break;
      }
      case 0X23: // DECR   Rn              OpCode:Rn                Rn <- Rn-1
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         computer.R[Rn] = (WORD) ((signed short int) computer.R[Rn] - 1);
         break;
      }
      case 0X24: // ZEROR  Rn              OpCode:Rn                Rn <- 0
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         computer.R[Rn] = 0X0000;
         break;
      }
      case 0X25: // LSRR   Rn              OpCode:Rn                Rn <- logical shift-right Rn 1 bit
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         computer.R[Rn] >>= 1;
         break;
      }
      case 0X26: // ASRR   Rn              OpCode:Rn                Rn <- arithmetic shift-right Rn 1 bit
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         computer.R[Rn] = (WORD) (((signed short int) computer.R[Rn]) >> 1);
         break;
      }
      case 0X27: // SLR    Rn              OpCode:Rn                Rn <- shift-left Rn 1 bit
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         computer.R[Rn] <<= 1;
         break;
      }
      case 0X28: // CMPR   Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn compareSigned   Rn2, -1 when Rn < Rn2, 0 when Rn = Rn2, 1 when Rn > Rn2
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         if      ( ((signed short int) computer.R[Rn])  < ((signed short int) computer.R[Rn2]) )
            computer.R[Rn] = (WORD) -1;
         else if ( ((signed short int) computer.R[Rn]) == ((signed short int) computer.R[Rn2]) )
            computer.R[Rn] = (WORD) 0;
         else//if ( ((signed short int) computer.R[Rn]) > ((signed short int) computer.R[Rn2]) )
            computer.R[Rn] = (WORD) 1;
         break;
      }
      case 0X29: // CMPUR  Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn compareUnsigned Rn2, -1 when Rn < Rn2, 0 when Rn = Rn2, 1 when Rn > Rn2
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         if      ( computer.R[Rn]  < computer.R[Rn2] )
            computer.R[Rn] = (WORD) -1;
         else if ( computer.R[Rn] == computer.R[Rn2] )
            computer.R[Rn] = (WORD) 0;
         else//if ( computer.R[Rn] > computer.R[Rn2] )
            computer.R[Rn] = (WORD) 1;
         break;
      }
      case 0X2A: // ANDR   Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn bitwise-and Rn2
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         computer.R[Rn] = computer.R[Rn] & computer.R[Rn2];
         break;
      }
      case 0X2B: // ORR    Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn bitwise-or Rn2
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         computer.R[Rn] = computer.R[Rn] | computer.R[Rn2];
         break;
      }
      case 0X2C: // XORR   Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn bitwise-xor Rn2
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         computer.R[Rn] = computer.R[Rn] ^ computer.R[Rn2];
         break;
      }
      case 0X2D: // NOTR   Rn              OpCode:Rn                Rn <- one's complement of Rn
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         computer.R[Rn] = ~computer.R[Rn];
         break;
      }
      case 0X2E: // NEGR   Rn              OpCode:Rn                Rn <- two's complement of Rn
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         computer.R[Rn] = ~computer.R[Rn]+1;
         break;
      }
      case 0X2F: // MULR   Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn*Rn2 (two's complement arithmetic)
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         computer.R[Rn] = (WORD) ((signed short int) computer.R[Rn] * (signed short int) computer.R[Rn2]);
         break;
      }
      case 0X30: // DIVR   Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn/Rn2 (two's complement arithmetic) 
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         if ( computer.R[Rn2] == 0X0000 )
            RaiseHWInterrupt(DIVISION_BY_0_INTERRUPT);
         else
            computer.R[Rn] = (WORD) ((signed short int) computer.R[Rn] / (signed short int) computer.R[Rn2]);
         break;
      }
      case 0X31: // MODR   Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn mod Rn2 (two's complement arithmetic)
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         if ( computer.R[Rn2] == 0X0000 )
            RaiseHWInterrupt(DIVISION_BY_0_INTERRUPT);
         else
            computer.R[Rn] = (WORD) ((signed short int) computer.R[Rn] % (signed short int) computer.R[Rn2]);
         break;
      }
   /* ============= */
   /* COPY          */
   /* ============= */
//----------------------------------------------------------------------------------
// LDR and STR are the *ONLY* HW instructions required to set both *memoryEA and *memoryWORD
//----------------------------------------------------------------------------------
      case 0X40: // LDR    Rn,operands
      {
         BYTE mode,Rn,Rn2,Rnx;
         WORD O16;
         
         ReadModeOperand(&mode);
         ReadRnOperand(&Rn);
         switch ( mode )
         {
            case 0X01: // Rn,O16          OpCode:0X01:Rn:O16       EA <- O16 (register,direct)
               ReadO16Operand(&O16);
               *memoryEA = O16;
               ReadDataWord(*memoryEA,memoryWORD);
               break;
            case 0X02: // Rn,@Rn2         OpCode:0X02:Rn:Rn2       EA <- Rn2 (register,indirect)
               ReadRnOperand(&Rn2);
               *memoryEA = computer.R[Rn2];
               ReadDataWord(*memoryEA,memoryWORD);
               break;
            case 0X04: // Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   EA <- O16+Rnx (register,indexed)
               ReadO16Operand(&O16);
               ReadRnOperand(&Rnx);
               *memoryEA = O16+computer.R[Rnx];
               ReadDataWord(*memoryEA,memoryWORD);
               break;
            case 0X10: // Rn,#O16         OpCode:0X10:Rn:O16       EA <- PC+3 (register,immediate)
               *memoryEA = computer.PC;
               ReadO16Operand(&O16);
               *memoryWORD = O16;
               break;
            case 0X21: // Rn,FB:O16       OpCode:0X21:Rn:O16       EA <- FB+2*O16 (register,FB-relative/direct)
               ReadO16Operand(&O16);
               *memoryEA = computer.FB+2*O16;
               ReadDataWord(*memoryEA,memoryWORD);
               break;
            case 0X22: // Rn,@FB:O16      OpCode:0X22:Rn:O16       EA <- memory[FB+2*O16] (register,FB-relative/indirect)
               ReadO16Operand(&O16);
               ReadDataWord(computer.FB+2*O16,memoryEA);
               ReadDataWord(*memoryEA,memoryWORD);
               break;
            case 0X25: // Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   EA <- (FB+2*O16)+Rnx (register,FB-relative/direct/indexed)
               ReadO16Operand(&O16);
               ReadRnOperand(&Rnx);
               *memoryEA = computer.FB+2*O16+computer.R[Rnx];
               ReadDataWord(*memoryEA,memoryWORD);
               break;
            case 0X26: // Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   EA <- memory[FB+2*O16]+Rnx (register,FB-relative/indirect/indexed)
               ReadO16Operand(&O16);
               ReadRnOperand(&Rnx);
               ReadDataWord(computer.FB+2*O16,memoryEA);
               *memoryEA += computer.R[Rnx];
               ReadDataWord(*memoryEA,memoryWORD);
               break;
         }
         computer.R[Rn] = *memoryWORD;
         break;
      }
//----------------------------------------------------------------------------------
// LDAR is the *ONLY* HW instruction required to set *memoryEA and *memoryWORD := NULL
//----------------------------------------------------------------------------------
      case 0X41: // LDAR   Rn,operands
      {
         BYTE mode,Rn,Rn2,Rnx;
         WORD O16;
         
         ReadModeOperand(&mode);
         ReadRnOperand(&Rn);
         switch ( mode )
         {
            case 0X01: // Rn,O16          OpCode:0X01:Rn:O16       EA <- O16 (register,direct)
               ReadO16Operand(&O16);
               *memoryEA = O16;
               break;
            case 0X02: // Rn,@Rn2         OpCode:0X02:Rn:Rn2       EA <- Rn2 (register,indirect)
               ReadRnOperand(&Rn2);
               *memoryEA = computer.R[Rn2];
               break;
            case 0X04: // Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   EA <- O16+Rnx (register,indexed)
               ReadO16Operand(&O16);
               ReadRnOperand(&Rnx);
               *memoryEA = O16+computer.R[Rnx];
               break;
            case 0X10: // Rn,#O16         OpCode:0X10:Rn:O16       EA <- PC+3 (register,immediate)
               *memoryEA = computer.PC;
               break;
            case 0X21: // Rn,FB:O16       OpCode:0X21:Rn:O16       EA <- FB+2*O16 (register,FB-relative/direct)
               ReadO16Operand(&O16);
               *memoryEA = computer.FB+2*O16;
               break;
            case 0X22: // Rn,@FB:O16      OpCode:0X22:Rn:O16       EA <- memory[FB+2*O16] (register,FB-relative/indirect)
               ReadO16Operand(&O16);
               ReadDataWord(computer.FB+2*O16,memoryEA);
               break;
            case 0X25: // Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   EA <- (FB+2*O16)+Rnx (register,FB-relative/direct/indexed)
               ReadO16Operand(&O16);
               ReadRnOperand(&Rnx);
               *memoryEA = computer.FB+2*O16+computer.R[Rnx];
               break;
            case 0X26: // Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   EA <- memory[FB+2*O16]+Rnx (register,FB-relative/indirect/indexed)
               ReadO16Operand(&O16);
               ReadRnOperand(&Rnx);
               ReadDataWord(computer.FB+2*O16,memoryEA);
               *memoryEA += computer.R[Rnx];
               break;
         }
         computer.R[Rn] = *memoryEA;
         *memoryWORD = NULLWORD;
         break;
      }
//----------------------------------------------------------------------------------
// LDR and STR are the *ONLY* HW instructions required to set both *memoryEA and *memoryWORD
//----------------------------------------------------------------------------------
      case 0X42: // STR    Rn,label
      {
         BYTE mode,Rn,Rn2,Rnx;
         WORD O16;
         
         ReadModeOperand(&mode);
         ReadRnOperand(&Rn);
         switch ( mode )
         {
            case 0X01: // Rn,O16          OpCode:0X01:Rn:O16       EA <- O16 (register,direct)
               ReadO16Operand(&O16);
               *memoryEA = O16;
               break;
            case 0X02: // Rn,@Rn2         OpCode:0X02:Rn:Rn2       EA <- Rn2 (register,indirect)
               ReadRnOperand(&Rn2);
               *memoryEA = computer.R[Rn2];
               break;
            case 0X04: // Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   EA <- O16+Rnx (register,indexed)
               ReadO16Operand(&O16);
               ReadRnOperand(&Rnx);
               *memoryEA = O16+computer.R[Rnx];
               break;
            case 0X21: // Rn,FB:O16       OpCode:0X21:Rn:O16       EA <- FB+2*O16 (register,FB-relative/direct)
               ReadO16Operand(&O16);
               *memoryEA = computer.FB+2*O16;
               break;
            case 0X22: // Rn,@FB:O16      OpCode:0X22:Rn:O16       EA <- memory[FB+2*O16] (register,FB-relative/indirect)
               ReadO16Operand(&O16);
               ReadDataWord(computer.FB+2*O16,memoryEA);
               break;
            case 0X25: // Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   EA <- (FB+2*O16)+Rnx (register,FB-relative/direct/indexed)
               ReadO16Operand(&O16);
               ReadRnOperand(&Rnx);
               *memoryEA = computer.FB+2*O16+computer.R[Rnx];
               break;
            case 0X26: // Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   EA <- memory[FB+2*O16]+Rnx (register,FB-relative/indirect/indexed)
               ReadO16Operand(&O16);
               ReadRnOperand(&Rnx);
               ReadDataWord(computer.FB+2*O16,memoryEA);
               *memoryEA += computer.R[Rnx];
               break;
         }
         WriteDataWord(*memoryEA,computer.R[Rn]);
         *memoryWORD = computer.R[Rn];
         break;
      }
      case 0X43: // COPYR  Rn,Rn2          OpCode:Rn:Rn2            Rn <- Rn2
      {
         BYTE Rn,Rn2;
         
         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         computer.R[Rn] = computer.R[Rn2];
         break;
      }
      case 0X44: // PUSHR  Rn              OpCode:Rn                push Rn onto top-of-stack
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         PushWord(computer.R[Rn]);
         break;
      }
      case 0X45: // POPR   Rn              OpCode:Rn                pop top-of-stack into Rn 
      {
         BYTE Rn;
         
         ReadRnOperand(&Rn);
         PopWord(SSBase,&computer.R[Rn]);
         break;
      }
      case 0X46: // SWAPR  Rn,Rn2          OpCode:Rn:Rn2            swap contents of Rn and Rn2
      {
         BYTE Rn,Rn2;
         WORD T;

         ReadRnOperand(&Rn);
         ReadRnOperand(&Rn2);
         T = computer.R[Rn];
         computer.R[Rn] = computer.R[Rn2];
         computer.R[Rn2] = T;
         break;
      }
      case 0X50: // PUSHFB                 OpCode                   push FB onto top-of-stack
      {
         PushWord(computer.FB);
         break;
      }
      case 0X51: // POPFB                  OpCode                   pop top-of-stack into FB
      {
         PopWord(SSBase,&computer.FB);
         break;
      }
      case 0X52: // SETFB  #O16            OpCode:O16              FB <- SP+O16 (O16 signed)
      {
         WORD O16;
         
         ReadO16Operand(&O16);
         computer.FB = computer.SP+O16;
         break;
      }
      case 0X53: // ADJSP  #O16            OpCode:O16              SP <- SP+O16 (O16 signed)
      {
         WORD O16;
         
         ReadO16Operand(&O16);
         computer.SP = computer.SP+O16;
         break;
      }
   /* ============= */
   /* MISCELLANEOUS */
   /* ============= */
/*
         (RESERVED FOR FUTURE USE)
*/      
   /* ============= */
   /* PRIVILEGED    */
   /* ============= */
      default:
/*
         (***NOT ALLOWED***)
         INR   Rn,O16           OpCode:Rn:O16            input byte from IO port into low order byte of Rn
         OUTR  Rn,O16           OpCode:Rn:O16            output byte to IO port from low order byte of Rn
         LDMMU O16              OpCodeLO16               load MMU registers from memory[O16]
         STMMU O16              OpCode:O16               store MMU registers to memory[O16]
         STOP                   OpCode                   stop execution
*/
   /* ============= */
   /* UNKNOWN       */
   /* ============= */
         RaiseHWInterrupt(OPERATION_CODE_INTERRUPT);
         break;
   }
}

//================================================================
// "simulation" of privileged HW instructions
//================================================================
//----------------------------------------------------------------
void DoINR(WORD *Rn,const WORD IOPort)
//----------------------------------------------------------------
{
   *Rn = MAKEWORD(0X00,computer.IOPorts[IOPort]);
}

//----------------------------------------------------------------
void DoOUTR(const WORD Rn,const WORD IOPort)
//----------------------------------------------------------------
{
   computer.IOPorts[IOPort] = LOBYTE(Rn);
}

//----------------------------------------------------------------
void DoLDMMU(const WORD MMURegisters[])
//----------------------------------------------------------------
{
   for (int i = 0; i <= 127; i++)
      computer.MMURegisters[i] = MMURegisters[i];
}

//----------------------------------------------------------------
void DoSTMMU(WORD MMURegisters[])
//----------------------------------------------------------------
{
   for (int i = 0; i <= 127; i++)
      MMURegisters[i] = computer.MMURegisters[i];
}

//----------------------------------------------------------------
void DoSTOP()
//----------------------------------------------------------------
{
   SetCPUState(HALT);
}

//----------------------------------------------------------------
/* private */ void ReadO16Operand(WORD *O16)
//----------------------------------------------------------------
{
// assumes multi-byte quantities are stored in big-endian byte order
   BYTE HOB,LOB;

   ReadCodeLogicalMainMemory(computer.MMURegisters,computer.PC,&HOB);
   computer.PC++;
   ReadCodeLogicalMainMemory(computer.MMURegisters,computer.PC,&LOB);
   computer.PC++;
   *O16 = MAKEWORD(HOB,LOB);
}

//----------------------------------------------------------------
/* private */ void ReadRnOperand(BYTE *Rn)
//----------------------------------------------------------------
{
   ReadCodeLogicalMainMemory(computer.MMURegisters,computer.PC,Rn);
   computer.PC++;
}

//----------------------------------------------------------------
/* private */ void ReadModeOperand(BYTE *mode)
//----------------------------------------------------------------
{
   ReadCodeLogicalMainMemory(computer.MMURegisters,computer.PC,mode);
   computer.PC++;
}

//----------------------------------------------------------------
/* private */ void PushWord(const WORD word)
//----------------------------------------------------------------
{
// assumes multi-byte quantities are stored in big-endian byte order
   BYTE HOB = HIBYTE(word);
   BYTE LOB = LOBYTE(word);
   
   if ( computer.SP == 0X0000 )
      RaiseHWInterrupt(STACK_OVERFLOW_INTERRUPT);
   else
   {
      WriteDataLogicalMainMemory(computer.MMURegisters,computer.SP,HOB);
      computer.SP++;
      WriteDataLogicalMainMemory(computer.MMURegisters,computer.SP,LOB);
      computer.SP++;
   }
}

//----------------------------------------------------------------
/* private */ void PopWord(const WORD SSBase,WORD *word)
//----------------------------------------------------------------
{
// assumes multi-byte quantities are stored in big-endian byte order
   BYTE HOB,LOB;

   if ( computer.SP == SSBase )
      RaiseHWInterrupt(STACK_UNDERFLOW_INTERRUPT);
   else
   {
      computer.SP--;
      ReadDataLogicalMainMemory(computer.MMURegisters,computer.SP,&LOB);
      computer.SP--;
      ReadDataLogicalMainMemory(computer.MMURegisters,computer.SP,&HOB);
      *word = MAKEWORD(HOB,LOB);
   }
}

//----------------------------------------------------------------
/* private */ void ReadDataWord(const WORD address,WORD *word)
//----------------------------------------------------------------
{
// assumes multi-byte quantities are stored in big-endian byte order
   BYTE HOB,LOB;

   ReadDataLogicalMainMemory(computer.MMURegisters,           address,&HOB);
   ReadDataLogicalMainMemory(computer.MMURegisters,(WORD) (address+1),&LOB);
   *word = MAKEWORD(HOB,LOB);
}

//----------------------------------------------------------------
/* private */ void WriteDataWord(const WORD address,const WORD word)
//----------------------------------------------------------------
{
// assumes multi-byte quantities are stored in big-endian byte order
   BYTE HOB = HIBYTE(word);
   BYTE LOB = LOBYTE(word);

   WriteDataLogicalMainMemory(computer.MMURegisters,           address,HOB);
   WriteDataLogicalMainMemory(computer.MMURegisters,(WORD) (address+1),LOB);
}

//----------------------------------------------------------------
/* private */ bool IsNegative(const WORD word)
//----------------------------------------------------------------
{
   return( (word > 32767) ? true : false );
}

//----------------------------------------------------------------
/* private */ bool IsPositive(const WORD word)
//----------------------------------------------------------------
{
   return( (0 < word) && (word < 32768) ? true : false );
}
