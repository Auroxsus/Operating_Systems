//--------------------------------------------------
// Dr. Art Hanna
// CS3350 S16 and S16OS
// S16.c
#define S16_VERSION "FA2022"
//--------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include "Random.h"
#include "Computer.h"
#include "LabelTable.h"

//=============================================================================
// Simulation global data definitions used by S16 and/or S16OS
//=============================================================================
#define EOLC                           1
#define EOFC                           2
#define SOURCE_LINE_LENGTH             512
#define SLASH                          '\\'

typedef enum
{
// pseudo-terminals
      EOFTOKEN,
      INTEGER,
      FIXEDPOINT,
      STRING,
      UNKNOWN,
// reserved words
//  jobstream commands and parameters
      SJOB,                            // S in place of $
      NAME,
      FILE2,                           // cannot use FILE because it has a predefined meaning
      STACK,
      PRIORITY,
      ARRIVAL,
      SEND,                            // again, S in place of $
// boolean literals
      TRUE,
      FALSE,
// configuration settings (TRACE-ing)
      ENABLETRACING,
      TRACEINSTRUCTIONS,
      TRACEMEMORYALLOCATION,
      TRACESJFSCHEDULING,
      TRACESCHEDULER,
      TRACEDISPATCHER,
      TRACEQUEUES,
      TRACESTATISTICS,
      TRACEHWINTERRUPTS,
      TRACEMISCELLANEOUSSVC,
      TRACEPROCESSMANAGEMENT,
      TRACERESOURCEMANAGEMENT,
      TRACETERMINALIO,
      TRACEDISKIO,
      TRACEMEMORYSEGMENTS,
      TRACEMESSAGEBOXES,
      TRACESEMAPHORES,
      TRACEMUTEXES,
      TRACEEVENTS,
      TRACEPIPES,
      TRACEDEADLOCKDETECTION,
      TRACESIGNALS,
// configuration settings (remaining)
      CPUSCHEDULER,
      FCFSCPUSCHEDULER,
      PRIORITYCPUSCHEDULER,
      MINIMUMPRIORITY,
      DEFAULTPRIORITY,
      MAXIMUMPRIORITY,
      SJFCPUSCHEDULER,
      ALPHA,
      USEPREEMPTIVECPUSCHEDULER,
      TIMEQUANTUM,
      USES16CLOCKQUANTUM,
      S16CLOCKQUANTUM,
      DEADLOCKDETECTIONALGORITHM,
      NODEADLOCKDETECTION,
      DEADLOCKDETECTIONMETHOD1,
      DEADLOCKDETECTIONMETHOD2,
      MQSCHEDULER,
      FCFSMQSCHEDULER,
      DISKIOQSCHEDULER,
      FCFSDISKIOQSCHEDULER,
      MINIMUMSVCWAITTIME,
      MAXIMUMSVCWAITTIME,
      CONTEXTSWITCHTIME,
      DEFAULTTERMINALPROMPT,
      TRUESTRING,
      FALSESTRING,
      DEFAULTSSSIZEINPAGES,
// delimiters
      COMMA,
      EQUAL
} TOKEN;

FILE *SOURCE,*TRACE;
char sourceLine[SOURCE_LINE_LENGTH+1],nextCharacter;
int  sourceLineIndex,sourceLineNumber;
bool atEOF,atEOL;
int  S16clock;
char WORKINGDIRECTORY[SOURCE_LINE_LENGTH+1];
char S16DIRECTORY[SOURCE_LINE_LENGTH+1];

//=============================================================================
// Simulation ERROR and WARNING handler used by S16 and S16OS
//=============================================================================
#define S16WARNING                     1
#define S16ERROR                       2
#define S16OSWARNING                   3
#define S16OSERROR                     4
//--------------------------------------------------
void ProcessWarningOrError(const int type,const char message[])
//--------------------------------------------------
{
   switch ( type )
   {
      case S16WARNING:
         printf("\n**** S16 warning: %s\n",message);
         fprintf(TRACE,"**** S16 warning: %s\n",message);
         break;
      case S16ERROR:
         printf("\n**** S16 error: %s, S16 aborted\n\n",message);
         fprintf(TRACE,"**** S16 error: %s, S16 aborted\n",message);
         break;
      case S16OSWARNING:
         printf("*************************************\n");
         printf("@%10d      S16OS warning: %s\n",S16clock,message);
         printf("*************************************\n");
         fprintf(TRACE,"*************************************\n");
         fprintf(TRACE,"@%10d      S16OS warning: %s\n",S16clock,message);
         fprintf(TRACE,"*************************************\n");
         break;
      case S16OSERROR:
         printf("\n*************************************\n");
         printf("@%10d      S16OS error: %s, S16 aborted\n",S16clock,message);
         printf("*************************************\n\n");
         fprintf(TRACE,"*************************************\n");
         fprintf(TRACE,"@%10d      S16OS error: %s, S16 aborted\n",S16clock,message);
         fprintf(TRACE,"*************************************\n");
         break;
   }
   fflush(stdout);
   fflush(TRACE);
   if ( (type == S16ERROR) || (type == S16OSERROR) )
   {
      fclose(TRACE);
      system("PAUSE");
      exit(1);
   }
}

//=============================================================================
// Start of S16OS: data definitions
//=============================================================================
//--------------------------------------------------
// System-wide collection of S16OS-managed resources
//    * Jobs (processes)
//    * Message boxes
//    * Memory segments
//    * Counting semaphores
//    * Mutexes (binary semaphores)
//    * Events
//    * Pipes
//
// When a system resource is allocated, the resource is assigned a uniquely
//    identifying handle that is the index in resources[]. resources[handle]
//    is used to store information about the newly-allocated resource during
//    the resource lifetime. *Note* After a handle is allocated, it is never 
//    used again to identify another resource (that is, the handle is not "recycled").
//
// When new resource types are added to S16OS, there are statements
//    in the following functions that need to be reconsidered
//       * void TraceStateOfSystemQueues()
//--------------------------------------------------
#define MAXIMUM_RESOURCES 512

typedef int HANDLE;

typedef enum
{
   NOTCREATEDS       = 0,
   JOBS              = 1,
   MESSAGEBOXES      = 2,
   MEMORYSEGMENTS    = 3,
   SEMAPHORES        = 4,
   MUTEXES           = 5,
   EVENTS            = 6,
   PIPES             = 7
} RESOURCETYPE;

typedef struct RESOURCE
{
   bool allocated;
   RESOURCETYPE type;
   char *name;                                // points-to dynamically-allocated name
   void *object;                              // points-to resource object
} RESOURCE;

//--------------------------------------------------
// (Q)ueue is a singly-linked list of
//    QNODEs containing a pointer-to the
//    S16OS object contained in the node
//--------------------------------------------------
typedef struct QNODE
{
   void *object;                             // generic pointer-to object "in" the node
   struct QNODE *FLink;                      // forward-looking pointer-to next node on linked list
} QNODE;

typedef struct QUEUE
{
   int size;                                 // number of nodes "on" the queue
   char *name;                               // name of queue
   bool containsPCBs;                        // true when queue contains PCB resources
   void (*TraceQNODEObject)(void *object);   // function-pointer for detailed trace of QNODE object
   QNODE *head;                              // pointer-to first node on linked list
   QNODE *tail;                              // pointer-to last  node on linked list
} QUEUE;

//--------------------------------------------------
// Process Control Block (PCB), an instance of JOBS resource
//--------------------------------------------------
typedef struct PCB
{
   HANDLE handle;
   int priority;
   char type;                                    // 'H'eavyweight process, 'T'hread process
   int childThreadActiveCount;                   // used only by heavyweight parent process
   QUEUE  childThreadQ;                          // used only by heavyweight parent process
   struct PCB *parentPCB;                        // used only by lightweight thread process
   bool terminateProcess;                        // used only by lightweight thread process
   bool suspendProcess;                          // used only by lightweight thread process
   int takeOffJobQTime;                          // ARRIVAL time
   int takeOffWaitQTime;
   HANDLE handleOfChildThreadJoined;             // used only by heavyweight parent process
   int takeOffSleepQTime;
   WORD SVCErrorHandler;
   WORD signalHandler;
// CPU registers
   WORD PC;
   WORD SP;
   WORD FB;
   WORD R[16];                                   // R[0] = R0, R[1] = R1,...,R[15] = R15
// MMU registers
   WORD MMURegisters[128];
// Object program
   WORD CSBase,CSSize;
   WORD DSBase,DSSize;
   WORD SSBase,SSSize;
   BYTE *memory;
   LABELTABLE labelTable;
// Terminal IO
   BYTE OUT[MEMORY_PAGE_SIZE_IN_BYTES];
// Lifetime statistics and miscellany
   int turnAroundTime;
   int runStateTime,runStateCount;
   int readyStateTime,readyStateCount;
   int waitStateTime,waitStateCount;
   int sleepStateTime,sleepStateCount;
   int joinStateTime,joinStateCount;             // used only by heavyweight parent process
   int suspendedStateTime,suspendedStateCount;   // used only by lightweight thread process
   int semaphoreWaitTime,semaphoreWaitCount;
   int mutexWaitTime,mutexWaitCount;
   int messageWaitTime,messageWaitCount;
   int eventWaitTime,eventWaitCount;
   int diskIOWaitTime,diskIOWaitCount;
   int resourcesWaitTime,resourcesWaitCount;
   int contextSwitchTime,contextSwitchCount;
   int HWInterruptCount;
   int CPUBurstCount,IOBurstCount;
   int signalsSentCount,signalsIgnoredCount,signalsHandledCount;
   int FCFSTime;
   int t,tau;
   int allocatedResourcesCount,deallocatedResourcesCount;
} PCB;

//--------------------------------------------------
// Message Box, an instance of MESSAGEBOXES resource
//--------------------------------------------------
typedef struct MESSAGEBOX
{
   HANDLE handle;
   HANDLE ownerHandle;
   bool waiting;
   WORD pointerToRequestedMessage;
   int capacity;                             // allowed capacity of queue, 0 = unbounded
   QUEUE Q;
} MESSAGEBOX;

//--------------------------------------------------
// Memory-segment, an instance of MEMORYSEGMENTS resource
//--------------------------------------------------
typedef struct MEMORYSEGMENT
{
   HANDLE handle;
   HANDLE ownerHandle;
   WORD LBPageNumber;
   WORD UBPageNumber;
} MEMORYSEGMENT;

//--------------------------------------------------
// Semaphore and Mutex, an instance of SEMAPHORES and MUTEXES resources
//--------------------------------------------------
typedef struct SEMAPHORE
{
   HANDLE handle;
   HANDLE ownerHandle;
   int value;
   QUEUE Q;
} SEMAPHORE;

//--------------------------------------------------
// Event, an instance of EVENTS resource
//--------------------------------------------------
typedef struct EVENT
{
   HANDLE handle;
   HANDLE ownerHandle;
   int missedSignals;
   QUEUE Q;
} EVENT;

//--------------------------------------------------
// Pipe, an instance of PIPES resource
//--------------------------------------------------
typedef struct PIPE
{
   HANDLE handle;
   HANDLE ownerHandle;
   int capacity;                             // capacity of circular buffer
   int head;                                 // head of circular buffer (the last buffer element read)
   int tail;                                 // tail of circular buffer (the last buffer element written)
   int size;                                 // number of words in circular buffer
   WORD *buffer;                             // dynamically-allocate array with index range in [0..(capacity-1)]
} PIPE;

//--------------------------------------------------
// Non-PCB queue nodes
//--------------------------------------------------
typedef struct MESSAGE
{
   struct
   {
      WORD length;
      WORD fromMessageBoxHandle;
      WORD toMessageBoxHandle;
      WORD priority;
   } header;
   WORD *block;                              // dynamically-allocated array block[length]
} MESSAGE;

typedef struct DISKIO
{
   PCB *pcb;                                 // PCB of process that made disk IO request
   int command;                              // disk controller read=2/write=3 sector command
   WORD sectorAddress;                       // disk sector address
   WORD bufferAddress;                       // disk buffer main memory logical address in process
   WORD sectorSize;                          // BYTES_PER_SECTOR
} DISKIO;

typedef struct RESOURCEWAIT
{
   PCB *pcb;                                 // PCB of process that made wait request
   char *name;                               // resource name waited for
} RESOURCEWAIT;

typedef struct CHILDTHREAD
{
   HANDLE handle;
   bool isActive;
} CHILDTHREAD;

typedef struct SIGNAL
{
   HANDLE senderProcessHandle;
   HANDLE signaledProcessHandle;
   int signal;
} SIGNAL;

//--------------------------------------------------
// System service requests and return codes
//--------------------------------------------------
typedef enum
{
// miscellaneous
   SVC_DO_NOTHING                        =     0,
   SVC_GET_CURRENT_TIME                  =     1,
   SVC_GET_RANDOM_INTEGER                =     2,
// process management
   SVC_TERMINATE_PROCESS                 =   100,
   SVC_ABORT_PROCESS                     =   101,
   SVC_SLEEP_PROCESS                     =   102,
   SVC_DUMP_PROCESS_DATA                 =   103,
   SVC_GET_PROCESS_HANDLE                =   104,
   SVC_GET_PROCESS_NAME                  =   105,
   SVC_GET_PROCESS_PRIORITY              =   106,
   SVC_SET_PROCESS_PRIORITY              =   107,
   SVC_SET_PROCESS_ERROR_HANDLER         =   108,
   SVC_GET_DATA_SEGMENT_UB_PAGE          =   109,
   SVC_GET_STACK_SEGMENT_SIZE_IN_PAGES   =   110,
   SVC_SET_STACK_SEGMENT_SIZE_IN_PAGES   =   111,

   SVC_SET_PROCESS_SIGNAL_HANDLER        =   112,
   SVC_SEND_SIGNAL_TO_PROCESS            =   113,

   SVC_CREATE_CHILD_PROCESS              =   150,
   SVC_CREATE_CHILD_THREAD               =   180,
   SVC_GET_ACTIVE_CHILD_THREAD_COUNT     =   181,
   SVC_SUSPEND_CHILD_THREAD              =   182,
   SVC_RESUME_CHILD_THREAD               =   183,
   SVC_TERMINATE_CHILD_THREAD            =   184,
   SVC_JOIN_CHILD_THREAD                 =   185,
// resource management
   SVC_GET_RESOURCE_TYPE                 =   200,
   SVC_GET_RESOURCE_HANDLE               =   201,
   SVC_WAIT_FOR_RESOURCE_HANDLE          =   202,
   SVC_GET_RESOURCE_NAME                 =   203,
// terminal IO
   SVC_READ_FROM_TERMINAL                =   300,
   SVC_WRITE_TO_TERMINAL                 =   301,
// disk drive IO
   SVC_READ_DISK_SECTOR                  =   400,
   SVC_WRITE_DISK_SECTOR                 =   401,
// memory segments
   SVC_CREATE_MEMORY_SEGMENT             =   500,
   SVC_DESTROY_MEMORY_SEGMENT            =   501,
   SVC_SHARE_MEMORY_SEGMENT              =   502,
   SVC_UNSHARE_MEMORY_SEGMENT            =   503,
   SVC_GET_MEMORY_SEGMENT_LB_PAGE        =   504,
   SVC_GET_MEMORY_SEGMENT_SIZE_IN_PAGES  =   505,
// message boxes
   SVC_CREATE_MESSAGEBOX                 =   600,
   SVC_DESTROY_MESSAGEBOX                =   601,
   SVC_SEND_MESSAGE                      =   602,
   SVC_REQUEST_MESSAGE                   =   603,
   SVC_GET_MESSAGE_COUNT                 =   604,
// semaphores
   SVC_CREATE_SEMAPHORE                  =   700,
   SVC_DESTROY_SEMAPHORE                 =   701,
   SVC_WAIT_SEMAPHORE                    =   702,
   SVC_SIGNAL_SEMAPHORE                  =   703,
// mutexes
   SVC_CREATE_MUTEX                      =   800,
   SVC_DESTROY_MUTEX                     =   801,
   SVC_LOCK_MUTEX                        =   802,
   SVC_UNLOCK_MUTEX                      =   803,
// events
   SVC_CREATE_EVENT                      =   900,
   SVC_DESTROY_EVENT                     =   901,
   SVC_SIGNAL_EVENT                      =   902,
   SVC_SIGNALALL_EVENT                   =   903,
   SVC_WAIT_EVENT                        =   904,
   SVC_GET_EVENT_QUEUE_SIZE              =   905,
// pipes
   SVC_CREATE_PIPE                       =  1000,
   SVC_DESTROY_PIPE                      =  1001,
   SVC_READ_PIPE                         =  1002,
   SVC_WRITE_PIPE                        =  1003,
   SVC_GET_PIPE_SIZE                     =  1004
} SVC_REQUEST_NUMBER;

typedef enum
{
   SVC_EOF      =  -1, // Terminal input EOF
   SVC_OK       =   0, // No error occurred
   SVC_ERROR001 =   1, // Unknown service request number
   SVC_ERROR002 =   2, // Terminal IO error
   SVC_ERROR003 =   3, // Semaphore value must be >= 0
   SVC_ERROR004 =   4, // Message box queue size too large
   SVC_ERROR005 =   5, // Set priority failure, priority is out-of-range
   SVC_ERROR006 =   6, // SVC error handler address is out-of-range
   SVC_ERROR007 =   7, // Pipe capacity out-of-range
   SVC_ERROR008 =   8, // Unable to create resource
   SVC_ERROR009 =   9, // Resource does not exist
   SVC_ERROR010 =  10, // Only owner can destroy resource
   SVC_ERROR011 =  11, // Sector address is out-of-range
   SVC_ERROR012 =  12, // Invalid handle
   SVC_ERROR013 =  13, // Message box owner only allowed to send/request from box
   SVC_ERROR014 =  14, // Message box capacity must be >= 0
   SVC_ERROR015 =  15, // Random number interval upper-bound must be >=0
   SVC_ERROR016 =  16, // Child jobstream file must contain only 1 job
   SVC_ERROR017 =  17, // Child jobstream file contains a syntax error
   SVC_ERROR018 =  18, // Set stack size failure, stack must be empty
   SVC_ERROR019 =  19, // Set stack size failure, size-in-pages is out-of-range
   SVC_ERROR020 =  20, // Set stack size failure, page-range not free
   SVC_ERROR021 =  21, // Create memory-segment failure, page-range not free
   SVC_ERROR022 =  22, // Share memory-segment failure, page-range not free
   SVC_ERROR023 =  23, // Signal ignored
   SVC_ERROR024 =  24, // Signal handler address is out-of-range
   SVC_ERRORXXX        // (Not used)
} SVC_RETURN_CODE;

//--------------------------------------------------
// S16OS resources
//--------------------------------------------------
RESOURCE resources[MAXIMUM_RESOURCES+1];
int resourcesCount;

//--------------------------------------------------
// job queue is a priority queue of PCBs (new entries are arranged
//    in ascending order based on the PCB field takeOffJobQTime)
//--------------------------------------------------
QUEUE jobQ;

//--------------------------------------------------
// ready queue is a priority queue of PCBs (new entries are arranged
//    in an order determined by the CPU short-term scheduler algorithm)
//--------------------------------------------------
QUEUE readyQ;

//--------------------------------------------------
// wait queue is a priority queue of PCBs (new entries are arranged
//    in ascending order based on the PCB field takeOffWaitQTime)
//--------------------------------------------------
QUEUE waitQ;

//--------------------------------------------------
// join queue is a list of parent process PCBs that have joined (that is, 
//    are waiting for termination of) a child thread (new entries are added
//    to the end of the queue)
//--------------------------------------------------
QUEUE joinQ;

//--------------------------------------------------
// signals queue is a list of not-yet-handled signals sent to
//    processes with non-NULL signal handlers (new entries are added
//    to end of queue)
//--------------------------------------------------
QUEUE signalsQ;

//--------------------------------------------------
// sleep queue is a priority queue of PCBs (new entries are arranged
//    in ascending order based on the PCB field takeOffSleepQTime)
//--------------------------------------------------
QUEUE sleepQ;

//--------------------------------------------------
// suspended queue is a list of child thread process PCBs 
//    suspended by parent process (new entries are added to
//    the end of the queue)
//--------------------------------------------------
QUEUE suspendedQ;

//--------------------------------------------------
// resources wait queue is a list of PCB/resource name
//    combinations each waiting for a specific resource to be
//    created before being allowed to continue execution (new entries
//    are added to end of queue)
//--------------------------------------------------
QUEUE resourcesWaitQ;

//--------------------------------------------------
// disk IO request queue is a list of pending disk IO requests
//    (new entries are added to end of queue)
//--------------------------------------------------
QUEUE diskIOQ;

//--------------------------------------------------
// pointer-to the running process (PCB currently in the RUN state)
//--------------------------------------------------
PCB *pcbInRunState;

//--------------------------------------------------
// pointer-to the disk IO request currently being processed
//--------------------------------------------------
DISKIO *diskIOInProgress;

//--------------------------------------------------
// memoryPages[i] = "main memory page i is allocated" for i in [ 0,MEMORY_PAGES-1 ]
//--------------------------------------------------
bool memoryPages[MEMORY_PAGES];

//--------------------------------------------------
// number-of-jobs statistics
//--------------------------------------------------
int numberOfJobsCreated,numberOfJobsTerminated;

//--------------------------------------------------
// throughput and CPU utilization statistics
//--------------------------------------------------
int throughputTime,HWInstructionCount,IDLETime,contextSwitchTime;

//--------------------------------------------------
// Method #1: Single-instance deadlock-detection (based on textbook section 7.6.1)
//--------------------------------------------------
// (R)esource (A)llocation (G)raph element RAG[r][c] 
//    true  means that resource r does     "point-to" resource c
//    false means that resource r does not "point-to" resource c
//--------------------------------------------------
bool RAG[MAXIMUM_RESOURCES+1][MAXIMUM_RESOURCES+1];

//--------------------------------------------------
// Method #2: Several-instance deadlock-detection (based on textbook section 7.6.2)
//--------------------------------------------------
// AVAILABLE[c]     means number of resources[c] currently available
// ALLOCATION[r][c] means number of resources[c] currently allocated to resources[r]
// REQUEST[r][c]    means number of resources[c] currently requested by resources[r]
// *Note* By design, r is JOBS resource handle (a process) and c is a non-JOBS resource
//--------------------------------------------------
int AVAILABLE[MAXIMUM_RESOURCES+1];
int ALLOCATION[MAXIMUM_RESOURCES+1][MAXIMUM_RESOURCES+1];
int REQUEST[MAXIMUM_RESOURCES+1][MAXIMUM_RESOURCES+1];

//--------------------------------------------------
// Configuration parameters
//--------------------------------------------------
// TRACE-enabling boolean flags; change the flag to true/false to turn on/off
//    trace output to the TRACE file; set the ENABLE_TRACING flag to false
//    to disable *ALL* tracing (even tracing that is individually enabled)
//--------------------------------------------------
bool ENABLE_TRACING;

bool TRACE_INSTRUCTIONS;
bool TRACE_MEMORY_ALLOCATION;
bool TRACE_SJF_SCHEDULING;
bool TRACE_SCHEDULER;
bool TRACE_DISPATCHER;
bool TRACE_QUEUES;
bool TRACE_STATISTICS;
bool TRACE_HWINTERRUPTS;
bool TRACE_MISCELLANEOUS_SVC;
bool TRACE_PROCESS_MANAGEMENT;
bool TRACE_RESOURCE_MANAGEMENT;
bool TRACE_TERMINAL_IO;
bool TRACE_DISK_IO;
bool TRACE_MEMORYSEGMENTS;
bool TRACE_MESSAGEBOXES;
bool TRACE_SEMAPHORES;
bool TRACE_MUTEXES;
bool TRACE_EVENTS;
bool TRACE_PIPES;
bool TRACE_DEADLOCK_DETECTION;
bool TRACE_SIGNALS;

//--------------------------------------------------
// CPU short-term scheduler. When adding a ready process to the ready queue
//    * First Come/First Serve (FCFS) when CPU_SCHEDULER = FCFS_CPU_SCHEDULER
//    * Priority scheduler is used when CPU_SCHEDULER = PRIORITY_CPU_SCHEDULER
//    * SJF scheduler is used when CPU_SCHEDULER = SJF_CPU_SCHEDULER
//--------------------------------------------------
#define FCFS_CPU_SCHEDULER     1
#define PRIORITY_CPU_SCHEDULER 2
#define SJF_CPU_SCHEDULER      3
int CPU_SCHEDULER;

//--------------------------------------------------
// scheduling priority is inverted, so should satisfy
//    ( MAXIMUM_PRIORITY <= DEFAULT_PRIORITY <= MINIMUM_PRIORITY )
//--------------------------------------------------
int MINIMUM_PRIORITY;
int DEFAULT_PRIORITY;
int MAXIMUM_PRIORITY;

//--------------------------------------------------
// SJF alpha parameter setting should be a fixed-point number in the range [ 0.0,1.0 ]
//--------------------------------------------------
double alpha;

//--------------------------------------------------
// A preemptive scheduler is used if-and-only-if USE_PREEMPTIVE_CPU_SCHEDULER
//   is TRUE, otherwise a non-preemptive scheduler is used. Time quantum setting should 
//   satisfy ( 50 <= TIME_QUANTUM ).
//--------------------------------------------------
bool USE_PREEMPTIVE_CPU_SCHEDULER;
int TIME_QUANTUM;

//--------------------------------------------------
// (M)essage (Q)ueue scheduler. The message queue scheduling algorithms are
//    mutually exclusive. Currently, message queue scheduler configuration setting 
//    must be FCFS_MQ_SCHEDULER.
//--------------------------------------------------
#define FCFS_MQ_SCHEDULER        1
#define some_other_MQ_scheduler2 2
#define some_other_MQ_scheduler3 3
int MQ_SCHEDULER;

//--------------------------------------------------
// Disk IO queue scheduler (diskIOQ scheduling algorithms are mutually exclusive).
//    Currently, disk IO queue scheduler configuration setting must be FCFS_DISKIOQ_SCHEDULER.
//--------------------------------------------------
#define FCFS_DISKIOQ_SCHEDULER        1
#define some_other_diskIOQ_scheduler2 2
#define some_other_diskIOQ_scheduler3 3
int DISKIOQ_SCHEDULER;

//--------------------------------------------------
// S16 programmer is prompted every S16CLOCK_QUANTUM
//    S16Clock ticks to answer question "Continue?" executing
//    instructions. (*Note* Used to guard against infinite loops.)
//    S16clock quantum should satisfy ( 10000 <= S16CLOCK_QUANTUM ).
//--------------------------------------------------
bool USE_S16CLOCK_QUANTUM;
int S16CLOCK_QUANTUM;

//--------------------------------------------------
// Establish the interval [ MINIMUM_SVCWAITTIME,MAXIMUM_SVCWAITTIME ]
//    used by the function SVCWaitTime() to randomly-choose SVC wait times 
//    (measured in S16clock ticks). Service request wait time configuration settings
//    should satisfy ( 20 <= MINIMUM_SVCWAITTIME <= MAXIMUM_SVCWAITTIME ).
//--------------------------------------------------
int MINIMUM_SVCWAITTIME;
int MAXIMUM_SVCWAITTIME;

//--------------------------------------------------
// CONTEXT_SWITCH_TIME is measured in S16clock ticks. Context switch time
//    setting should satisfy ( CONTEXT_SWITCH_TIME > 0 ).
//--------------------------------------------------
int CONTEXT_SWITCH_TIME;

//--------------------------------------------------
// Deadlock detection algorithm. Deadlock detection algorithm should be one of 
//    * DEADLOCK_DETECTION_ALGORITHM = NO_DEADLOCK_DETECTION disables deadlock detection
//    * DEADLOCK_DETECTION_ALGORITHM = DEADLOCK_DETECTION_METHOD1 "works" when job uses only
//         single-instance resources (like MUTEX-es)
//    * DEADLOCK_DETECTION_ALGORITHM = DEADLOCK_DETECTION_METHOD2 "works" when job uses 
//         several-instance resource (like SEMAPHORE-es initialized value > 1)
//--------------------------------------------------
#define NO_DEADLOCK_DETECTION      0
#define DEADLOCK_DETECTION_METHOD1 1
#define DEADLOCK_DETECTION_METHOD2 2
int DEADLOCK_DETECTION_ALGORITHM;

//--------------------------------------------------
// terminal IO settings
//--------------------------------------------------
char defaultTerminalPrompt[MEMORY_PAGE_SIZE_IN_BYTES];
char TRUEString[MEMORY_PAGE_SIZE_IN_BYTES];
char FALSEString[MEMORY_PAGE_SIZE_IN_BYTES];

//--------------------------------------------------
// Default SSSize measured in pages should be in range [ 1,126 ].
//--------------------------------------------------
int defaultSSSizeInPages;

//=============================================================================
// S16OS three entry points: Start, HWInterrupt, and Service Request (SVC) 
//=============================================================================
//--------------------------------------------------
void S16OS_GoToStartEntryPoint()
//--------------------------------------------------
{
   void ConstructQ(QUEUE *queue,char *name,bool containsPCBs,void (*TraceQNODEObject)(void *object));
   void TraceReadyQObject(void *object);
   void TraceJobQObject(void *object);
   void TraceWaitQObject(void *object);
   void TraceJoinQObject(void *object);
   void TraceSignalsQObject(void *object);
   void TraceSuspendedQObject(void *object);
   void TraceSleepQObject(void *object);
   void TraceResourcesWaitQObject(void *object);
   void TraceDiskIOQObject(void *object);
   void DriverSWForTimer(int command,...);
   void DriverSWForDisk(int command,...);

// initialize resources[] to empty
   resourcesCount = 0;
   for (int i = 1; i <= MAXIMUM_RESOURCES; i++)
   {
      resources[i].allocated = false;
      resources[i].name = NULL;
      resources[i].object = NULL;
   }

// initialize ready, job, wait, join, suspended, sleep, and resources wait queues to empty
   ConstructQ(&readyQ,"readyQ",true,TraceReadyQObject);
   ConstructQ(&jobQ,"jobQ",true,TraceJobQObject);
   ConstructQ(&waitQ,"waitQ",true,TraceWaitQObject);
   ConstructQ(&joinQ,"joinQ",true,TraceJoinQObject);
   ConstructQ(&signalsQ,"signalsQ",false,TraceSignalsQObject);
   ConstructQ(&suspendedQ,"suspendedQ",true,TraceSuspendedQObject);
   ConstructQ(&sleepQ,"sleepQ",true,TraceSleepQObject);
   ConstructQ(&resourcesWaitQ,"resourcesWaitQ",false,TraceResourcesWaitQObject);

// initialize all main memory pages to unallocated
   for (int page = 0; page <= MEMORY_PAGES-1; page++)
      memoryPages[page] = false;

// initialize CPU utilization statistic
   HWInstructionCount = 0;
   IDLETime = 0;
   contextSwitchTime = 0;

// initialize number-of-job statistics
   numberOfJobsCreated = 0;
   numberOfJobsTerminated = 0;

// initialize disk IO queue and diskIOInProgress
   ConstructQ(&diskIOQ,"diskIOQ",false,TraceDiskIOQObject);
   diskIOInProgress = NULL;

//--------------------------------------------------
// initialize hardware devices
//    1. Priority Interrupt Device (PID)
//    2. disable timer
//    3. disable disk controller
//--------------------------------------------------
   ClearHWInterrupt();
   DriverSWForTimer(1);
   DriverSWForDisk(1);
}

//--------------------------------------------------
void S16OS_GoToHWInterruptEntryPoint()
//--------------------------------------------------
{
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToReadyQNow(void *pcbR,void *pcb);
   bool AddPCBToWaitQNow(void *pcbR,void *pcb);
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void DriverSWForDisk(int command,...);
   void DriverSWForTimer(int command,...);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);
   void S16OS_CPUScheduler();
   void S16OS_Dispatcher();

/*
   The CPU state must be either
      1.  RUN (the running process pointed to by pcbInRunState was just hardware-interrupted)
   or
      2. IDLE (there is no running process)
*/

// get HW interrupt number from PIM and clear interrupt
   WORD PIDHWInterruptNumber;

   DoINR(&PIDHWInterruptNumber,PID_HWINTERRUPT_NUMBER);
   ClearHWInterrupt();

// save CPU state in PCB of interrupted running process
   if ( GetCPUState() == RUN )
   {
      PCB *pcb = pcbInRunState;
      int n;

      // TRACE-ing
      if ( ENABLE_TRACING && TRACE_HWINTERRUPTS )
      {
         fprintf(TRACE,"@%10d(%3d) hardware interrupt 0X%02X\n",
            S16clock,pcb->handle,PIDHWInterruptNumber);
         fflush(TRACE);
      }
      // ENDTRACE-ing

      pcb->PC = GetPC();
      pcb->SP = GetSP();
      pcb->FB = GetFB();
      for (int n = 0; n <= 15; n++)
         pcb->R[n] = GetRn(n);
      DoSTMMU(pcb->MMURegisters);

   // update run state time statistics for PCB
      pcb->runStateTime += S16clock;

   // and CPU burst length, t, and compute tau = alpha*t + (1.0-alpha)*tau
      pcb->t += S16clock;
      pcb->tau = (int) ( alpha*pcb->t + (1.0-alpha)*pcb->tau) ;

      // TRACE-ing
      if ( ENABLE_TRACING && TRACE_SJF_SCHEDULING )
      {
         fprintf(TRACE,"@%10d(%3d) t = %5d, tau = %5d\n",
            S16clock,pcb->handle,pcb->t,pcb->tau);
         fflush(TRACE);
      }
      // ENDTRACE-ing

   // add PCB of interrupted job to ready queue
      pcb->FCFSTime = S16clock;
      AddObjectToQ(&readyQ,pcb,AddPCBToReadyQNow);

   // update clock and context switch time/count statistics
      pcb->contextSwitchTime += CONTEXT_SWITCH_TIME/2;
      S16clock = S16clock+CONTEXT_SWITCH_TIME/2;
      pcb->contextSwitchCount++;
      contextSwitchTime += CONTEXT_SWITCH_TIME/2;

   // update ready state time/count statistic for PCB
      pcb->readyStateTime -= S16clock;
      pcb->readyStateCount++;

   // and update H/W interrupt count
      pcb->HWInterruptCount++;

      if ( USE_PREEMPTIVE_CPU_SCHEDULER )
      {
         // disable the timer
            DriverSWForTimer(1);
      }
   }
   else
   {

      // TRACE-ing
      if ( ENABLE_TRACING && TRACE_HWINTERRUPTS )
      {
         fprintf(TRACE,"@%10d     IDLE hardware interrupt 0X%02X\n",S16clock,PIDHWInterruptNumber);
         fflush(TRACE);
      }
      // ENDTRACE-ing

   }
/*
   Handle hardware interrupt. Some of the hardware interrupts signify a
      fatal run-time error that causes the S16 CPU to enter HALT state. The return-s
      found in some of the case-s below are *ABSOLUTELY* necessary so that 
      scheduling-and-dispatching are bypassed "on the way" to HALT-ing in main().
*/
   switch ( PIDHWInterruptNumber )
   {
      case MEMORY_ACCESS_INTERRUPT:
         ProcessWarningOrError(S16OSWARNING,"Memory access interrupt");
         DoSTOP();
         return;

      case PAGE_FAULT_INTERRUPT:
         ProcessWarningOrError(S16OSWARNING,"Page fault interrupt");
         DoSTOP();
         return;

      case OPERATION_CODE_INTERRUPT:
         ProcessWarningOrError(S16OSWARNING,"Unknown operation code interrupt");
         DoSTOP();
         return;

      case DIVISION_BY_0_INTERRUPT:
         ProcessWarningOrError(S16OSWARNING,"Division-by-0 interrupt");
         break;

      case STACK_UNDERFLOW_INTERRUPT:
         ProcessWarningOrError(S16OSWARNING,"Stack underflow interrupt");
         DoSTOP();
         return;

      case STACK_OVERFLOW_INTERRUPT:
         ProcessWarningOrError(S16OSWARNING,"Stack overflow interrupt");
         DoSTOP();
         return;

      case TIMER_INTERRUPT:
         if ( USE_PREEMPTIVE_CPU_SCHEDULER )
         {
         /*
            All handling of a timer interrupt has already been done; a context switch has been started; 
               and the interrupted process has been added to the ready queue.
         */
            break;
         }
         else
         {
            ProcessWarningOrError(S16OSWARNING,"Unexpected timer interrupt");
            DoSTOP();
            return;
         }

      case DISK_INTERRUPT:
         {
            PCB *pcb = diskIOInProgress->pcb;
            int waitTime;

         // update disk IO wait statistics
            pcb->diskIOWaitTime += S16clock;

            if ( diskIOInProgress->command == 2 )          // transfer data from disk sector to job buffer
            {
               int sectorAddress;
               int bufferAddress;
               WORD HOB,LOB;

               DoINR(&HOB,DISK_SECTOR_HOB);
               DoINR(&LOB,DISK_SECTOR_LOB);
               sectorAddress = MAKEWORD(HOB,LOB);
               DoINR(&HOB,DISK_BUFFER_HOB);
               DoINR(&LOB,DISK_BUFFER_LOB);
               bufferAddress = MAKEWORD(HOB,LOB);
               fseek(GetComputerDISK(),(sectorAddress*BYTES_PER_SECTOR),SEEK_SET);
               for (int i = 0; i <= BYTES_PER_SECTOR-1; i++)
               {
                  BYTE byte;

                  fread(&byte,1,1,GetComputerDISK());
                  WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (bufferAddress+i),byte);
               }

               waitTime = SVCWaitTime(SVC_READ_DISK_SECTOR);

               // TRACE-ing
               if ( ENABLE_TRACING && TRACE_DISK_IO )
               {
                  fprintf(TRACE,"@%10d(%3d) complete read %d-byte sector #%d into buffer at OX%04X\n",
                     S16clock,pcb->handle,diskIOInProgress->sectorSize,(int) diskIOInProgress->sectorAddress,
                     diskIOInProgress->bufferAddress);
                  fflush(TRACE);
               }
               // ENDTRACE-ing

            }
            else if ( diskIOInProgress->command == 3 )     // transfer data from job buffer to disk sector
            {
               int sectorAddress;
               int bufferAddress;
               WORD HOB,LOB;

               DoINR(&HOB,DISK_SECTOR_HOB);
               DoINR(&LOB,DISK_SECTOR_LOB);
               sectorAddress = MAKEWORD(HOB,LOB);
               DoINR(&HOB,DISK_BUFFER_HOB);
               DoINR(&LOB,DISK_BUFFER_LOB);
               bufferAddress = MAKEWORD(HOB,LOB);
               fseek(GetComputerDISK(),(sectorAddress*BYTES_PER_SECTOR),SEEK_SET);
               for (int i = 0; i <= BYTES_PER_SECTOR-1; i++)
               {
                  BYTE byte;

                  ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (bufferAddress+i),&byte);
                  fwrite(&byte,1,1,GetComputerDISK());
               }

               waitTime = SVCWaitTime(SVC_WRITE_DISK_SECTOR);

               // TRACE-ing
               if ( ENABLE_TRACING && TRACE_DISK_IO )
               {
                  fprintf(TRACE,"@%10d(%3d) complete write %d-byte sector #%d from buffer at OX%04X\n",
                     S16clock,pcb->handle,diskIOInProgress->sectorSize,(int) diskIOInProgress->sectorAddress,
                     diskIOInProgress->bufferAddress);
                  fflush(TRACE);
               }
               // ENDTRACE-ing

            }
         // add process that was waiting for disk IO to complete to the wait queue and update wait queue statistics
            pcb->takeOffWaitQTime = S16clock + waitTime;
            AddObjectToQ(&waitQ,pcb,AddPCBToWaitQNow);
            pcb->waitStateTime -= S16clock;
            pcb->waitStateCount++;

         // deallocate disk request in progress (because it has completed)
            free(diskIOInProgress);

         // when disk request queue is not empty, take next disk request from the disk request queue
            if ( diskIOQ.size > 0 )
            {
               DISKIO *diskIO;

               RemoveObjectFromQ(&diskIOQ,1,(void **) &diskIO);

               // TRACE-ing
               if ( ENABLE_TRACING && TRACE_DISK_IO )
               {
                  fprintf(TRACE,"@%10d(%3d) request removed from diskIOQ\n",S16clock,diskIO->pcb->handle);
                  fflush(TRACE);
               }
               // ENDTRACE-ing

            // call the disk driver to start the IO request
               DriverSWForDisk(diskIO->command,diskIO);
               diskIOInProgress = diskIO;
            }
         // otherwise, disable disk controller
            else
            {
               DriverSWForDisk(1);
               diskIOInProgress = NULL;
            }
            break;
         }
      default:
         ProcessWarningOrError(S16OSWARNING,"Unknown hardware interrupt");
         DoSTOP();
         return;
   }

// when ready queue is empty, set CPU state to IDLE
   if ( readyQ.size == 0 )
      SetCPUState(IDLE);
// otherwise, schedule and dispatch next job in ready queue
   else
   {
      S16OS_CPUScheduler();
      S16OS_Dispatcher();
   }
}

//--------------------------------------------------
void DriverSWForTimer(int command,...)
//                                     1          ) disable timer
//                                     2,int count) enable timer with count
//--------------------------------------------------
{
   va_list parameters;

   va_start(parameters,command);
   switch ( command )
   {
      case 1:
         DoOUTR((WORD) 0X00,TIMER_ENABLED);
         break;
      case 2:
      {
         int count = va_arg(parameters,int);

         DoOUTR(HIBYTE(count),TIMER_COUNT_HOB);
         DoOUTR(LOBYTE(count),TIMER_COUNT_LOB);
         DoOUTR((WORD) 0XFF,TIMER_ENABLED);
         break;
      }
   }
   va_end(parameters);
}

//--------------------------------------------------
void DriverSWForDisk(int command,...)
//                                    1               ) disable disk controller
//                                    2,DISKIO *diskIO) read sector
//                                    3,DISKIO *diskIO) write sector
//                                    4,bool *pEnabled) is disk enabled?
//                                    5,DISKIO *diskIO) get PCB from DiskIOInProgress
//--------------------------------------------------
{
   va_list parameters;
   DISKIO *diskIO;
   bool *pEnabled;
   WORD enabled;

   va_start(parameters,command);
   switch ( command )
   {
      case 1:
         DoOUTR((WORD) 0X00,DISK_COMMAND);
         break;
      case 2:
         diskIO = va_arg(parameters,DISKIO *);
         DoOUTR((WORD) 0X0F,DISK_COMMAND);
         DoOUTR(HIBYTE(diskIO->bufferAddress),DISK_BUFFER_HOB);
         DoOUTR(LOBYTE(diskIO->bufferAddress),DISK_BUFFER_LOB);
         DoOUTR(HIBYTE(diskIO->sectorAddress),DISK_SECTOR_HOB);
         DoOUTR(LOBYTE(diskIO->sectorAddress),DISK_SECTOR_LOB);
         DoOUTR(HIBYTE(diskIO->sectorSize),DISK_COUNT_HOB);
         DoOUTR(LOBYTE(diskIO->sectorSize),DISK_COUNT_LOB);

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_DISK_IO )
         {
            fprintf(TRACE,"@%10d(%3d) start     read %d-byte sector #OX%04X into buffer at OX%04X\n",
               S16clock,diskIO->pcb->handle,diskIO->sectorSize,diskIO->sectorAddress,
               diskIO->bufferAddress);
            fflush(TRACE);
         //ENDTRACE-ing

         break;
      case 3:
         diskIO = va_arg(parameters,DISKIO *);
         DoOUTR((WORD) 0XF0,DISK_COMMAND);
         DoOUTR(HIBYTE(diskIO->bufferAddress),DISK_BUFFER_HOB);
         DoOUTR(LOBYTE(diskIO->bufferAddress),DISK_BUFFER_LOB);
         DoOUTR(HIBYTE(diskIO->sectorAddress),DISK_SECTOR_HOB);
         DoOUTR(LOBYTE(diskIO->sectorAddress),DISK_SECTOR_LOB);
         DoOUTR(HIBYTE(diskIO->sectorSize),DISK_COUNT_HOB);
         DoOUTR(LOBYTE(diskIO->sectorSize),DISK_COUNT_LOB);

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_DISK_IO )
            fprintf(TRACE,"@%10d(%3d) start    write %d-byte sector #OX%04X from buffer at OX%04X\n",
               S16clock,diskIO->pcb->handle,diskIO->sectorSize,diskIO->sectorAddress,
               diskIO->bufferAddress);
            fflush(TRACE);
         }
         //ENDTRACE-ing

         break;
      case 4:
         pEnabled = va_arg(parameters,bool *);
         DoINR(&enabled,DISK_COMMAND);
         *pEnabled = !(enabled == 0X00);
         break;
   }
}

#define DISPLAY_MESSAGE_FIELD()                                \
{                                                              \
   int i;                                                      \
                                                               \
   for (i = 0; i <= message->header.length-1; i++)             \
      fprintf(TRACE,"%04X",message->block[i]);                 \
   fprintf(TRACE," |");                                        \
   for (i = 0; i <= message->header.length-1; i++)             \
      if ( isprint((char) message->block[i]) )                 \
         fprintf(TRACE,"%c",(char) message->block[i]);         \
      else                                                     \
         fprintf(TRACE,".");                                   \
   fprintf(TRACE,"| ");                                        \
   fflush(TRACE);                                              \
}

#define DISPLAY_PIPE_BUFFER()                                  \
{                                                              \
   int i;                                                      \
                                                               \
   for (i = 0; i <= count-1; i++)                              \
      fprintf(TRACE,"%04X",buffer[i]);                         \
   fprintf(TRACE," |");                                        \
   for (i = 0; i <= count-1; i++)                              \
      if ( isprint((char) buffer[i]) )                         \
         fprintf(TRACE,"%c",(char) buffer[i]);                 \
      else                                                     \
         fprintf(TRACE,".");                                   \
   fprintf(TRACE,"| ");                                        \
   fflush(TRACE);                                              \
}

//--------------------------------------------------
void S16OS_GoToSVCEntryPoint(WORD SVCRequestNumber)
//--------------------------------------------------
{
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToWaitQNow(void *pcbR,void *pcb);
   bool AddPCBToSuspendedQNow(void *pcbR,void *pcb);
   void *ObjectFromQ(QUEUE *queue,int index);
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void DestructQ(QUEUE *queue);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);
   void S16OS_CPUScheduler();
   void S16OS_Dispatcher();
   void S16OS_TerminateProcess(PCB *pcb);
   void TraceStateOfSystemQueues();

   void DoSVC_DO_NOTHING(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_CURRENT_TIME(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_RANDOM_INTEGER(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_SLEEP_PROCESS(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_DUMP_PROCESS_DATA(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_PROCESS_HANDLE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_PROCESS_NAME(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_PROCESS_PRIORITY(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_SET_PROCESS_PRIORITY(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_SET_PROCESS_ERROR_HANDLER(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_DATA_SEGMENT_UB_PAGE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_STACK_SEGMENT_SIZE_IN_PAGES(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_SET_STACK_SEGMENT_SIZE_IN_PAGES(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DOSVC_SET_PROCESS_SIGNAL_HANDLER(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DOSVC_SEND_SIGNAL_TO_PROCESS(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   
   void DoSVC_CREATE_CHILD_PROCESS(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_CREATE_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_ACTIVE_CHILD_THREAD_COUNT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_SUSPEND_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_RESUME_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_TERMINATE_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_JOIN_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_GET_RESOURCE_TYPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_RESOURCE_HANDLE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_WAIT_FOR_RESOURCE_HANDLE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_RESOURCE_NAME(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_READ_FROM_TERMINAL(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_WRITE_TO_TERMINAL(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_READ_DISK_SECTOR(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_WRITE_DISK_SECTOR(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_CREATE_MEMORY_SEGMENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_DESTROY_MEMORY_SEGMENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_SHARE_MEMORY_SEGMENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_UNSHARE_MEMORY_SEGMENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_MEMORY_SEGMENT_LB_PAGE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_MEMORY_SEGMENT_SIZE_IN_PAGES(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_CREATE_MESSAGEBOX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_DESTROY_MESSAGEBOX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_SEND_MESSAGE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_REQUEST_MESSAGE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_MESSAGE_COUNT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_CREATE_SEMAPHORE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_DESTROY_SEMAPHORE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_WAIT_SEMAPHORE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_SIGNAL_SEMAPHORE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_CREATE_MUTEX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_DESTROY_MUTEX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_LOCK_MUTEX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_UNLOCK_MUTEX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_CREATE_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_DESTROY_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_SIGNAL_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_SIGNALALL_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_WAIT_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_EVENT_QUEUE_SIZE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   void DoSVC_CREATE_PIPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_DESTROY_PIPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_READ_PIPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_WRITE_PIPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);
   void DoSVC_GET_PIPE_SIZE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ);

   PCB *pcb = pcbInRunState;
   bool shouldAddPCBToWaitQ;
   SVC_RETURN_CODE returnCode;

/*
   update run state time statistics for PCB (because the
      process in the run state software-interrupted itself)
*/
   pcb->runStateTime += S16clock;

// save CPU state in PCB of interrupted running process
   pcb->PC = GetPC();
   pcb->SP = GetSP();
   pcb->FB = GetFB();
   for (int n = 0; n <= 15; n++)
      pcb->R[n] = GetRn(n);
   DoSTMMU(pcb->MMURegisters);

// update IO burst count statistic
   pcb->IOBurstCount++;

// CPU burst length, t, and compute tau = alpha*t + (1.0-alpha)*tau
   pcb->t += S16clock;
   pcb->tau = (int) ( alpha*pcb->t + (1.0-alpha)*pcb->tau) ;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_SJF_SCHEDULING )
   {
      fprintf(TRACE,"@%10d(%3d) t = %5d, tau = %5d\n",
         S16clock,pcb->handle,pcb->t,pcb->tau);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   if ( USE_PREEMPTIVE_CPU_SCHEDULER )
   {
   // disable timer
      DriverSWForTimer(1);
   }

// update clock and context switch time/count statistics
   pcb->contextSwitchTime += CONTEXT_SWITCH_TIME/2;
   S16clock = S16clock+CONTEXT_SWITCH_TIME/2;
   pcb->contextSwitchCount++;
   contextSwitchTime += CONTEXT_SWITCH_TIME/2;

/*
   When a child thread makes a system service request and its "terminate process" PCB field
      has been set to true by its parent, override the child thread service request treating
      it as a SVC_TERMINATE_PROCESS instead.

   ****************************************************************************
   This is the "safe point" for the SVC_TERMINATE_CHILD_THREAD system service request.
   ****************************************************************************
*/
   if ( pcb->terminateProcess )
   {
      SVCRequestNumber = SVC_TERMINATE_PROCESS;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) Child thread terminated by parent process (%d)\n",
            S16clock,pcb->handle,((PCB *) pcb->parentPCB)->handle);
         fflush(TRACE);
      }
   }

/* 
   ****************************************************************************
   This is the "safe point" for the SVC_SEND_SIGNAL_TO_PROCESS system service request.
   ****************************************************************************

   Search signals queue for a SIGNAL record whose signaledProcessHandle matches the running 
      process handle. When found, remove the SIGNAL record from signals queue, then
      1. increment running process signalsHandledCount
      2. push running process PC on the running process run-time stack
      3. push signal->semderProcessHandle on the running process run-time stack
      4. push signal->signal on the running process run-time stack
      5. set running process PC to running process signalHandler
      6. deallocate signal
*/
         int index = 1;
         bool found = false;
         SIGNAL *signal;
         while ( !found && (index <= signalsQ.size) )
         {
         // "peek at" signals queue index-th SIGNAL record
            signal = (SIGNAL *) ObjectFromQ(&signalsQ,index);
            if ( pcb->handle == signal->signaledProcessHandle )
               found = true;
            else
               index++;
         }
         if ( found )
         {
            RemoveObjectFromQ(&signalsQ,index,(void **) &signal);
         // 1.
            pcb->signalsHandledCount++;
         // 2.
            WriteDataLogicalMainMemory(pcb->MMURegisters,pcb->SP,HIBYTE(pcb->PC));
            pcb->SP++;
            WriteDataLogicalMainMemory(pcb->MMURegisters,pcb->SP,LOBYTE(pcb->PC));
            pcb->SP++;
         // 3.
            WriteDataLogicalMainMemory(pcb->MMURegisters,pcb->SP,HIBYTE(signal->senderProcessHandle));
            pcb->SP++;
            WriteDataLogicalMainMemory(pcb->MMURegisters,pcb->SP,LOBYTE(signal->senderProcessHandle));
            pcb->SP++;
         // 4.
            WriteDataLogicalMainMemory(pcb->MMURegisters,pcb->SP,HIBYTE(signal->signal));
            pcb->SP++;
            WriteDataLogicalMainMemory(pcb->MMURegisters,pcb->SP,LOBYTE(signal->signal));
            pcb->SP++;
         // 5.
            pcb->PC = pcb->signalHandler;
         // 6.
            free(signal);
         }

// handle software interrupt
   switch ( SVCRequestNumber )
   {
//********************
// miscellaneous
//********************
      case SVC_DO_NOTHING:
         DoSVC_DO_NOTHING(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_CURRENT_TIME:
         DoSVC_GET_CURRENT_TIME(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_RANDOM_INTEGER:
         DoSVC_GET_RANDOM_INTEGER(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;

//********************
// process management
//********************
      case SVC_ABORT_PROCESS:
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains abort code to specify 
      application-specific reason for abnormal termination
   OUT parameters: (none)
*/
         
      case SVC_TERMINATE_PROCESS:
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: (none)
   OUT parameters: (none)
*/

//*****************************************************************************
//    *NOTE* The case-s SVC_ABORT_PROCESS and SVC_TERMINATE_PROCESS "share" a significant
//       amount of common code, using IF/ELSE-statements to distinguish between the two
//       system service requests as necessary.
//*****************************************************************************

      // "flush" terminal OUT buffer if not empty when job terminates
         if ( strlen(pcb->OUT) != 0 )
         {
            printf(       "(%3d)> FLUSH %s\n",pcb->handle,pcb->OUT);

            //TRACE-ing
            if ( ENABLE_TRACING && TRACE_TERMINAL_IO )
            {
               fprintf(TRACE,"(%3d)> FLUSH %s\n",pcb->handle,pcb->OUT);
               fflush(TRACE);
            }
            //ENDTRACE-ing

            pcb->OUT[0] = '\0';
         }

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            if     ( SVCRequestNumber == SVC_TERMINATE_PROCESS )
            {
               fprintf(TRACE,"@%10d(%3d) terminated\n",S16clock,pcb->handle);
               fflush(TRACE);
            }
            else// ( SVCRequestNumber == SVC_ABORT_PROCESS )
            {
               fprintf(TRACE,"@%10d(%3d) aborted (abort code = %d)\n",S16clock,pcb->handle,pcb->R[15]);
               fflush(TRACE);
               printf(       "(%3d) aborted (abort code = %d)\n",pcb->handle,pcb->R[15]);
            }
         }
         //ENDTRACE-ing

      // compute turnaround time = CPU S16clock at job termination - CPU S16clock at job creation
         pcb->turnAroundTime += S16clock;

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_STATISTICS )
         {
         /*
            display job statistics to TRACE file using the following format

                     1         2         3         4         5         6         7         8         9
            123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
            Turn Around Time      TTTTTTTTTT ticks
            Run   State Time      TTTTTTTTTT ticks XXXXXXXXX count
            Ready State Time      TTTTTTTTTT ticks XXXXXXXXX count
            Wait  State Time      TTTTTTTTTT ticks XXXXXXXXX count
            Join  State Time      TTTTTTTTTT ticks XXXXXXXXX count
            Suspended State Time  TTTTTTTTTT ticks XXXXXXXXX count
            Sleep State Time      TTTTTTTTTT ticks XXXXXXXXX count
            Semaphore Wait Time   TTTTTTTTTT ticks XXXXXXXXX count
            Mutex     Wait Time   TTTTTTTTTT ticks XXXXXXXXX count
            Message Wait Time     TTTTTTTTTT ticks XXXXXXXXX count
            Event Wait Time       TTTTTTTTTT ticks XXXXXXXXX count
            Disk IO Wait Time     TTTTTTTTTT ticks XXXXXXXXX count
            Resources Wait Time   TTTTTTTTTT ticks XXXXXXXXX count
            Context Switch Time   TTTTTTTTTT ticks XXXXXXXXX count
            H/W Interrupts         XXXXXXXXX
            CPU Bursts             XXXXXXXXX XXX.X% shorter than XXXXX clock-tick time quantum
            IO Bursts              XXXXXXXXX
            Signals                XXXXXXXXX sent, XXXXXXXXX ignored, XXXXXXXXX handled 
            Tau                   TTTTTTTTTT ticks XXX.X% of XXXXX clock-tick time quantum
            Resources                   XXXX allocated, XXXX deallocated *** RESOURCE LEAK ***
         */
            fprintf(TRACE,"Turn Around Time      %10d ticks\n",pcb->turnAroundTime);
            fprintf(TRACE,"Run   State Time      %10d ticks %9d count\n",
               pcb->runStateTime,pcb->runStateCount);
            fprintf(TRACE,"Ready State Time      %10d ticks %9d count\n",
               pcb->readyStateTime,pcb->readyStateCount);
            fprintf(TRACE,"Wait  State Time      %10d ticks %9d count\n",
               pcb->waitStateTime,pcb->waitStateCount);
            fprintf(TRACE,"Join  State Time      %10d ticks %9d count\n",
               pcb->joinStateTime,pcb->joinStateCount);
            fprintf(TRACE,"Suspended State Time  %10d ticks %9d count\n",
               pcb->suspendedStateTime,pcb->suspendedStateCount);
            fprintf(TRACE,"Sleep State Time      %10d ticks %9d count\n",
               pcb->sleepStateTime,pcb->sleepStateCount);
            fprintf(TRACE,"Semaphore Wait Time   %10d ticks %9d count\n",
               pcb->semaphoreWaitTime,pcb->semaphoreWaitCount);
            fprintf(TRACE,"Mutex     Wait Time   %10d ticks %9d count\n",
               pcb->mutexWaitTime,pcb->mutexWaitCount);
            fprintf(TRACE,"Message Wait Time     %10d ticks %9d count\n",
               pcb->messageWaitTime,pcb->messageWaitCount);
            fprintf(TRACE,"Event Wait Time       %10d ticks %9d count\n",
               pcb->eventWaitTime,pcb->eventWaitCount);
            fprintf(TRACE,"Disk IO Wait Time     %10d ticks %9d count\n",
               pcb->diskIOWaitTime,pcb->diskIOWaitCount);
            fprintf(TRACE,"Resources Wait Time   %10d ticks %9d count\n",
               pcb->resourcesWaitTime,pcb->resourcesWaitCount);
            fprintf(TRACE,"Context Switch Time   %10d ticks %9d count\n",
               pcb->contextSwitchTime,pcb->contextSwitchCount);
            fprintf(TRACE,"H/W Interrupts         %9d\n",pcb->HWInterruptCount);
         
            if ( USE_PREEMPTIVE_CPU_SCHEDULER )
            {
               fprintf(TRACE,"CPU Bursts             %9d %5.1f%% shorter than %5d clock-tick time quantum\n",
                  pcb->CPUBurstCount,((double) pcb->IOBurstCount/pcb->CPUBurstCount)*100,TIME_QUANTUM);
               fprintf(TRACE,"Tau                   %10d ticks %5.1f%% of %5d clock-tick time quantum\n",
                  pcb->tau,((double) pcb->tau/TIME_QUANTUM)*100,TIME_QUANTUM);
            }
            else
            {
               fprintf(TRACE,"CPU Bursts             %9d\n",pcb->CPUBurstCount);
               fprintf(TRACE,"Tau                   %10d ticks\n",pcb->tau);
            }
         
            fprintf(TRACE,"IO Bursts              %9d\n",pcb->IOBurstCount);

            fprintf(TRACE,"Signals                %9d sent, %9d ignored, %9d handled\n",
               pcb->signalsSentCount,pcb->signalsIgnoredCount,pcb->signalsHandledCount);
        
            fprintf(TRACE,"Resources                   %4d allocated, %4d deallocated",
               pcb->allocatedResourcesCount,pcb->deallocatedResourcesCount);
            if ( pcb->allocatedResourcesCount != pcb->deallocatedResourcesCount )
               fprintf(TRACE," *** RESOURCE LEAK ***");
            fprintf(TRACE,"\n\n");
         }
         //ENDTRACE-ing

         if ( pcb->allocatedResourcesCount != pcb->deallocatedResourcesCount )
            ProcessWarningOrError(S16OSWARNING,"Terminating process has a resource leak");
         
         if ( pcb->childThreadActiveCount != 0 )
            ProcessWarningOrError(S16OSWARNING,"Terminating process still has active child threads");

         S16OS_TerminateProcess(pcb);

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"Jobs still alive       %9d",numberOfJobsCreated-numberOfJobsTerminated);
            fprintf(TRACE,"\n");
         }
         //ENDTRACE-ing

      // CPU *MUST* be halted when all created jobs have terminated
         if ( numberOfJobsCreated == numberOfJobsTerminated )
         {
         /*
            update and display throughput and CPU utilization statistics to TRACE file
Number of Jobs      XXXXX
Throughput        X.XXXXX jobs/clock-tick
CPU Utilization    XXX.XX%
IDLE-ing           XXX.XX%
Context switching  XXX.XX%
         */
            throughputTime += S16clock;

            //TRACE-ing
            if ( ENABLE_TRACING && TRACE_STATISTICS )
            {
               fprintf(TRACE,"\nNumber of jobs       %5d\n",numberOfJobsCreated);
               fprintf(TRACE,"Throughput         %7.5f jobs/clock-tick\n",
                  (double) numberOfJobsCreated/throughputTime);
               fprintf(TRACE,"CPU Utilization    %6.2f%%\n",
                  ((double) HWInstructionCount/throughputTime)*100);
               fprintf(TRACE,"IDLE-ing           %6.2f%%\n",
                  ((double) IDLETime/throughputTime)*100);
               fprintf(TRACE,"Context switching  %6.2f%%\n",
                  ((double) contextSwitchTime/throughputTime)*100);
               fflush(TRACE);
            }
            //ENDTRACE-ing

         // trace all system queues before destruction
            TraceStateOfSystemQueues();
            
         // destruct queues (except queues used by SEMAPHORE-s, MUTEX-s, MESSAGEBOX-s, and EVENT-s)
            DestructQ(&jobQ);
            DestructQ(&readyQ);
            DestructQ(&waitQ);
            DestructQ(&joinQ);
            DestructQ(&signalsQ);
            DestructQ(&sleepQ);
            DestructQ(&suspendedQ);
            DestructQ(&resourcesWaitQ);
            DestructQ(&diskIOQ);

         // halt CPU
            DoSTOP();
         /*
            The "return" below is *ABSOLUTELY* necessary so that scheduling
            and dispatching is bypassed when all jobs have terminated.
         */
            return;
         }
         break;
      case SVC_SLEEP_PROCESS:
         DoSVC_SLEEP_PROCESS(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_DUMP_PROCESS_DATA:
         DoSVC_DUMP_PROCESS_DATA(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_PROCESS_HANDLE:
         DoSVC_GET_PROCESS_HANDLE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_PROCESS_NAME:
         DoSVC_GET_PROCESS_NAME(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_PROCESS_PRIORITY:
         DoSVC_GET_PROCESS_PRIORITY(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_SET_PROCESS_PRIORITY:
         DoSVC_SET_PROCESS_PRIORITY(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_SET_PROCESS_ERROR_HANDLER:
         DoSVC_SET_PROCESS_ERROR_HANDLER(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_DATA_SEGMENT_UB_PAGE:
         DoSVC_GET_DATA_SEGMENT_UB_PAGE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_STACK_SEGMENT_SIZE_IN_PAGES:
         DoSVC_GET_STACK_SEGMENT_SIZE_IN_PAGES(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_SET_STACK_SEGMENT_SIZE_IN_PAGES:
         DoSVC_SET_STACK_SEGMENT_SIZE_IN_PAGES(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_CREATE_CHILD_PROCESS:
         DoSVC_CREATE_CHILD_PROCESS(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_CREATE_CHILD_THREAD:
         DoSVC_CREATE_CHILD_THREAD(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_ACTIVE_CHILD_THREAD_COUNT:
         DoSVC_GET_ACTIVE_CHILD_THREAD_COUNT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;

      case SVC_SET_PROCESS_SIGNAL_HANDLER:
         DOSVC_SET_PROCESS_SIGNAL_HANDLER(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_SEND_SIGNAL_TO_PROCESS:
         DOSVC_SEND_SIGNAL_TO_PROCESS(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;

      case SVC_SUSPEND_CHILD_THREAD:
         DoSVC_SUSPEND_CHILD_THREAD(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_RESUME_CHILD_THREAD:
         DoSVC_RESUME_CHILD_THREAD(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_TERMINATE_CHILD_THREAD:
         DoSVC_TERMINATE_CHILD_THREAD(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_JOIN_CHILD_THREAD:
         DoSVC_JOIN_CHILD_THREAD(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// resource management
//********************
      case SVC_GET_RESOURCE_TYPE:
         DoSVC_GET_RESOURCE_TYPE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_RESOURCE_HANDLE:
         DoSVC_GET_RESOURCE_HANDLE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_WAIT_FOR_RESOURCE_HANDLE:
         DoSVC_WAIT_FOR_RESOURCE_HANDLE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_RESOURCE_NAME:
         DoSVC_GET_RESOURCE_NAME(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// terminal IO
//********************
      case SVC_READ_FROM_TERMINAL:
         DoSVC_READ_FROM_TERMINAL(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_WRITE_TO_TERMINAL:
         DoSVC_WRITE_TO_TERMINAL(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// disk IO
//********************
      case SVC_READ_DISK_SECTOR:
         DoSVC_READ_DISK_SECTOR(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_WRITE_DISK_SECTOR:
         DoSVC_WRITE_DISK_SECTOR(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// memory segment
//********************
      case SVC_CREATE_MEMORY_SEGMENT:
         DoSVC_CREATE_MEMORY_SEGMENT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_DESTROY_MEMORY_SEGMENT:
         DoSVC_DESTROY_MEMORY_SEGMENT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_SHARE_MEMORY_SEGMENT:
         DoSVC_SHARE_MEMORY_SEGMENT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_UNSHARE_MEMORY_SEGMENT:
         DoSVC_UNSHARE_MEMORY_SEGMENT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_MEMORY_SEGMENT_LB_PAGE:
         DoSVC_GET_MEMORY_SEGMENT_LB_PAGE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_MEMORY_SEGMENT_SIZE_IN_PAGES:
         DoSVC_GET_MEMORY_SEGMENT_SIZE_IN_PAGES(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// message boxes
//********************
      case SVC_CREATE_MESSAGEBOX:
         DoSVC_CREATE_MESSAGEBOX(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_DESTROY_MESSAGEBOX:
         DoSVC_DESTROY_MESSAGEBOX(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_SEND_MESSAGE:
         DoSVC_SEND_MESSAGE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_REQUEST_MESSAGE:
         DoSVC_REQUEST_MESSAGE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_MESSAGE_COUNT:
         DoSVC_GET_MESSAGE_COUNT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// semaphores
//********************
      case SVC_CREATE_SEMAPHORE:
         DoSVC_CREATE_SEMAPHORE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_DESTROY_SEMAPHORE:
         DoSVC_DESTROY_SEMAPHORE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_WAIT_SEMAPHORE:
         DoSVC_WAIT_SEMAPHORE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_SIGNAL_SEMAPHORE:
         DoSVC_SIGNAL_SEMAPHORE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// mutexes
//********************
      case SVC_CREATE_MUTEX:
         DoSVC_CREATE_MUTEX(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_DESTROY_MUTEX:
         DoSVC_DESTROY_MUTEX(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_LOCK_MUTEX:
         DoSVC_LOCK_MUTEX(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_UNLOCK_MUTEX:
         DoSVC_UNLOCK_MUTEX(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// events
//********************
      case SVC_CREATE_EVENT:
         DoSVC_CREATE_EVENT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_DESTROY_EVENT:
         DoSVC_DESTROY_EVENT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_SIGNAL_EVENT:
         DoSVC_SIGNAL_EVENT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_SIGNALALL_EVENT:
         DoSVC_SIGNALALL_EVENT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_WAIT_EVENT:
         DoSVC_WAIT_EVENT(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_EVENT_QUEUE_SIZE:
         DoSVC_GET_EVENT_QUEUE_SIZE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// pipes
//********************
      case SVC_CREATE_PIPE:
         DoSVC_CREATE_PIPE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_DESTROY_PIPE:
         DoSVC_DESTROY_PIPE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_READ_PIPE:
         DoSVC_READ_PIPE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_WRITE_PIPE:
         DoSVC_WRITE_PIPE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
      case SVC_GET_PIPE_SIZE:
         DoSVC_GET_PIPE_SIZE(pcb,&returnCode,&shouldAddPCBToWaitQ);
         break;
//********************
// unknown service request number
//********************
      default:
         ProcessWarningOrError(S16OSWARNING,"Unknown service request number");
         returnCode = SVC_ERROR001;
         shouldAddPCBToWaitQ = true;
         break;
   }

   if ( (SVCRequestNumber != SVC_TERMINATE_PROCESS) && (SVCRequestNumber != SVC_ABORT_PROCESS) )
   {
   // update R0 in PCB with service request return code
      pcb->R[0] = returnCode;

   /*
      When run-time error occurred and SVC error handling is enabled (that is, PCB field SVCErrorHandler 
         is not NULL) push PC onto stack, and set PC to SVC error handler address; otherwise
         SVC error handling is not enabled (that is, PCB field SVCErrorHandler is NULL) display
         warning message.
   */
      if ( returnCode > SVC_OK )
      {
         if ( pcb->SVCErrorHandler == NULLWORD )
         {
            char information[SOURCE_LINE_LENGTH+1];
      
            sprintf(information,"Run-time error SVC_ERROR%03d occurred, SVC error handling not enabled",returnCode);
            ProcessWarningOrError(S16OSWARNING,information);
         }
         else
         {
            WriteDataLogicalMainMemory(pcb->MMURegisters,pcb->SP,HIBYTE(pcb->PC));
            pcb->SP++;
            WriteDataLogicalMainMemory(pcb->MMURegisters,pcb->SP,LOBYTE(pcb->PC));
            pcb->SP++;
            pcb->PC = pcb->SVCErrorHandler;
         }
      }
   /*
      When running PCB should be added to the wait queue, add PCB to the
         wait queue in priority order based on computed "takeOffWaitQTime", and
         update wait state time/count statistics for PCB.
   */
      if ( shouldAddPCBToWaitQ )
      {
/*
   ****************************************************************************
   This is the "safe point" for the SVC_SUSPEND_CHILD_THREAD system service request.
   ****************************************************************************

   When a child thread makes a system service request and its "suspend process" PCB field
      has peviously been set to true by its parent, add the child thread to the suspended queue
      instead of the wait queue.
*/
         if ( pcb->suspendProcess )
         {
            AddObjectToQ(&suspendedQ,pcb,AddPCBToSuspendedQNow);
            pcb->suspendedStateTime -= S16clock;
            pcb->suspendedStateCount++;
      
            //TRACE-ing
            if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
            {
               fprintf(TRACE,"@%10d(%3d) running process added to suspended queue\n",
                  S16clock,pcb->handle);
               fflush(TRACE);
            }
            //ENDTRACE-ing
         }
         else
         {
            pcb->takeOffWaitQTime = S16clock + SVCWaitTime(SVCRequestNumber);
            AddObjectToQ(&waitQ,pcb,AddPCBToWaitQNow);
            pcb->waitStateTime -= S16clock;
            pcb->waitStateCount++;
         }
      }
   }

// when ready queue is empty, set CPU state to IDLE
   if ( readyQ.size == 0 )
      SetCPUState(IDLE);
// otherwise ready queue is not empty, schedule and dispatch next job in ready queue
   else
   {
      S16OS_CPUScheduler();
      S16OS_Dispatcher();
   }
}

//=============================================================================
// S16OS CPU short-term scheduler and dispatcher
//=============================================================================
//--------------------------------------------------
void S16OS_CPUScheduler()
//--------------------------------------------------
{
   void *ObjectFromQ(QUEUE *queue,int index);
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void TraceQ(QUEUE *queue);

// take PCB off head of ready queue, make it the running PCB and set CPU state to RUN
   RemoveObjectFromQ(&readyQ,1,(void **) &pcbInRunState);
   SetCPUState(RUN);

// update ready state time/count statistics for PCB
   pcbInRunState->readyStateTime += S16clock;

// update clock and context switch time statistics
   pcbInRunState->contextSwitchTime += CONTEXT_SWITCH_TIME/2;
   S16clock = S16clock+CONTEXT_SWITCH_TIME/2;
   contextSwitchTime += CONTEXT_SWITCH_TIME/2;

// update run state time/count statistics for PCB
   pcbInRunState->runStateTime -= S16clock;
   pcbInRunState->runStateCount++;

   // TRACE-ing
   if ( ENABLE_TRACING && TRACE_SCHEDULER )
   {
      fprintf(TRACE,"@%10d(%3d) scheduled",S16clock,pcbInRunState->handle);
      TraceQ(&readyQ); fprintf(TRACE,"\n");
      fflush(TRACE);
   }
   // ENDTRACE-ing

}

//--------------------------------------------------
void S16OS_Dispatcher()
//--------------------------------------------------
{
   void DriverSWForTimer(int command,...);

// set CPU state with contents of the running process PCB
   SetPC(pcbInRunState->PC);
   SetSP(pcbInRunState->SP);
   SetFB(pcbInRunState->FB);
   for (int n = 0; n <= 15; n++)
      SetRn(n,pcbInRunState->R[n]);
   DoLDMMU(pcbInRunState->MMURegisters);

   if ( USE_PREEMPTIVE_CPU_SCHEDULER )
   {
   // set timer count to TIME_QUANTUM
      DriverSWForTimer(2,TIME_QUANTUM);
   }

// update CPU burst count statistic
   pcbInRunState->CPUBurstCount++;

// update CPU burst length, t, used only for SJF scheduling
   pcbInRunState->t = -S16clock;

   // TRACE-ing
   if ( ENABLE_TRACING && TRACE_DISPATCHER )
   {
      fprintf(TRACE,"@%10d(%3d) dispatched\n",S16clock,pcbInRunState->handle);
      fflush(TRACE);
   }
   // ENDTRACE-ing

}

//=============================================================================
// S16OS system service requests
//=============================================================================
//--------------------------------------------------
void DoSVC_DO_NOTHING(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: (none)
   OUT parameters: R0 always contains SVC_OK
*/

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MISCELLANEOUS_SVC )
   {
      fprintf(TRACE,"@%10d(%3d) ***DO NOTHING***\n",S16clock,pcb->handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_CURRENT_TIME(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: (none)
   OUT parameters: R15 contains the current time (the current value of S16clock);
      R0 always contains SVC_OK
*/
   pcb->R[15] = S16clock;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MISCELLANEOUS_SVC )
   {
      fprintf(TRACE,"@%10d(%3d) current time = %d S16clock ticks\n",
         S16clock,pcb->handle,S16clock);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_RANDOM_INTEGER(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the upper-bound of interval, [0,R15 ], used to define the set of
      integers from which a uniformly-distributed random integer is chosen
   OUT parameters: R15 contains the random integer; R0 contains SVC_ERROR015 when the upper-bound is 
      out of range, otherwise SVC_OK
*/
   int upperbound = pcb->R[15];

   if ( upperbound < 0 )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MISCELLANEOUS_SVC )
      {
         fprintf(TRACE,"@%10d(%3d) get random integer failure, upper-bound = %d out-of-range\n",
            S16clock,pcb->handle,upperbound);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR015;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {
      pcb->R[15] = RandomInt(0,upperbound);

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MISCELLANEOUS_SVC )
      {
         fprintf(TRACE,"@%10d(%3d) get random integer = %d\n",
            S16clock,pcb->handle,pcb->R[15]);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
// DoSVC_TERMINATE_PROCESS() is handled in S16OS_GoToSVCEntryPoint()
//--------------------------------------------------

//--------------------------------------------------
// DoSVC_ABORT_PROCESS() is handled in S16OS_GoToSVCEntryPoint()
//--------------------------------------------------

//--------------------------------------------------
void DoSVC_SLEEP_PROCESS(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains number of S16clock ticks the job must sleep
   OUT parameters: R0 always contains SVC_OK
*/
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToSleepQNow(void *pcbR,void *pcb);

   pcb->takeOffSleepQTime = S16clock + pcb->R[15];
   AddObjectToQ(&sleepQ,pcb,AddPCBToSleepQNow);
   pcb->sleepStateTime -= S16clock;
   pcb->sleepStateCount++;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) running process added to sleep queue for %d S16clock ticks\n",
         S16clock,pcb->handle,pcb->R[15]);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = false;
}

//--------------------------------------------------
void DoSVC_DUMP_PROCESS_DATA(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the 16-bit binary value 000000000000SDR that specifies 
      which data is dumped to the trace file as follows
         * R = 1 means dump general-purpose registers R0-R15
         * D = 1 dump data-segment contents
         * S = 1 dump current stack contents
   OUT parameters: R0 always contains SVC_OK
*/
   void DumpProcessData(FILE *OUT,PCB *pcb,bool dumpRegisters,bool dumpData,bool dumpStack);

   int R = pcb->R[15] & 0X0001;
   int D = pcb->R[15] & 0X0002;
   int S = pcb->R[15] & 0X0004;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) dump running process data requested\n\n",S16clock,pcb->handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   DumpProcessData(TRACE,pcb,(R != 0),(D != 0),(S != 0));
   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_PROCESS_HANDLE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: (none)
   OUT parameters: R15 contains the running process JOBS resource handle;
      R0 always contains SVC_OK
*/
   pcb->R[15] = pcb->handle;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) get running process JOBS resource handle = %d\n",
         S16clock,pcb->handle,pcb->handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_PROCESS_NAME(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of buffer in running process where process name should be written
   OUT parameters: R0 always contains SVC_OK
*/
   BYTE HOB,LOB;
   HANDLE handle;
   WORD LA;

   handle = pcb->handle;
   LA = (WORD) pcb->R[15];

   for (int i = 0; i <= (int) strlen(resources[handle].name); i++)
   {
      WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+2*i+0),(BYTE)           0X00);
      WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+2*i+1),(BYTE) resources[handle].name[i]);
   }

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) get name for running process = \"%s\"\n",
         S16clock,pcb->handle,resources[handle].name);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_PROCESS_PRIORITY(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: (none)
   OUT parameters: R15 contains the running process priority;
      R0 always contains SVC_OK
*/
   pcb->R[15] = pcb->priority;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) get running process priority = %d\n",
         S16clock,pcb->handle,pcb->priority);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_SET_PROCESS_PRIORITY(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the new priority
   OUT parameters: R0 contains SVC_OK when the new priority is in range, otherwise SVC_ERROR005
*/
   int newPriority = pcb->R[15];

   if ( (MAXIMUM_PRIORITY <= newPriority) && (newPriority <= MINIMUM_PRIORITY) )
   {
      pcb->priority = newPriority;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) set running process priority to %d\n",
            S16clock,pcb->handle,pcb->priority);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) set running process priority failure, priority = %d out-of-range\n",
            S16clock,pcb->handle,newPriority);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR005;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
void DoSVC_SET_PROCESS_ERROR_HANDLER(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains code-segment logical address-of SVC error handler entry point
   OUT parameters: R0 contains SVC_OK when new address is in range, otherwise SVC_ERROR006
*/
   if ( /*(0 <= pcb->R[15]) && */ (pcb->R[15] < pcb->CSSize) )
   {
      pcb->SVCErrorHandler = pcb->R[15];

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) set running process SVC error handler, address = %04X\n",
            S16clock,pcb->handle,pcb->SVCErrorHandler);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) set running process SVC error handler failure, address = %04X out-of-range\n",
            S16clock,pcb->handle,pcb->R[15]);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR006;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
void DoSVC_GET_DATA_SEGMENT_UB_PAGE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: (none)
   OUT parameters: R15 contains the running process data-segment upper-bound page number
      R0 always contains SVC_OK
*/
   pcb->R[15] = PAGE_NUMBER(pcb->DSBase+pcb->DSSize-1);
   
   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) get running process data-segment upper-bound page number = %d\n",
         S16clock,pcb->handle,(int) pcb->R[15]);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_STACK_SEGMENT_SIZE_IN_PAGES(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: (none)
   OUT parameters: R15 contains the running process stack-segment size-in-pages
      R0 always contains SVC_OK
*/
   pcb->R[15] = SIZE_IN_PAGES(pcb->SSSize);
   
   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) get running process stack-segment size-in-pages = %d\n",
         S16clock,pcb->handle,(int) pcb->R[15]);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_SET_STACK_SEGMENT_SIZE_IN_PAGES(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the running process stack-segment new size-in-pages
   OUT parameters: R0 contains SVC_ERROR018 when stack is not empty; R0 contains
      SVC_ERROR019 when size-in-pages is out-of-range; R0 contains SVC_ERROR020 when
      one or more pages in the implied page-range are not free; 
      otherwise R0 contains SVC_OK
*/
   void S16OS_AllocateMemoryPage(const PCB *pcb,WORD *physicalPage,const char situation[]);
   void S16OS_DeallocateMemoryPage(const PCB *pcb,WORD physicalPage,const char situation[]);

   WORD newSSSize = pcb->R[15]*MEMORY_PAGE_SIZE_IN_BYTES;
   WORD newSSBase = PAGE_NUMBER(LOGICAL_ADDRESS_SPACE_IN_BYTES-newSSSize)*MEMORY_PAGE_SIZE_IN_BYTES;

   if ( pcb->SSBase != pcb->SP )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) set running process stack-segment size-in-pages failure, stack must be empty\n",
            S16clock,pcb->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR018;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   if ( newSSBase < PAGE_NUMBER(pcb->DSBase+pcb->DSSize-1)*MEMORY_PAGE_SIZE_IN_BYTES )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) set running process stack-segment size-in-pages failure, size-in-pages = %d out-of-range\n",
            S16clock,pcb->handle,pcb->R[15]);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR019;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   if      ( newSSSize < pcb->SSSize ) // stack-segment is smaller, deallocate stack-segment pages no longer needed
   {
      for (int page = PAGE_NUMBER(pcb->SSBase); page <= PAGE_NUMBER(newSSBase)-1; page++)
      {
         int physicalPage;

         physicalPage = pcb->MMURegisters[page] & 0X01FF;
         S16OS_DeallocateMemoryPage(pcb,physicalPage,"set stack-segment size");
         pcb->MMURegisters[page] = 0X0000;
      }
   }
   else if ( newSSSize > pcb->SSSize ) // stack-segment is bigger, allocate new stack-segment pages
   {
      bool areAllNewPagesFree = true;
      
      for (int page = PAGE_NUMBER(newSSBase); page <= PAGE_NUMBER(pcb->SSBase)-1; page++)
         areAllNewPagesFree = areAllNewPagesFree && (((pcb->MMURegisters[page] & 0X4000) >> 14) == 0);   // V = 0?
      if ( !areAllNewPagesFree )
      {
         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"@%10d(%3d) set running process stack-segment size-in-pages failure, page range = %d-%d not free\n",
               S16clock,pcb->handle,pcb->R[15],PAGE_NUMBER(newSSBase),(PAGE_NUMBER(pcb->SSBase)-1));
            fflush(TRACE);
         }
         //ENDTRACE-ing

         *returnCode = SVC_ERROR020;
         *shouldAddPCBToWaitQ = true;
         return;
      }

   // stack-segment is M=1, V=1, E=0, W=1, R=1, PPPPPPPPPPP=11-bit physicalPage
      for (int page = PAGE_NUMBER(newSSBase); page <= PAGE_NUMBER(pcb->SSBase)-1; page++)
      {
         WORD physicalPage;

         S16OS_AllocateMemoryPage(pcb,&physicalPage,"set stack-segment size");
         pcb->MMURegisters[page] = 0XD800 | physicalPage;
      }
   }
   else// if ( newSSSize == pcb->SSSize ) // no change in size or base of stack-segment
      ; // do nothing

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) set running process stack-segment size-in-pages new size = 0X%04X (%d pages), stack-segment new base = 0X%04X\n",
         S16clock,pcb->handle,(int) newSSSize,(int) SIZE_IN_PAGES(newSSSize),(int) newSSBase);
      fflush(TRACE);
   }
  //ENDTRACE-ing

   pcb->SSBase = newSSBase;
   pcb->SSSize = newSSSize;
   pcb->SP = pcb->SSBase;
   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DOSVC_SET_PROCESS_SIGNAL_HANDLER(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains code-segment logical address-of signal handler entry point
   OUT parameters: R0 contains SVC_OK when new address is in range, otherwise SVC_ERROR024
*/
   if ( /*(0 <= pcb->R[15]) && */ (pcb->R[15] < pcb->CSSize) )
   {
      pcb->signalHandler = pcb->R[15];

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) set running process signal handler, address = %04X\n",
            S16clock,pcb->handle,pcb->signalHandler);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) set running process signal handler failure, address = %04X out-of-range\n",
            S16clock,pcb->handle,pcb->R[15]);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR024;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
void DOSVC_SEND_SIGNAL_TO_PROCESS(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
         word   0: HOB/LOB of the handle of signaled process
         word   1: HOB/LOB of the signal
   OUT parameters: R0 contains SVC_ERROR012 when the handle of the signaled process 
      is invalid; SVC_ERROR023 when the signal is ignored because signaled process
      signal handler is disabled; otherwise SVC_OK
/*
   ensure handle is valid, specifies an existing memory-segment, and destroy request is
      made by creator/owner of the resource
*/

   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddSignalToSignalsQNow(void *signalR,void *signal);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle;
   WORD signal;
   BYTE HOB,LOB;

// get handle of send-to process from parameter block word 0
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 1),&LOB);
   handle = MAKEWORD(HOB,LOB);
// get signal from parameter block word 1
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 2),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 3),&LOB);
   signal = MAKEWORD(HOB,LOB);

// ensure handle is a valid JOBS handle
   if ( !IsValidHandleForResourceType(handle,JOBS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SIGNALS )
      {
         fprintf(TRACE,"@%10d(%3d) send signal to process (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {
      if ( ((PCB *) resources[handle].object)->signalHandler == NULLWORD )
      {
         ((PCB *) resources[handle].object)->signalsSentCount++;
         ((PCB *) resources[handle].object)->signalsIgnoredCount++;
         
         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_SIGNALS )
         {
            fprintf(TRACE,"@%10d(%3d) send signal %d to process (handle %d) is ignored\n",
               S16clock,pcb->handle,signal,handle);
            fflush(TRACE);
         }
         //ENDTRACE-ing

         *returnCode = SVC_ERROR023;
         *shouldAddPCBToWaitQ = true;
      }
      else
      {
         SIGNAL *p = (SIGNAL *) malloc(sizeof(SIGNAL));

         p->senderProcessHandle = pcb->handle;
         p->signaledProcessHandle = handle;
         p->signal = signal;
         AddObjectToQ(&signalsQ,p,AddSignalToSignalsQNow);
         ((PCB *) resources[handle].object)->signalsSentCount++;

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_SIGNALS )
         {
            fprintf(TRACE,"@%10d(%3d) send signal %d to process (handle %d)\n",
               S16clock,pcb->handle,signal,handle);
            fflush(TRACE);
         }
         //ENDTRACE-ing

         *returnCode = SVC_OK;
         *shouldAddPCBToWaitQ = true;
      }
   }
}

//--------------------------------------------------
void DoSVC_CREATE_CHILD_PROCESS(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the NUL-terminated string
      that is either the first part of the file name of a jobstream file that contains 
      exactly one child job description (and have no syntax errors) or the first part 
      of the file name of an object file. For example, when the child job is stored in 
      the file "Child1.job" or "Child1.object", then the NUL-terminated string must be "Child1".
   OUT parameters: R0 contains SVC_ERROR016 when a jobstream file contains more than one
      job; contains SVC_ERROR017 when jobstream file has a syntax error; otherwise R0 contains SVC_OK
   *Note* A terminal error occurs when the child job object file cannot be opened; an 
      error message is added to the trace file and the S16 simulation aborts.
*/
   void LoadJobs(const char fileName[],int *numberJobs,bool *syntaxError);
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);

   char string[MEMORY_PAGE_SIZE_IN_BYTES+1];
   int numberJobs;
   bool syntaxError;

   GetNULTerminatedString(pcb,pcb->R[15],string);
   LoadJobs(string,&numberJobs,&syntaxError);
   if ( numberJobs != 1 )
   {
      *returnCode = SVC_ERROR016;
      *shouldAddPCBToWaitQ = true;
   } 
   else if ( syntaxError )
   {
      *returnCode = SVC_ERROR017;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {
      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
void DoSVC_CREATE_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
         word   0: HOB/LOB of address-of first instruction of thread (thread PC)
         word   1: name[0] character 1 of n-character, NUL-terminated string
         word   2: name[1]
                .       .
         word n+1: name[n] = NULL
   OUT parameters: R0 contains SVC_ERROR008 when the thread handle could not
      be allocated; otherwise SVC_OK; R15 contains the thread handle
*/
   void S16OS_AllocateMemoryPage(const PCB *pcb,WORD *physicalPage,const char situation[]);
   void ConstructQ(QUEUE *queue,char *name,bool containsPCBs,void (*DisplayQNODEObject)(void *object));
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToReadyQNow(void *pcbR,void *pcb);
   bool AddChildThreadToChildThreadQNow(void *childThreadR,void *childThread);
   void TraceChildThreadQObject(void *object);
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void AllocateResourceHandle(PCB *pcb,const char name[],RESOURCETYPE type,bool *allocated,HANDLE *handle);

   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   BYTE HOB,LOB;
   PCB *pcbThread;
   bool allocated;
   HANDLE handle;

   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+1),&LOB);
   GetNULTerminatedString(pcb,(WORD) (pcb->R[15]+2),name);
   AllocateResourceHandle(pcb,name,JOBS,&allocated,&handle);
   if ( !allocated )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) create child thread failure\n",S16clock,pcb->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR008;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// allocate PCB
   pcbThread = (PCB *) malloc(sizeof(PCB));
   resources[handle].object = pcbThread;

// lightweight process (type = (T)hread) has same priority as parent process
   pcbThread->handle = handle;
   pcbThread->priority = pcb->priority;
   pcbThread->type = 'T';
   pcbThread->parentPCB = (struct PCB *) pcb;
   pcbThread->childThreadActiveCount = 0;

// create empty child thread queue
   char *QName = (char *) malloc(sizeof(char)*(strlen(name)+strlen(" (childThreadQ)")+1));
   sprintf(QName,"%s (childThreadQ)",name);
   ConstructQ(&pcbThread->childThreadQ,QName,false,TraceChildThreadQObject);
   free(QName);

   pcbThread->takeOffJobQTime = S16clock;
   pcbThread->terminateProcess = false;
   pcbThread->suspendProcess = false;

// add child thread node to end of parent process child thread queue
   CHILDTHREAD *p = (CHILDTHREAD *) malloc(sizeof(CHILDTHREAD));
   p->handle = handle;
   p->isActive = true;
   AddObjectToQ(&pcb->childThreadQ,p,AddChildThreadToChildThreadQNow);
   pcb->childThreadActiveCount++;
   
//*****************************************************************************
// *QUESTION* Should child threads use the parent SVC error handler and signal handler by default?!
//*****************************************************************************
   pcbThread->SVCErrorHandler = NULLWORD;
   pcbThread->signalHandler = NULLWORD;

/*
   initialize PCB register fields with a copy of current value
      of parent process Rns (PC comes from parameter block, SP is
      set to parent process SSBase to indicate empty stack, and
      FB must be set by running code of child thread as appropriate)
*/
   pcbThread->PC = MAKEWORD(HOB,LOB);
   pcbThread->SP = pcb->SSBase;
   pcbThread->FB = 0X0000;
   for (int n = 0; n <= 15; n++)
      pcbThread->R[n] = pcb->R[n];

/*
   use same code-segment and data-segment memory pages as parent process (including
      memory-segment pages allocated by the parent process
*/
   pcbThread->CSSize = pcb->CSSize; pcbThread->CSBase = pcb->CSBase;
   pcbThread->DSSize = pcb->DSSize; pcbThread->DSBase = pcb->DSBase;
   for (int page = 0; page <= SIZE_IN_PAGES(LOGICAL_ADDRESS_SPACE_IN_BYTES)-1; page++)
      pcbThread->MMURegisters[page] = pcb->MMURegisters[page];

/*
   but, allocate new stack-segment with same base/size as parent process stack-segment
      by overwriting parent process stack-segment MMURegisters[] (see above) with child thread
      stack-segment physical pages information and initialize the child thread new
      stack-segment contents to 0X00
*/
   pcbThread->SSBase = pcb->SSBase;
   pcbThread->SSSize = pcb->SSSize;
// stack-segment is M=1, V=1, E=0, W=1, R=1, PPPPPPPPPPP=11-bit physicalPage
   for (int page = PAGE_NUMBER(pcbThread->SSBase); page <= PAGE_NUMBER(pcbThread->SSBase+pcbThread->SSSize-1); page++)
   {
      WORD physicalPage;

      S16OS_AllocateMemoryPage(pcbThread,&physicalPage,"stack-segment for child thread");
      pcbThread->MMURegisters[page] = 0XD800 | physicalPage;
      for (int logicalAddress = page*MEMORY_PAGE_SIZE_IN_BYTES; logicalAddress <= (page+1)*MEMORY_PAGE_SIZE_IN_BYTES-1; logicalAddress++)
      {
         int physicalAddress = (physicalPage << 9) + (logicalAddress & 0X01FF);
         WritePhysicalMainMemory(physicalAddress,0X00);
      }
   }

// has the same label table as parent process
   pcbThread->labelTable = pcb->labelTable;

// initialize terminal OUT buffer to empty
   pcbThread->OUT[0] = '\0';

// initialize lifetime statistics
   pcbThread->runStateTime = 0;
   pcbThread->runStateCount = 0;
   pcbThread->readyStateTime = 0;
   pcbThread->readyStateCount = 0;
   pcbThread->waitStateTime = 0;
   pcbThread->waitStateCount = 0;
   pcbThread->joinStateTime = 0;
   pcbThread->joinStateCount = 0;
   pcbThread->suspendedStateTime = 0;
   pcbThread->suspendedStateCount = 0;
   pcbThread->sleepStateTime = 0;
   pcbThread->sleepStateCount = 0;
   pcbThread->semaphoreWaitTime = 0;
   pcbThread->semaphoreWaitCount = 0;
   pcbThread->mutexWaitTime = 0;
   pcbThread->mutexWaitCount = 0;
   pcbThread->messageWaitTime = 0;
   pcbThread->messageWaitCount = 0;
   pcbThread->eventWaitTime = 0;
   pcbThread->eventWaitCount = 0;
   pcbThread->diskIOWaitTime = 0;
   pcbThread->diskIOWaitCount = 0;
   pcbThread->resourcesWaitTime = 0;
   pcbThread->resourcesWaitCount = 0;
   pcbThread->contextSwitchTime = 0;
   pcbThread->contextSwitchCount = 0;
   pcbThread->HWInterruptCount = 0;
   pcbThread->CPUBurstCount = 0;
   pcbThread->IOBurstCount = 0;
   pcbThread->signalsSentCount = 0;
   pcbThread->signalsIgnoredCount = 0;
   pcbThread->signalsHandledCount = 0;

// initialize CPU scheduler quantities
   pcbThread->t   = TIME_QUANTUM;
   pcbThread->tau = TIME_QUANTUM;

// initialze resource usage quantities
   pcbThread->allocatedResourcesCount = 0;
   pcbThread->deallocatedResourcesCount = 0;

// compute turnaround time = CPU S16clock at job termination - CPU S16clock at job creation
   pcbThread->turnAroundTime = -S16clock;

// update number-of-jobs statistic
   numberOfJobsCreated++;

// add PCB to the ready queue
   pcbThread->FCFSTime = S16clock;
   AddObjectToQ(&readyQ,pcbThread,AddPCBToReadyQNow);
// ready state time statistics for PCB
   pcbThread->readyStateTime -= S16clock;
   pcbThread->readyStateCount++;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) parent process created child thread \"%s\" (handle %d)\n",
         S16clock,pcb->handle,resources[pcbThread->handle].name,pcbThread->handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   pcb->R[15] = pcbThread->handle;
   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_ACTIVE_CHILD_THREAD_COUNT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: (none)
   OUT parameters: R15 contains the count of active child threads created by the parent
      process; R0 always contains SVC_OK
*/
   pcb->R[15] = pcb->childThreadActiveCount;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) get running process active child thread count = %d\n",
         S16clock,pcb->handle,pcb->childThreadActiveCount);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_SUSPEND_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the child thread to be suspended
   OUT parameters: R0 contains SVC_ERROR012 when the child thread handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void *ObjectFromQ(QUEUE *queue,int index);
   
// ensure child thread handle is in child thread list
   int index = 1;
   bool found = false;
   CHILDTHREAD *childThread;
   while ( !found && (index <= pcb->childThreadQ.size) )
   {
   // "peek at" child thread queue index-th CHILDTHREAD record
      childThread = (CHILDTHREAD *) ObjectFromQ(&pcb->childThreadQ,index);
      if ( pcb->R[15] == childThread->handle )
         found = true;
      else
         index++;
   }
   if ( !found )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) running process attempted to suspend child thread (%d)--invalid handle\n",
            S16clock,pcb->handle,pcb->R[15]);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {
      if ( !childThread->isActive )
      {
         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"@%10d(%3d) running process attempted to suspend child thread (%d) that has already terminated\n",
               S16clock,pcb->handle,pcb->R[15]);
            fflush(TRACE);
         }
         //ENDTRACE-ing
   
      }
      else
      {
         ((PCB *) resources[childThread->handle].object)->suspendProcess = true;

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"@%10d(%3d) running process suspended child thread (%d)\n",
               S16clock,pcb->handle,pcb->R[15]);
            fflush(TRACE);
         }
         //ENDTRACE-ing

      }
      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
void DoSVC_RESUME_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the child thread to be resumed
   OUT parameters: R0 contains SVC_ERROR012 when the child thread handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void *ObjectFromQ(QUEUE *queue,int index);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToReadyQNow(void *pcbR,void *pcb);
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);

// ensure child thread handle is in child thread list
   int index = 1;
   bool found = false;
   CHILDTHREAD *childThread;
   while ( !found && (index <= pcb->childThreadQ.size) )
   {
   // "peek at" child thread queue index-th CHILDTHREAD record
      childThread = (CHILDTHREAD *) ObjectFromQ(&pcb->childThreadQ,index);
      if ( pcb->R[15] == childThread->handle )
         found = true;
      else
         index++;
   }
   if ( !found )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) running process attempted to resume child thread (%d)--invalid handle\n",
            S16clock,pcb->handle,pcb->R[15]);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {
      if ( !childThread->isActive )
      {
         
         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"@%10d(%3d) running process attempted to resume child thread (%d) that has already terminated\n",
               S16clock,pcb->handle,pcb->R[15]);
            fflush(TRACE);
         }
         //ENDTRACE-ing
   
      }
      else
      {
      // find PCB on suspended queue whose handle matches to-be-resumed child thread handle
         int index = 1;
         bool found = false;
         PCB *pcb2;
         while ( !found && (index <= suspendedQ.size) )
         {
         // "peek at" suspended queue PCB
            pcb2 = (PCB *) ObjectFromQ(&suspendedQ,index);
            if ( pcb->R[15] == pcb2->handle )
               found = true;
            else
               index++;
         }
      /* 
         When the resumed child thread is not found on suspended queue and when the child thread has been suspended by
            its parent process but has not reached a "safe point" yet, reset the "suspend process" PCB field to false
            and assume the suspend-then-immediate-resume "cancel" each other out; otherwise, an error has occurred because
            it represents a parent process attempt to resume an un-suspended child process.
      */
         if ( !found )
         {
            if ( ((PCB *) resources[pcb->R[15]].object)->suspendProcess )
            {
               ((PCB *) resources[pcb->R[15]].object)->suspendProcess = false;

               //TRACE-ing
               if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
               {
                  fprintf(TRACE,"@%10d(%3d) running process suspended-then-immediately-resumed child thread (%d)\n",
                     S16clock,pcb->handle,pcb->R[15]);
                  fflush(TRACE);
               }
               //ENDTRACE-ing

            }
            else
            {
               //TRACE-ing
               if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
               {
                  fprintf(TRACE,"@%10d(%3d) running process resumed child thread (%d) that was not suspended\n",
                     S16clock,pcb->handle,pcb->R[15]);
                  fflush(TRACE);
               }
               //ENDTRACE-ing

            }
         }
         else
         {
         // remove PCB from suspended queue
            RemoveObjectFromQ(&suspendedQ,index,(void **) &pcb2);
            pcb2->suspendProcess = false;
         // add PCB to ready queue
            AddObjectToQ(&readyQ,pcb2,AddPCBToReadyQNow);
         // update ready state and suspended state time statistics for PCB
            pcb2->suspendedStateTime += S16clock;
            pcb2->readyStateTime -= S16clock;
            pcb2->readyStateCount++;
         }

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"@%10d(%3d) running process resumed child thread (%d)\n",
               S16clock,pcb->handle,pcb->R[15]);
            fflush(TRACE);
         }
         //ENDTRACE-ing

      }
      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
void DoSVC_TERMINATE_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the child thread to be terminated
   OUT parameters: R0 contains SVC_ERROR012 when the child thread handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void *ObjectFromQ(QUEUE *queue,int index);

// ensure child thread handle is in child thread list
   int index = 1;
   bool found = false;
   CHILDTHREAD *childThread;
   while ( !found && (index <= pcb->childThreadQ.size) )
   {
   // "peek at" child thread queue index-th CHILDTHREAD record
      childThread = (CHILDTHREAD *) ObjectFromQ(&pcb->childThreadQ,index);
      if ( pcb->R[15] == childThread->handle )
         found = true;
      else
         index++;
   }
   if ( !found )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) running process attempted to terminate child thread (%d)--invalid handle\n",
            S16clock,pcb->handle,pcb->R[15]);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {
      if ( !childThread->isActive )
      {
         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"@%10d(%3d) running process attempted to terminate child thread (%d) that has already terminated\n",
               S16clock,pcb->handle,pcb->R[15]);
            fflush(TRACE);
         }
         //ENDTRACE-ing
   
      }
      else
      {
         ((PCB *) resources[childThread->handle].object)->terminateProcess = true;

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"@%10d(%3d) running process terminated child thread (%d)\n",
               S16clock,pcb->handle,pcb->R[15]);
            fflush(TRACE);
         }
         //ENDTRACE-ing

      }
      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
void DoSVC_JOIN_CHILD_THREAD(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the child thread to be joined
   OUT parameters: R0 contains SVC_ERROR012 when the child thread handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void *ObjectFromQ(QUEUE *queue,int index);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToJoinQNow(void *pcbR,void *pcb);

// ensure child thread handle is in child thread list
   int index = 1;
   bool found = false;
   CHILDTHREAD *childThread;
   while ( !found && (index <= pcb->childThreadQ.size) )
   {
   // "peek at" child thread queue index-th CHILDTHREAD record
      childThread = (CHILDTHREAD *) ObjectFromQ(&pcb->childThreadQ,index);
      if ( pcb->R[15] == childThread->handle )
         found = true;
      else
         index++;
   }
   if ( !found )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) running process attempted to join child thread (%d)--invalid handle\n",
            S16clock,pcb->handle,pcb->R[15]);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {
      if ( !childThread->isActive )
      {
         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"@%10d(%3d) running process joined child thread (%d) that has already terminated\n",
               S16clock,pcb->handle,pcb->R[15]);
            fflush(TRACE);
         }
         //ENDTRACE-ing
   
         *returnCode = SVC_OK;
         *shouldAddPCBToWaitQ = true;
      }
      else
      {
         pcb->handleOfChildThreadJoined = pcb->R[15];
         AddObjectToQ(&joinQ,pcb,AddPCBToJoinQNow);
         pcb->joinStateTime -= S16clock;
         pcb->joinStateCount++;
   
         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
         {
            fprintf(TRACE,"@%10d(%3d) running process joined child thread (%d)--waiting for child thread to terminate\n",
               S16clock,pcb->handle,pcb->R[15]);
            fflush(TRACE);
         }
         //ENDTRACE-ing
   
         *returnCode = SVC_OK;
         *shouldAddPCBToWaitQ = false;
      }
   }
}

//--------------------------------------------------
void DoSVC_GET_RESOURCE_TYPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the NUL-terminated string
      that is the name of the resource whose type is requested
   OUT parameters: R15 contains the resource type (NOTCREATEDS when resource does not exist);
      R0 always contains SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void FindNameInResources(const char name[],bool *found,HANDLE *handle);

   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   HANDLE handle;
   bool found;

   GetNULTerminatedString(pcb,pcb->R[15],name);
   FindNameInResources(name,&found,&handle);
   if ( found )
   {
      pcb->R[15] = resources[handle].type;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) get resource type for \"%s\" (handle %d) = ",
            S16clock,pcb->handle,name,handle);
         switch ( resources[handle].type )
         {
            case           JOBS: fprintf(TRACE,          "JOB\n"); break;
            case   MESSAGEBOXES: fprintf(TRACE,   "MESSAGEBOX\n"); break;
            case MEMORYSEGMENTS: fprintf(TRACE,"MEMORYSEGMENT\n"); break;
            case     SEMAPHORES: fprintf(TRACE,    "SEMAPHORE\n"); break;
            case        MUTEXES: fprintf(TRACE,        "MUTEX\n"); break;
            case         EVENTS: fprintf(TRACE,        "EVENT\n"); break;
            case          PIPES: fprintf(TRACE,         "PIPE\n"); break;
         }
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }
   else
   {
      pcb->R[15] = NOTCREATEDS;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) get resource type for \"%s\" (handle %d) = NOTCREATED\n",
            S16clock,pcb->handle,name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }
   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_RESOURCE_HANDLE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the NUL-terminated string
      that is the name of the resource whose handle is requested
   OUT parameters: R15 contains the resource handle when it exists; R0 contains SVC_ERROR009
      when the resource does not exist; otherwise SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void FindNameInResources(const char name[],bool *found,HANDLE *handle);

   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   HANDLE handle;
   bool found;

   GetNULTerminatedString(pcb,pcb->R[15],name);
   FindNameInResources(name,&found,&handle);
   if ( !found )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) get resource handle for \"%s\" failure\n",
            S16clock,pcb->handle,name);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR009;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   pcb->R[15] = handle;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) get resource handle for \"%s\" = %d\n",
         S16clock,pcb->handle,name,handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_WAIT_FOR_RESOURCE_HANDLE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the NUL-terminated string
      that is the name of the resource whose handle is requested
   OUT parameters: R15 contains the resource handle; R0 always contains SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void FindNameInResources(const char name[],bool *found,HANDLE *handle);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddResourceWaitToResourcesWaitQNow(void *resourceWaitR,void *resourceWait);

   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   HANDLE handle;
   bool found;

   GetNULTerminatedString(pcb,pcb->R[15],name);
   FindNameInResources(name,&found,&handle);
/*
   when resource already exists, return handle and don't make requesting process wait
      on resources wait queue
*/
   if ( found )
   {
      pcb->R[15] = handle;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) wait for resource handle for resource \"%s\", required no wait, handle = %d\n",
            S16clock,pcb->handle,name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing
      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
      return;
   }
/*
   otherwise, make requesting process wait (along with name of resource) on
      resources wait queue and update resources wait queue statistics
*/
   RESOURCEWAIT *resourceWait = (RESOURCEWAIT *) malloc(sizeof(RESOURCEWAIT));

   resourceWait->pcb = pcb;
   resourceWait->name = (char *) malloc(sizeof(char)*(strlen(name)+1));
   strcpy(resourceWait->name,name);
   AddObjectToQ(&resourcesWaitQ,resourceWait,AddResourceWaitToResourcesWaitQNow);
   pcb->resourcesWaitTime -= S16clock;
   pcb->resourcesWaitCount++;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) wait for resource handle for resource \"%s\" required wait\n",
         S16clock,pcb->handle,name);
      fflush(TRACE);
   }
   
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = false;
}

//--------------------------------------------------
void DoSVC_GET_RESOURCE_NAME(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: handle of resource
      word   1: logical address-of buffer in running process where resource name should be written
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   BYTE HOB,LOB;
   HANDLE handle;
   WORD LA;

   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+1),&LOB);
   handle = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+2),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+3),&LOB);
   LA = MAKEWORD(HOB,LOB);

// ensure handle is allocated
   if ( !resources[handle].allocated )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) get resource name for (handle %d) failed, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      for (int i = 0; i <= (int) strlen("*UNKNOWN*"); i++)
      {
         WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+2*i+0),(BYTE)           0X00);
         WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+2*i+1),(BYTE) "*UNKNOWN*"[i]);
      }
      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   for (int i = 0; i <= (int) strlen(resources[handle].name); i++)
   {
      WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+2*i+0),(BYTE)           0X00);
      WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+2*i+1),(BYTE) resources[handle].name[i]);
   }

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) get resource name for (handle %d) = \"%s\"\n",
         S16clock,pcb->handle,handle,resources[handle].name);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_READ_FROM_TERMINAL(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the NUL-terminated format string that 
      specifies an optional prompt followed by one format specifier that describes the
      the one datum to be input from the user terminal
   OUT parameters: only one of the registers R1-R15 is considered an OUT parameter when
      it is named in the format specifier; R0 contains SVC_ERROR002 when a read error occurred; 
      R0 contains SVC_EOF when an end-of-file condition occurred; otherwise R0 contains SVC_OK
*/

   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);

   char format[MEMORY_PAGE_SIZE_IN_BYTES+1];
   int fi;
   char prompt[MEMORY_PAGE_SIZE_IN_BYTES+1];
   int pi;
   char datum[MEMORY_PAGE_SIZE_IN_BYTES+1];
   int IOResult;

   GetNULTerminatedString(pcb,pcb->R[15],format);
/*
   The format string must contain exactly one format specifier using the syntax %XT where 
      X in { 1,...,15 } (notice R0 is not allowed) and T in { i,I,u,U,h,H,c,C,b,B,s,S }. Notice 
      that T is case neutral. 
   
   X names the general-purpose S16 register, RX, that is used to transmit the datum to the 
      calling program directly (the scalar datum value is stored in RX on return to the calling
      program) or indirectly (the array datum, a NUL-terminated string, is stored into a buffer
      whose logical address is stored in RX).
      
   T names the data (T)ype of the datum interpreted as follows
      
      * signed decimal (I)nteger (scalar)
      * (U)nsigned decimal integer (scalar)
      * unsigned (H)exadecimal integer (scalar)
      * (C)haracter (scalar)
      * (B)oolean (scalar)
      * (S)tring (array)
      
   The format string format specifier may be prefixed with a prompt. For example, the format
      string "%6I" (format string does not contain a prompt) means input a signed (I)nteger scalar
      datum from the user terminal and pass the datum to the calling program in R6; "flag? %6B" 
      (format string contains the prompt "flag? ") means input a (B)oolean datum from the
      user terminal and pass the datum in R6.
       
   *Notes*
      * Each READ_FROM_TERMINAL inputs an entire '\n'-terminated line from the user terminal, deletes
        the '\n' from the end of the line, converts the contents of the line in an attempt to satisfy 
        the data type, then discards the line.
      * The single keystroke Ctrl+Z is interpreted as interactive EOF; SVC_EOF is returned in R0.
      * Any conversion error is returned as SVC_ERROR002 in R0.
      * The legal range of signed decimal (I)nteger input using %XI is [ -32768,32767 ].
      * The legal range of (U)nsigned decimal integer input using %XU is [ 0,65535 ].
      * The legal range of an unsigned (H)exadecimal integer input using %XH is [ 0X0000,0XFFFF ].
      * The legal boolean input is case-sensitive so it *MUST* be spelled exactly the way that 
        TRUE_STRING or FALSE_STRING settings is spelled in the .config file; legal boolean input may *NOT*
        have any leading and/or trailing white space.
      * The character '\n' *CANNOT* be input using %XC, otherwise any printable character that can be typed
        can be input using %XC.
      * The programmer must be absolutely certain that the buffer pointed-to by RX is large enough
        to contain any string that may be input by the user. The string input is the entire line 
        entered by the user because the string is delimited by the end-of-line and not by the
        first white space character in the string.
      
   The prompt used during input is (in decreasing order of priority)
      * The prompt that is included in the format string (when the format string contains a prompt).
      * When the format string does not contain a prompt, then the terminal OUT buffer
        is used when it is not empty (this empties the terminal OUT buffer) (*NOTE* This supports the 
        "tie" between the terminal IN and OUT buffers that exists for most standard input/output terminal
        devices; that is, the OUT buffer is "flushed" to the user terminal before input is done).
      * When the format string does not contain a prompt and the terminal OUT buffer is empty, then
        the default terminal prompt specified in the .config file is used.
*/
   fi = 0;
   *returnCode = SVC_OK;
// scan format string for prompt[]
   prompt[0] = '\0';
   pi = 0;
   while ( (format[fi] != '%') && (fi <= strlen(format)-1) )
   {
      prompt[pi] = format[fi];
      pi++;
      fi++;
   }
   prompt[pi] = '\0';
// ensure format string contains a specification
   if ( format[fi] != '%' )
      *returnCode = SVC_ERROR002;
   else
   {
      int RX;

      fi++;
      if ( !isdigit(format[fi]) )
         *returnCode = SVC_ERROR002;
      else
      {
         RX = format[fi]-'0';
         fi++;
         if ( isdigit(format[fi]) )
         {
            RX = RX*10+(format[fi]-'0');
            fi++;
         }
         if ( (RX < 0) || (RX > 15) )
            *returnCode = SVC_ERROR002;
         else
         {
         // display prompt
            if      ( strlen(prompt) > 0 )                                    // (1)
               printf(       "(%3d)< %s",pcb->handle,prompt);
            else if ( strlen(pcb->OUT) > 0 )                                  // (2)
            {
               printf(       "(%3d)< %s",pcb->handle,pcb->OUT);
               strcpy(prompt,pcb->OUT);
               pcb->OUT[0] = '\0';
            }
            else
            {
               printf(       "(%3d)< %s",pcb->handle,defaultTerminalPrompt);  // (3)
               strcpy(prompt,defaultTerminalPrompt);
            }
            
         // input datum
            fgets(datum,MEMORY_PAGE_SIZE_IN_BYTES,stdin);
            datum[(int) strlen(datum)-1] = '\0';
         // check for Ctrl+Z (EOF) input
            if ( feof(stdin) )
            {
               printf("(%3d)< EOF\n",pcb->handle);

               //TRACE-ing
               if ( ENABLE_TRACING && TRACE_TERMINAL_IO )
               {
                  fprintf(TRACE,"(%3d)< EOF\n",pcb->handle);
                  fflush(TRACE);
               }
               //ENDTRACE-ing

               *returnCode = SVC_EOF;
            }
            else
            {
            // convert datum string to appropriate S16 data type
               switch ( format[fi] )
               {
                  case 'i': case 'I':
                     {
                        IOResult = sscanf(datum,"%hd",&pcb->R[RX]);
                     }
                     break;
                  case 'u': case 'U':
                     {
                        IOResult = sscanf(datum,"%hu",&pcb->R[RX]);
                     }
                     break;
                  case 'h': case 'H':
                     {
                        IOResult = sscanf(datum,"%hX",&pcb->R[RX]);
                     }
                     break;
                  case 'c': case 'C':
                     {
                        char c;
                        
                        IOResult = sscanf(datum,"%c",&c);
                        pcb->R[RX] = (WORD) c;
                     }
                     break;
                  case 'b': case 'B':
                     {
                        IOResult = 1;
                        if      ( strcmp(datum, TRUEString) == 0 )
                           pcb->R[RX] = 1;
                        else if ( strcmp(datum,FALSEString) == 0 )
                           pcb->R[RX] = 0;
                        else
                           IOResult = 0;
                     }
                     break;
                  case 's': case 'S':
                     {
                        WORD logicalAddress = pcb->R[RX];
                        int i;

                        for (i = 0; i <= (int) strlen(datum); i++)
                        {
                           WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD)(logicalAddress+2*i+0),(BYTE) 0X00);
                           WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD)(logicalAddress+2*i+1),(BYTE) datum[i]);
                        }
                     }
                     IOResult = 1;
                     break;
                  default:
                     *returnCode = SVC_ERROR002;
                     break;
               }
               if ( IOResult ==   1 )
               {

                  //TRACE-ing
                  if ( ENABLE_TRACING && TRACE_TERMINAL_IO )
                  {
                     fprintf(TRACE,"(%3d)< %s%s\n",pcb->handle,prompt,datum);
                     fflush(TRACE);
                  }
                  //ENDTRACE-ing

               }
               else
               {
                   printf("(%3d)< Terminal IO error\n",pcb->handle);

                  //TRACE-ing
                  if ( ENABLE_TRACING && TRACE_TERMINAL_IO )
                  {
                     fprintf(TRACE,"(%3d)< Terminal IO error\n",pcb->handle);
                     fflush(TRACE);
                  }
                  //ENDTRACE-ing

                  *returnCode = SVC_ERROR002;
               }
            }
         }
      }
   }
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_WRITE_TO_TERMINAL(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the NUL-terminated format string that
       is used to write formatted register contents to the terminal OUT buffer; registers 
       R0-R15 are considered IN parameters when named in a format specification
   OUT parameters: R0 contains SVC_ERROR002 when a formatting error occurred; otherwise
       R0 contains SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);

   char format[MEMORY_PAGE_SIZE_IN_BYTES+1];
   int fi;
   bool error,atEOL;
   BYTE datum[MEMORY_PAGE_SIZE_IN_BYTES+1];

   GetNULTerminatedString(pcb,pcb->R[15],format);
/*
   The format string contains non-specification characters interspersed with 0 or more format specifiers each
      formed using the syntax %n or %XT where X in { 0,1,...,15 } and T in { i,I,u,U,h,H,c,C,b,B,s,S }. 
      Notice that T is case neutral.

   The format string is scanned character-by-character from left-to-right text is appended to the 
      terminal OUT buffer as follows

      * Every non-format-specifier character encountered (that is, each character that is *not* part of
        a %XT or %n format specifier) is appended to the terminal OUT buffer contents.
      * Every %n format specifier encountered causes the current contents of the terminal OUT buffer to be
        "flushed" to the user terminal, that is, the terminal OUT buffer contents are output to the 
        user terminal and the terminal OUT buffer is emptied.
      * Every %XT specifier encountered contains an X which names a general-purpose S16 register, RX,
        and a data (T)ype which describes the formatting of the datum in RX as follows
           * signed decimal (I)nteger
           * (U)nsigned decimal integer
           * unsigned (H)exadecimal integer
           * single(C)haracter
           * (B)oolean
           * RX contains the logical address-of a NUL-terminated (S)tring
      * Every %% appends a single % to the terminal OUT buffer

   The left-to-right scan continues as described above until the end of the format string is encountered.
   
   *Notes* 
      * The datums appended to the terminal OUT buffer are not output to the user terminal until a
        %n specification is encountered during the left-to-right scan of the format string.
      * The terminal OUT buffer is automatically "flushed" before new datum characters are appended to the
        terminal OUT buffer if the new datam characters will cause the terminal OUT buffer overflow.
      * The terminal OUT buffer is automatically "flushed" when a job terminates with a non-empty
        terminal OUT buffer.

   For example, when
      R3 contains the integer 12345
      R4 contains the integer 98
      R5 contains the character code for 'A' 
      R6 contains the boolean FALSE (0 means FALSE, not-0 means TRUE)
      R7 contains the integer 32767
      R11 contains the logical address-of the NUL-terminated string "ABCXYZ"

   then
      format string "X = %3i%nY = %4i%n"
         displays the two lines "X = 12345" and "Y = 98"

      format string "%4h %7H%n"
         displays the line "0X0062 0X7FFF"

      format string "flag = %6B"
         buffers the datums "flag = FALSE" but does not output them to the user terminal

      format string "Did you know %5I is the integer equivalent of %5c?%n" 
         displays the line "Did you know 65 is the integer equivalent of A?"

      format string "The string is %11S!%n"
         displays the line "The string is ABCXYZ!"
*/
   fi = 0;
   error = false;
   atEOL = false;
   while ( (fi <= (int) strlen(format)) && !error )
   {
      if ( format[fi] == '%' )
      {
         int RX;

         fi++;
         if      ( format[fi] == '%' )
         {
            datum[0] = '%'; datum[1] = '\0';
            fi++;
         }
         else if ( format[fi] == 'n' )
         {
            atEOL = true;
            datum[0] = '\0';
            fi++;
         }
         else
         {
            if ( !isdigit(format[fi]) )
            {
               error = true;
               continue;
            }
            RX = format[fi]-'0';
            fi++;
            if ( isdigit(format[fi]) )
            {
               RX = RX*10+(format[fi]-'0');
               fi++;
            }
            if ( (RX < 0) || (RX > 15) )
            {
               error = true;
               continue;
            }
            switch ( format[fi] )
            {
               case 'i': case 'I':
                  sprintf(datum,"%hd",pcb->R[RX]);
                  break;
               case 'u': case 'U':
                  sprintf(datum,"%hu",pcb->R[RX]);
                  break;
               case 'h': case 'H':
                  sprintf(datum,"0X%04hX",pcb->R[RX]);
                  break;
               case 'c': case 'C':
                  sprintf(datum,"%c",(char) pcb->R[RX]);
                  break;
               case 'b': case 'B':
                  if ( pcb->R[RX] == 0 )
                     strcpy(datum,FALSEString);
                  else
                     strcpy(datum,TRUEString);
                  break;
               case 's': case 'S':
                  GetNULTerminatedString(pcb,pcb->R[RX],datum);
                  break;
               default:
                  error = true;
                  break;
            }
            if ( error ) continue;
            fi++;
         }
      }
   // append format string non-specification character to end of terminal OUT buffer
      else
      {
         datum[0] = format[fi]; datum[1] = '\0';
         fi++;
      }
   // "flush" terminal OUT buffer whenever the new datum characters overflow the buffer
      if ( atEOL || (strlen(pcb->OUT)+strlen(datum) > MEMORY_PAGE_SIZE_IN_BYTES) )
      {
         printf(       "(%3d)> %s\n",pcb->handle,pcb->OUT);

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_TERMINAL_IO )
         {
            fprintf(TRACE,"(%3d)> %s\n",pcb->handle,pcb->OUT);
            fflush(TRACE);
         }
         //ENDTRACE-ing

         pcb->OUT[0] = '\0';
         atEOL = false;
      }

   // append datum to terminal OUT buffer
      strcat(pcb->OUT,datum);
   }

   if ( error )
      *returnCode = SVC_ERROR002;
   else
   {
      *returnCode = SVC_OK;
   }
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_READ_DISK_SECTOR(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: HOB/LOB of sector address
      word   1: HOB/LOB logical address-of sector buffer
   OUT parameters: R0 contains SVC_ERROR011 when the sector address is out of range;
      R0 contains otherwise SVC_OK
*/
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddDiskIOToDiskIOQNow(void *diskIOR,void *diskIO);
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void DriverSWForDisk(int command,...);

   WORD sectorAddress;
   WORD bufferAddress;
   BYTE HOB,LOB;

/*
   2 head/disk, 128 track/head, 32 sector/track = 8192-sector hard disk
     with (8192 sectors)*(128-byte/sector) = 1M byte capacity
   
                       MSB              LSB
                         0123456789012345
   16-bit sector address XXXTTTTTTTSSSSSH that "fits" in one S16 word
     X = (reserved for future use)
     H = head 0,1
     T = track 0000000-1111111 (0-127)
     S = sector 00000-11111 (0-31)
*/

// get sectorAddress and bufferAddress from parameter block
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 1),&LOB);
   sectorAddress = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 2),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 3),&LOB);
   bufferAddress = MAKEWORD(HOB,LOB);
   if ( /*(0 <= sectorAddress) && */ (sectorAddress <= SECTORS_PER_DISK-1) )
   {
      DISKIO *diskIO = (DISKIO *) malloc(sizeof(DISKIO));
      bool enabled;

      diskIO->pcb = pcb;
      diskIO->command = 2;
      diskIO->sectorAddress = sectorAddress;
      diskIO->bufferAddress = bufferAddress;
      diskIO->sectorSize = BYTES_PER_SECTOR;
   /*
     add diskIO to disk request queue using scheduling strategy
        and update disk request wait queue statistics
   */
      AddObjectToQ(&diskIOQ,diskIO,AddDiskIOToDiskIOQNow);

      pcb->diskIOWaitTime -= S16clock;
      pcb->diskIOWaitCount++;
   /*
      normal termination; do not add process PCB to wait queue (because it's
         waiting in the disk request queue)
   */
      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = false;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_DISK_IO )
      {
         fprintf(TRACE,"@%10d(%3d) read disk sector #OX%04X into buffer at OX%04X added to diskIOQ\n",
            S16clock,pcb->handle,diskIO->sectorAddress,diskIO->bufferAddress);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   // when disk controller is disabled, take next disk request from the disk request queue
      DriverSWForDisk(4,&enabled);
      if ( !enabled )
      {
         DISKIO *diskIO;

         RemoveObjectFromQ(&diskIOQ,1,(void **) &diskIO);

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_DISK_IO )
         {
            fprintf(TRACE,"@%10d(%3d) disk IO request removed from diskIOQ\n",
               S16clock,diskIO->pcb->handle);
            fflush(TRACE);
         }
         //ENDTRACE-ing

      //  call the disk driver to start the disk IO request
         DriverSWForDisk(diskIO->command,diskIO);
         diskIOInProgress = diskIO;
      }
   }
   else
   {
      *returnCode = SVC_ERROR011;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
void DoSVC_WRITE_DISK_SECTOR(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: HOB/LOB of sector address
      word   1: HOB/LOB logical address-of sector buffer
   OUT parameters: R0 contains SVC_ERROR011 when the sector address is out of range;
      R0 contains otherwise SVC_OK
*/
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddDiskIOToDiskIOQNow(void *diskIOR,void *diskIO);
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void DriverSWForDisk(int command,...);

   WORD sectorAddress;
   WORD bufferAddress;
   BYTE HOB,LOB;

/*
   2 head/disk, 128 track/head, 32 sector/track = 8192-sector hard disk
     with (8192 sectors)*(128-byte/sector) = 1M byte capacity
   
                       MSB              LSB
                         0123456789012345
   16-bit sector address XXXTTTTTTTSSSSSH that "fits" in one S16 word
     X = (reserved for future use)
     H = head 0,1
     T = track 0000000-1111111 (0-127)
     S = sector 00000-11111 (0-31)
*/

// get sectorAddress and bufferAddress from parameter block
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 1),&LOB);
   sectorAddress = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 2),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 3),&LOB);
   bufferAddress = MAKEWORD(HOB,LOB);
   if ( /* (0 <= sectorAddress) && */ (sectorAddress <= SECTORS_PER_DISK-1) )
   {
      DISKIO *diskIO = (DISKIO *) malloc(sizeof(DISKIO));
      bool enabled;

      diskIO->pcb = pcb;
      diskIO->command = 3;
      diskIO->sectorAddress = sectorAddress;
      diskIO->bufferAddress = bufferAddress;
      diskIO->sectorSize = BYTES_PER_SECTOR;
   /*
     add diskIO to disk request queue using scheduling strategy
        and update disk request wait queue statistics
   */
      AddObjectToQ(&diskIOQ,diskIO,AddDiskIOToDiskIOQNow);

      pcb->diskIOWaitTime -= S16clock;
      pcb->diskIOWaitCount++;
   /*
      normal termination; do not add process PCB to wait queue (because it's
         waiting in the disk request queue)
   */
      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = false;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_DISK_IO )
      {
         fprintf(TRACE,"@%10d(%3d) write disk sector #OX%04X from buffer at OX%04X added to diskIOQ\n",
            S16clock,pcb->handle,diskIO->sectorAddress,diskIO->bufferAddress);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   // when disk controller is disabled, take next disk request from the disk request queue
      DriverSWForDisk(4,&enabled);
      if ( !enabled )
      {
         DISKIO *diskIO;

         RemoveObjectFromQ(&diskIOQ,1,(void **) &diskIO);

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_DISK_IO )
         {
            fprintf(TRACE,"@%10d(%3d) disk IO request removed from diskIOQ\n",S16clock,diskIO->pcb->handle);
            fflush(TRACE);
         }
         //ENDTRACE-ing

      // call the disk driver to start the disk IO request
         DriverSWForDisk(diskIO->command,diskIO);
         diskIOInProgress = diskIO;
      }
   }
   else
   {
      *returnCode = SVC_ERROR011;
      *shouldAddPCBToWaitQ = true;
   }
}

//--------------------------------------------------
void DoSVC_CREATE_MEMORY_SEGMENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: LB page number of memory-segment resource to be created
      word   1: UB page number of memory-segment resource to be created
      word   2: name[0] character 1 of n-character, NUL-terminated string that is the name 
      word   3: name[1]    of the memory-segment resource to be created
             .       .
      n+3: name[n] = NULL
   OUT parameters: R15 contains the handle of the memory-segment resource (when allocated);
      R0 contains SVC_ERROR008 when the creation failed; R0 contains SVC_ERROR021 when
      one or more pages in page range are not free; otherwise R0 contains SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void AllocateResourceHandle(PCB *pcb,const char name[],RESOURCETYPE type,bool *allocated,HANDLE *handle);
   void S16OS_AllocateMemoryPage(const PCB *pcb,WORD *physicalPage,const char situation[]);

   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   HANDLE handle;
   bool allocated;
   WORD LBPageNumber;
   WORD UBPageNumber;

   BYTE HOB,LOB;

// get LB page number and UB page number from parameter block
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 1),&LOB);
   LBPageNumber = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 2),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 3),&LOB);
   UBPageNumber = MAKEWORD(HOB,LOB);

// get memory-segment name from parameter block, then allocate MEMORYSEGMENTS resource handle
   GetNULTerminatedString(pcb,(WORD) (pcb->R[15]+4),name);
   AllocateResourceHandle(pcb,name,MEMORYSEGMENTS,&allocated,&handle);
   if ( !allocated )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
      {
         fprintf(TRACE,"@%10d(%3d) create memory-segment named \"%s\" failure\n",
            S16clock,pcb->handle,name);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR008;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   bool areAllNewPagesFree = true;

   for (int page = LBPageNumber; page <= UBPageNumber; page++)
      areAllNewPagesFree = areAllNewPagesFree && (((pcb->MMURegisters[page] & 0X4000) >> 14) == 0);
   if ( !areAllNewPagesFree )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MISCELLANEOUS_SVC )
      {
         fprintf(TRACE,"@%10d(%3d) create memory-segment \"%s\" failure, page range %d-%d not free\n",
            S16clock,pcb->handle,name,LBPageNumber,UBPageNumber);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR021;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// each page of memory(data)-segment is M=1, V=1, E=0, W=1, R=1, PPPPPPPPPPP=11-bit physicalPage
   for (int page = LBPageNumber; page <= UBPageNumber; page++)
   {
      WORD physicalPage;

      S16OS_AllocateMemoryPage(pcb,&physicalPage,"allocate memory-segment");
      pcb->MMURegisters[page] = 0XD800 | physicalPage;
   }

// initialize resource with newly-constructed memorySegment
   MEMORYSEGMENT *memorySegment = (MEMORYSEGMENT *) malloc(sizeof(MEMORYSEGMENT));

   memorySegment->handle = handle;
   memorySegment->ownerHandle = pcb->handle;
   memorySegment->LBPageNumber = LBPageNumber;
   memorySegment->UBPageNumber = UBPageNumber;
   resources[handle].object = memorySegment;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
   {
      fprintf(TRACE,"@%10d(%3d) create memory-segment, pages %d-%d = %d pages, named \"%s\" (handle %d)\n",
         S16clock,pcb->handle,LBPageNumber,UBPageNumber,(UBPageNumber-LBPageNumber+1),name,handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

// return handle in R15
   pcb->R[15] = handle;

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_DESTROY_MEMORY_SEGMENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the memory-segment resource to be destroyed
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; R0 contains 
      SVC_ERROR010 when the job making the request is not the owner; 
      otherwise R0 contains SVC_OK
*/
   void S16OS_DeallocateMemoryPage(const PCB *pcb,WORD physicalPage,const char situation[]);
   void DeallocateResourceHandle(PCB *pcb,HANDLE handle);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

/*
   ensure handle is valid, specifies an existing memory-segment, and destroy request is
      made by creator/owner of the resource
*/
   if ( !IsValidHandleForResourceType(handle,MEMORYSEGMENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
      {
         fprintf(TRACE,"@%10d(%3d) destroy memory-segment (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   MEMORYSEGMENT *memorySegment = (MEMORYSEGMENT *) resources[handle].object;

   if ( memorySegment->ownerHandle != pcb->handle )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
      {
         fprintf(TRACE,"@%10d(%3d) destroy memory-segment (handle %d) failure\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      ProcessWarningOrError(S16OSWARNING,"Only owner can destroy resource");
      *returnCode = SVC_ERROR010;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// deallocate memory-segment pages
   for (int page = memorySegment->LBPageNumber; page <= memorySegment->UBPageNumber; page++)
   {
      WORD physicalPage = pcb->MMURegisters[page] & 0X01FF;
      S16OS_DeallocateMemoryPage(pcb,physicalPage,"destroy memory-segment");
   }

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
   {
      fprintf(TRACE,"@%10d(%3d) destroy memory-segment, pages %d-%d = %d pages, named \"%s\" (handle %d)\n",
         S16clock,pcb->handle,memorySegment->LBPageNumber,memorySegment->UBPageNumber,
         (memorySegment->UBPageNumber-memorySegment->LBPageNumber+1),
         resources[handle].name,handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   DeallocateResourceHandle(pcb,handle);
   free(memorySegment);
   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_SHARE_MEMORY_SEGMENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the memory-segment resource to be shared
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; R0 contains
      SVC_ERROR022 when one or more pages in page range are not free; 
      otherwise R0 contains SVC_OK
*/
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing memory-segment
   if ( !IsValidHandleForResourceType(handle,MEMORYSEGMENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
      {
         fprintf(TRACE,"@%10d(%3d) share memory-segment (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }
 
   MEMORYSEGMENT *memorySegment = (MEMORYSEGMENT *) resources[handle].object;

   bool areAllNewPagesFree = true;

   for (int page = memorySegment->LBPageNumber; page <= memorySegment->UBPageNumber; page++)
      areAllNewPagesFree = areAllNewPagesFree && (((pcb->MMURegisters[page] & 0X4000) >> 14) == 0);
   if ( !areAllNewPagesFree )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MISCELLANEOUS_SVC )
      {
         fprintf(TRACE,"@%10d(%3d) share memory-segment \"%s\" (handle %d) owned by (handle %d) failure, page range %d-%d not free\n",
            S16clock,pcb->handle,resources[handle].name,handle,memorySegment->ownerHandle,
            memorySegment->LBPageNumber,memorySegment->UBPageNumber);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR022;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// copy MMURegisters[LBPageNumber:UBPageNumber] from resource-owner to share-er
   PCB *pcbOwner = (PCB *) resources[memorySegment->ownerHandle].object;
   for (int page = memorySegment->LBPageNumber; page <= memorySegment->UBPageNumber; page++)
      pcb->MMURegisters[page] = pcbOwner->MMURegisters[page];

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
   {
      fprintf(TRACE,"@%10d(%3d) share memory-segment \"%s\" (handle %d) owned by job \"%s\" (handle %d), page range %d-%d\n",
         S16clock,pcb->handle,resources[handle].name,handle,
         resources[memorySegment->ownerHandle].name,memorySegment->ownerHandle,
         memorySegment->LBPageNumber,memorySegment->UBPageNumber);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_UNSHARE_MEMORY_SEGMENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the memory-segment resource to be un-shared
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing memory-segment
   if ( !IsValidHandleForResourceType(handle,MEMORYSEGMENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
      {
         fprintf(TRACE,"@%10d(%3d) un-share memory-segment (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }
 
   MEMORYSEGMENT *memorySegment = (MEMORYSEGMENT *) resources[handle].object;

// set share-er MMURegisters[LBPageNumber:UBPageNumber] to unused
   for (int page = memorySegment->LBPageNumber; page <= memorySegment->UBPageNumber; page++)
      pcb->MMURegisters[page] = 0X0000;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
   {
      fprintf(TRACE,"@%10d(%3d) un-share memory-segment \"%s\" (handle %d) owned by (handle %d), page range %d-%d\n",
         S16clock,pcb->handle,resources[handle].name,handle,memorySegment->ownerHandle,
         memorySegment->LBPageNumber,memorySegment->UBPageNumber);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_MEMORY_SEGMENT_LB_PAGE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the memory-segment resource
   OUT parameters: R15 contains the memory-segment LB page number; R0 contains SVC_ERROR012 when 
      the handle is not valid; otherwise R0 contains SVC_OK
*/
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing memory-segment
   if ( !IsValidHandleForResourceType(handle,MEMORYSEGMENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
      {
         fprintf(TRACE,"@%10d(%3d) get memory-segment (handle %d) LB page number failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }
 
   MEMORYSEGMENT *memorySegment = (MEMORYSEGMENT *) resources[handle].object;

      //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
   {
      fprintf(TRACE,"@%10d(%3d) get memory-segment \"%s\" (handle %d) LB page number = %d\n",
         S16clock,pcb->handle,resources[handle].name,handle,memorySegment->LBPageNumber);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   pcb->R[15] = memorySegment->LBPageNumber;
   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_MEMORY_SEGMENT_SIZE_IN_PAGES(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the memory-segment resource
   OUT parameters: R15 contains the memory-segment size-in-pages; R0 contains SVC_ERROR012 when 
      the handle is not valid; otherwise R0 contains SVC_OK
*/
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing memory-segment
   if ( !IsValidHandleForResourceType(handle,MEMORYSEGMENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
      {
         fprintf(TRACE,"@%10d(%3d) get memory-segment (handle %d) size-in-pages memory-segment failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }
 
   MEMORYSEGMENT *memorySegment = (MEMORYSEGMENT *) resources[handle].object;

      //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MEMORYSEGMENTS )
   {
      fprintf(TRACE,"@%10d(%3d) get memory-segment \"%s\" (handle %d) size-in-pages = %d\n",
         S16clock,pcb->handle,resources[handle].name,handle,
         (memorySegment->UBPageNumber-memorySegment->LBPageNumber+1));
      fflush(TRACE);
   }
   //ENDTRACE-ing

   pcb->R[15] = memorySegment->UBPageNumber-memorySegment->LBPageNumber+1;
   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_CREATE_MESSAGEBOX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: HOB/LOB capacity of queue, 0 = unbounded
      word   1: name[0] character 1 of n-character, NUL-terminated string
      word   2: name[1]
             .       .
           n+1: name[n] = NULL
   OUT parameters: R15 contains the handle of the message box (when allocated);
      R0 contains SVC_ERROR014 when the capacity is out of range; R0 contains
      SVC_ERROR008 when the creation failed; otherwise R0 contains SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void AllocateResourceHandle(PCB *pcb,const char name[],RESOURCETYPE type,bool *allocated,HANDLE *handle);
   void ConstructQ(QUEUE *queue,char *name,bool containsPCBs,void (*TraceQNODEObject)(void *object));
   void TraceMessageBoxQObject(void *object);
   
   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   HANDLE handle;
   bool allocated;
   int capacity;
   BYTE HOB,LOB;

// get capacity and message box name from parameter block and ensure (capacity >= 0)
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 1),&LOB);
   capacity = MAKEWORD(HOB,LOB);
   GetNULTerminatedString(pcb,(WORD) (pcb->R[15]+2),name);
   if ( capacity < 0 )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
      {
         fprintf(TRACE,"@%10d(%3d) create message box named \"%s\", capacity = %d failure\n",
            S16clock,pcb->handle,name,capacity);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR014;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// allocate MESSAGEBOXES resource handle
   AllocateResourceHandle(pcb,name,MESSAGEBOXES,&allocated,&handle);
   if ( !allocated )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
      {
         fprintf(TRACE,"@%10d(%3d) create message box named \"%s\", capacity = %d failure\n",
            S16clock,pcb->handle,name,capacity);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR008;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// dynamically-allocate and construct message box
   MESSAGEBOX *messageBox = (MESSAGEBOX *) malloc(sizeof(MESSAGEBOX));
   char *QName = (char *) malloc(sizeof(char)*(strlen(name)+strlen(" (message box)")+1));

   messageBox->handle = handle;
   messageBox->ownerHandle = pcb->handle;
   messageBox->waiting = false;
   messageBox->capacity = capacity;
   sprintf(QName,"%s (message box)",name);
   ConstructQ(&messageBox->Q,QName,false,TraceMessageBoxQObject);
   free(QName);

// initialize resource with newly-constructed message box
   resources[handle].object = messageBox;
// return handle in R15
   pcb->R[15] = handle;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
   {
      fprintf(TRACE,"@%10d(%3d) create message box named \"%s\" (handle %d), capacity = %d\n",
         S16clock,pcb->handle,name,handle,capacity);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_DESTROY_MESSAGEBOX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the message box resource to be destroyed
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; R0 contains
      SVC_ERROR010 when the job making the request is not the owner;
      otherwise R0 contains SVC_OK
*/
   void TraceQ(QUEUE *queue);
   void DestructQ(QUEUE *queue);
   void DeallocateResourceHandle(PCB *pcb,HANDLE handle);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

/*
   ensure handle is valid, specifies an existing message box, and destroy request is
      made by creator/owner of the resource
*/
   if ( !IsValidHandleForResourceType(handle,MESSAGEBOXES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
      {
         fprintf(TRACE,"@%10d(%3d) destroy message box (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   MESSAGEBOX *messageBox = (MESSAGEBOX *) resources[handle].object;

   if ( messageBox->ownerHandle != pcb->handle )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
      {
         fprintf(TRACE,"@%10d(%3d) destroy message box \"%s\" (handle %d) failure\n",
            S16clock,pcb->handle,resources[handle].name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR010;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
   {
      fprintf(TRACE,"@%10d(%3d) destroy message box \"%s\" (handle %d), contains %d messages",
         S16clock,pcb->handle,resources[handle].name,handle,messageBox->Q.size);
      TraceQ(&messageBox->Q); fprintf(TRACE,"\n");
      fflush(TRACE);
   }
   //ENDTRACE-ing

// destruct queue
   DestructQ(&messageBox->Q);
// deallocate handle
   DeallocateResourceHandle(pcb,handle);
   free(messageBox);

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_SEND_MESSAGE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of message buffer to be sent
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid, R0 contains
      SVC_ERROR013 when the job making the request is not the owner of the message box 
      sent from; R0 contains SVC_ERROR004 when the message exceeds the capacity
      of the message box sent to; otherwise R0 contains SVC_OK
*/
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToWaitQNow(void *pcbR,void *pcb);
   bool AddMessageToMessageBoxQNow(void *messageR,void *message);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   BYTE HOB,LOB;
   int i;
   WORD LA = pcb->R[15];
   MESSAGE *message;

// dynamically-allocate message, make copy of entire message header
   *returnCode = SVC_OK;
   message = (MESSAGE *) malloc(sizeof(MESSAGE));
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+1),&LOB);
   message->header.length = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+2),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+3),&LOB);
   message->header.fromMessageBoxHandle = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+4),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+5),&LOB);
   message->header.toMessageBoxHandle = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+6),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+7),&LOB);
   message->header.priority = MAKEWORD(HOB,LOB);

// dynamically-allocate block and make copy of block
   message->block = (WORD *) malloc(message->header.length*sizeof(WORD));
   for (i = 0; i <= message->header.length-1; i++)
   {
      ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+8+2*i+0),&HOB);
      ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+8+2*i+1),&LOB);
      message->block[i] = MAKEWORD(HOB,LOB);
   }

// ensure handles in message are valid and specify existing message boxes
   if ( !IsValidHandleForResourceType(message->header.fromMessageBoxHandle,MESSAGEBOXES)
     || !IsValidHandleForResourceType(message->header.toMessageBoxHandle  ,MESSAGEBOXES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
      {
         fprintf(TRACE,"@%10d(%3d) send message priority = %2d from message box (handle %d) to message box (handle %d): ",
            S16clock,pcb->handle,message->header.priority,message->header.fromMessageBoxHandle,message->header.toMessageBoxHandle);
         DISPLAY_MESSAGE_FIELD()
         fprintf(TRACE,"invalid handle, ignored\n");
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      free(message->block);
      free(message);
   }
   else
   {
      MESSAGEBOX *fromMessageBox = (MESSAGEBOX *) resources[message->header.fromMessageBoxHandle].object;
      MESSAGEBOX *toMessageBox   = (MESSAGEBOX *) resources[message->header.toMessageBoxHandle  ].object;

      if ( !IsValidHandleForResourceType(fromMessageBox->ownerHandle,JOBS)
        || !IsValidHandleForResourceType(  toMessageBox->ownerHandle,JOBS) )
      {

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
         {
            fprintf(TRACE,"@%10d(%3d) send message priority = %2d from message box (handle %d) to message box (handle %d): ",
               S16clock,pcb->handle,message->header.priority,message->header.fromMessageBoxHandle,message->header.toMessageBoxHandle);
            DISPLAY_MESSAGE_FIELD()
            fprintf(TRACE,"invalid handle, ignored\n");
            fflush(TRACE);
         }
         //ENDTRACE-ing

         *returnCode = SVC_ERROR012;
      }
      else
      {
         PCB *fromPCB = (PCB *) resources[fromMessageBox->ownerHandle].object;
         PCB *toPCB   = (PCB *) resources[  toMessageBox->ownerHandle].object;

         if ( fromMessageBox->ownerHandle != pcb->handle )
         {

            //TRACE-ing
            if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
            {
               fprintf(TRACE,"@%10d(%3d) send message priority = %2d from message box (handle %d) to message box (handle %d): ",
                  S16clock,pcb->handle,message->header.priority,message->header.fromMessageBoxHandle,message->header.toMessageBoxHandle);
               DISPLAY_MESSAGE_FIELD()
               fprintf(TRACE,"contains error, ignored\n");
               fflush(TRACE);
            }
            //ENDTRACE-ing

            *returnCode = SVC_ERROR013;
         }
         else
         {
         // when the to-message-box is waiting for this message
            if ( toMessageBox->waiting )
            {
            // the to-message-box is no longer waiting
               toMessageBox->waiting = false;
            // update message wait time statistics for owner process of to-message-box
               toPCB->messageWaitTime += S16clock;
            /*
               write complete message (header and variable-length block fields)
                  into toPCB job beginning at logical address toMessageBox->pointerToRequestedMessage
            */
               LA = toMessageBox->pointerToRequestedMessage;
               WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+0),HIBYTE(message->header.length));
               WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+1),LOBYTE(message->header.length));
               WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+2),HIBYTE(message->header.fromMessageBoxHandle));
               WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+3),LOBYTE(message->header.fromMessageBoxHandle));
               WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+4),HIBYTE(message->header.toMessageBoxHandle));
               WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+5),LOBYTE(message->header.toMessageBoxHandle));
               WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+6),HIBYTE(message->header.priority));
               WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+7),LOBYTE(message->header.priority));
               for (i = 0; i <= message->header.length-1; i++)
               {
                  WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+8+2*i+0),HIBYTE(message->block[i]));
                  WriteDataLogicalMainMemory(toPCB->MMURegisters,(LA+8+2*i+1),LOBYTE(message->block[i]));
               }

               //TRACE-ing
               if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
               {
                  fprintf(TRACE,"@%10d(%3d) send message priority = %2d to message box \"%s\" (handle %d): ",
                     S16clock,pcb->handle,message->header.priority,
                     resources[message->header.toMessageBoxHandle].name,message->header.toMessageBoxHandle);
                  DISPLAY_MESSAGE_FIELD()
                  fprintf(TRACE,"received, unblocked\n");
                  fflush(TRACE);
               }
               //ENDTRACE-ing

            // free the dynamically-allocated message and its variable-length block field
               free(message->block);
               free(message);
            // add toPCB job PCB to the wait queue and update wait state/time statistics for toPCB job
               toPCB->takeOffWaitQTime = S16clock + SVCWaitTime(SVC_REQUEST_MESSAGE);
               AddObjectToQ(&waitQ,toPCB,AddPCBToWaitQNow);
               toPCB->waitStateTime -= S16clock;
               toPCB->waitStateCount++;
            }

         // when the to-message-box is not waiting for this message
            else
            {
               /*
                  add dynamically-allocated message to to-message-box message queue
                     when it does not exceed capacity of message queue
               */
               if ( (toMessageBox->capacity == 0) || (toMessageBox->Q.size < toMessageBox->capacity) )
               {
                  AddObjectToQ(&toMessageBox->Q,message,AddMessageToMessageBoxQNow);

                  //TRACE-ing
                  if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
                  {
                     fprintf(TRACE,"@%10d(%3d) send message priority = %2d to message box \"%s\" (handle %d): ",
                        S16clock,pcb->handle,message->header.priority,
                        resources[message->header.toMessageBoxHandle].name,message->header.toMessageBoxHandle);
                     DISPLAY_MESSAGE_FIELD()
                     fprintf(TRACE,"queued\n");
                     fflush(TRACE);
                  }
                  //ENDTRACE-ing

               }
               else
               {

                  //TRACE-ing
                  if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
                  {
                     fprintf(TRACE,"@%10d(%3d) send message priority = %2d from message box (handle %d) to message box (handle %d): ",
                        S16clock,pcb->handle,message->header.priority,message->header.fromMessageBoxHandle,message->header.toMessageBoxHandle);
                     DISPLAY_MESSAGE_FIELD()
                     fprintf(TRACE,"ignored\n");
                     fflush(TRACE);
                  }
                  //ENDTRACE-ing

                  *returnCode = SVC_ERROR004;
               }
            }
         }
      }
   }
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_REQUEST_MESSAGE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: handle of message box
      word   1: logical address-of buffer in requesting job address space into which
                the message is written
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; R0 contains
      SVC_ERROR013 when the job making the request is not the owner of the message box 
      requested from; otherwise R0 contains SVC_OK
*/
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   BYTE HOB,LOB;
   HANDLE handle;

   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+1),&LOB);
   handle = MAKEWORD(HOB,LOB);

// ensure handle is valid and specifies existing message box
   if ( !IsValidHandleForResourceType(handle,MESSAGEBOXES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
      {
         fprintf(TRACE,"@%10d(%3d) request message from message box (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
   }
   else
   {
      MESSAGEBOX *messageBox = (MESSAGEBOX *) resources[handle].object;

      if ( pcb->handle != messageBox->ownerHandle )
      {

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
         {
            fprintf(TRACE,"@%10d(%3d) request message from message box (handle %d) failure\n",
               S16clock,pcb->handle,handle);
            fflush(TRACE);
         }
         //ENDTRACE-ing

         *returnCode = SVC_ERROR013;
      }
      else
      {
      // when the message box queue is empty, then there is no message to satisfy the request
         if ( messageBox->Q.size == 0 )
         {
            PCB *pcb = (PCB *) resources[messageBox->ownerHandle].object;

         // store logical address-of message for future reference
            ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+2),&HOB);
            ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+3),&LOB);
            messageBox->pointerToRequestedMessage = MAKEWORD(HOB,LOB);
            messageBox->waiting = true;
         // update message wait time/count statistics for job
            pcb->messageWaitTime -= S16clock;
            pcb->messageWaitCount++;
         /*
            don't put the requesting process on the wait queue because it
               blocks while it waits for a message to be sent to it
         */
            *returnCode = SVC_OK;
            *shouldAddPCBToWaitQ = false;

            //TRACE-ing
            if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
            {
               fprintf(TRACE,"@%10d(%3d) request message from message box (handle %d), but queue is empty so process blocked\n",
                  S16clock,pcb->handle,handle);
               fflush(TRACE);
            }
            //ENDTRACE-ing

         }

      // when the message box queue is not empty, take a message off the head of the queue
         else
         {
            MESSAGE *message;
            WORD LA;
            int i;

            RemoveObjectFromQ(&messageBox->Q,1,(void **) &message);
         /*
            write complete message (header and variable-length block field) into
               requesting job beginning at logical address contained in parameter block
         */
            ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+2),&HOB);
            ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+3),&LOB);
            LA = MAKEWORD(HOB,LOB);
            WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+0),HIBYTE(message->header.length));
            WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+1),LOBYTE(message->header.length));
            WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+2),HIBYTE(message->header.fromMessageBoxHandle));
            WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+3),LOBYTE(message->header.fromMessageBoxHandle));
            WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+4),HIBYTE(message->header.toMessageBoxHandle));
            WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+5),LOBYTE(message->header.toMessageBoxHandle));
            WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+6),HIBYTE(message->header.priority));
            WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+7),LOBYTE(message->header.priority));
            for (i = 0; i <= message->header.length-1; i++)
            {
               WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+8+2*i+0),HIBYTE(message->block[i]));
               WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (LA+8+2*i+1),LOBYTE(message->block[i]));
            }

            //TRACE-ing
            if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
            {
               fprintf(TRACE,"@%10d(%3d) request message priority = %2d by message box \"%s\" (handle %d) received from message box \"%s\" (handle %d): ",
                  S16clock,pcb->handle,message->header.priority,
                  resources[message->header.toMessageBoxHandle].name,message->header.toMessageBoxHandle,
                  resources[message->header.fromMessageBoxHandle].name,message->header.fromMessageBoxHandle);
               DISPLAY_MESSAGE_FIELD()
               fprintf(TRACE,"\n");
               fflush(TRACE);
            }
            //ENDTRACE-ing

         // free the dynamically-allocated message and its variable-length block field
            free(message->block);
            free(message);
            *returnCode = SVC_OK;
            *shouldAddPCBToWaitQ = true;
         }
      }
   }
}

//--------------------------------------------------
void DoSVC_GET_MESSAGE_COUNT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the message box resource whose 
       size is requested
   OUT parameters: R15 contains the size of the list of pending messages; R0 contains
      SVC_ERROR012 when the handle is not valid; otherwise R0 contains SVC_OK
*/
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

   if ( !IsValidHandleForResourceType(handle,MESSAGEBOXES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
      {
         fprintf(TRACE,"@%10d(%3d) get message count for message box (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   MESSAGEBOX *messageBox = (MESSAGEBOX *) resources[handle].object;

   pcb->R[15] = (WORD) messageBox->Q.size;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MESSAGEBOXES )
   {
      fprintf(TRACE,"@%10d(%3d) get message count for message box \"%s\" (handle %d) pending messages = %d\n",
         S16clock,pcb->handle,resources[handle].name,handle,pcb->R[15]);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_CREATE_SEMAPHORE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: HOB/LOB initial value of semaphore
      word   1: name[0] character 1 of n-character, NUL-terminated string
      word   2: name[1]
             .       .
      n+1: name[n] = NULL
   OUT parameters: R15 contains the handle of the semaphore (when allocated);
      R0 contains SVC_ERROR003 when the initial value is out of range; R0 contains
      SVC_ERROR008 when the creation failed; otherwise R0 contains SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void AllocateResourceHandle(PCB *pcb,const char name[],RESOURCETYPE type,bool *allocated,HANDLE *handle);
   void ConstructQ(QUEUE *queue,char *name,bool containsPCBs,void (*TraceQNODEObject)(void *object));
   void TraceSemaphoreQObject(void *object);
   
   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   HANDLE handle;
   bool allocated;
   int value;
   BYTE HOB,LOB;

// get semaphore initial value and semaphore name from parameter block and ensure (value >= 1)
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 1),&LOB);
   value = MAKEWORD(HOB,LOB);
   GetNULTerminatedString(pcb,(WORD) (pcb->R[15]+2),name);
   if ( ((int) value) < 0)
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) create semaphore \"%s\", value = %d failure\n",
            S16clock,pcb->handle,name,value);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR003;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// allocate SEMAPHORES resource handle
   AllocateResourceHandle(pcb,name,SEMAPHORES,&allocated,&handle);
   if ( !allocated )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) create semaphore \"%s\", value = %d failure\n",
            S16clock,pcb->handle,name,value);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR008;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// dynamically-allocate and construct semaphore
   SEMAPHORE *semaphore = (SEMAPHORE *) malloc(sizeof(SEMAPHORE));
   char *QName = (char *) malloc(sizeof(char)*(strlen(name)+strlen(" (semaphore)")+1));

   semaphore->handle = handle;
   semaphore->ownerHandle = pcb->handle;
   semaphore->value = value;
   sprintf(QName,"%s (semaphore)",name);
   ConstructQ(&semaphore->Q,QName,true,TraceSemaphoreQObject);
   free(QName);

// initialize resource with newly-constructed semaphore
   resources[handle].object = semaphore;
// return handle in R15
   pcb->R[15] = handle;

//***DEADLOCK#1 cannot be used because SEMAPHORE-s are several-instance resources!
//***DEADLOCK#2
   AVAILABLE[handle] = value;
   for (int n = 1; n <= MAXIMUM_RESOURCES; n++)
   {
      ALLOCATION[n][handle] = 0;
      REQUEST[n][handle] = 0;
   }
//***ENDDEADLOCK#2

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_SEMAPHORES )
   {
      fprintf(TRACE,"@%10d(%3d) create semaphore \"%s\" (handle %d), value = %d\n",
         S16clock,pcb->handle,name,handle,value);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_DESTROY_SEMAPHORE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the semaphore resource to be destroyed
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; R0 contains
      SVC_ERROR010 when the job making the request is not the owner; 
      otherwise R0 contains SVC_OK
*/
   void TraceQ(QUEUE *queue);
   void DestructQ(QUEUE *queue);
   void DeallocateResourceHandle(PCB *pcb,HANDLE handle);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

/*
   ensure handle is valid, specifies an existing semaphore, and destroy request is
      made by creator/owner of the resource
*/
   if ( !IsValidHandleForResourceType(handle,SEMAPHORES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) destroy semaphore (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }
   SEMAPHORE *semaphore = (SEMAPHORE *) resources[handle].object;

   if ( semaphore->ownerHandle != pcb->handle )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) destroy semaphore \"%s\" (handle %d) failure\n",
            S16clock,pcb->handle,resources[handle].name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR010;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_SEMAPHORES )
   {
      fprintf(TRACE,"@%10d(%3d) destroy semaphore \"%s\" (handle %d), %d waiting processes",
         S16clock,pcb->handle,resources[handle].name,handle,semaphore->Q.size);
      TraceQ(&semaphore->Q); fprintf(TRACE,"\n");
      fflush(TRACE);
   }
   //ENDTRACE-ing

// destruct queue
   DestructQ(&semaphore->Q);
// deallocate handle
   DeallocateResourceHandle(pcb,handle);
   free(semaphore);

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_WAIT_SEMAPHORE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the semaphore resource
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToSemaphoreQNow(void *pcbR,void *pcb);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing semaphore
   if ( !IsValidHandleForResourceType(handle,SEMAPHORES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) wait semaphore (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   SEMAPHORE *semaphore = (SEMAPHORE *) resources[handle].object;

   semaphore->value--;
   if ( semaphore->value < 0 )
   {

//***DEADLOCK#1 cannot be used because SEMAPHORE-s are several-instance resources!
//***DEADLOCK#2
// unsuccessful WAIT of SEMAPHORE handle-m by process handle-p
      REQUEST[pcb->handle][handle] += 1;
//***ENDDEADLOCK#2

   /*
      add the running process PCB to semaphore queue of PCBs (it will
      be blocked until it is signal()-ed off of semaphore queue and onto
      the wait queue)
   */
      AddObjectToQ(&semaphore->Q,pcb,AddPCBToSemaphoreQNow);
   // update semaphore wait time/count statistics for pcb
      pcb->semaphoreWaitTime -= S16clock;
      pcb->semaphoreWaitCount++;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) wait semaphore \"%s\" (handle %d), job \"%s\" (handle %d) blocked\n",
            S16clock,pcb->handle,resources[handle].name,handle,resources[pcb->handle].name,pcb->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *shouldAddPCBToWaitQ = false;
   }
   else
   {

//***DEADLOCK#1 cannot be used because SEMAPHORE-s are several-instance resources!
//***DEADLOCK#2
// successful WAIT of SEMAPHORE handle-m by process handle-p
     AVAILABLE[handle] -= 1;
     ALLOCATION[pcb->handle][handle] += 1;
     REQUEST[pcb->handle][handle] = 0;
//***ENDDEADLOCK#2

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) wait semaphore \"%s\" (handle %d), job \"%s\" (handle %d) not blocked\n",
            S16clock,pcb->handle,resources[handle].name,handle,resources[pcb->handle].name,pcb->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing
   
      *shouldAddPCBToWaitQ = true;
   }
   *returnCode = SVC_OK;
}

//--------------------------------------------------
void DoSVC_SIGNAL_SEMAPHORE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the semaphore resource
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToWaitQNow(void *pcbR,void *pcb);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing semaphore
   if ( !IsValidHandleForResourceType(handle,SEMAPHORES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) signal semaphore (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   SEMAPHORE *semaphore = (SEMAPHORE *) resources[handle].object;

   semaphore->value++;
   if ( semaphore->value <= 0 )
   {
   // remove process P PCB from semaphore queue
      PCB *P;

      RemoveObjectFromQ(&semaphore->Q,1,(void **) &P);
   // update semaphore wait time statistics for P
      P->semaphoreWaitTime += S16clock;
   /*
      add P PCB to the wait queue and update wait
      state/time statistics for P
   */
      P->takeOffWaitQTime = S16clock + SVCWaitTime(SVC_SIGNAL_SEMAPHORE);
      AddObjectToQ(&waitQ,P,AddPCBToWaitQNow);
      P->waitStateTime -= S16clock;
      P->waitStateCount++;

//***DEADLOCK#1 cannot be used because SEMAPHORE-s are several-instance resources!
//***DEADLOCK#2
// successful SIGNAL of SEMAPHORE handle-m by process handle-p
      AVAILABLE[handle] += 1;
      ALLOCATION[pcb->handle][handle] -= 1;
// successful WAIT of SEMAPHORE handle-m by process handle-p
      AVAILABLE[handle] -= 1;
      ALLOCATION[P->handle][handle] += 1;
      REQUEST[P->handle][handle] -= 1;
//***ENDDEADLOCK#2

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) signal semaphore \"%s\" (handle %d), job \"%s\" (handle %d) unblocked\n",
            S16clock,pcb->handle,resources[handle].name,handle,resources[P->handle].name,P->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }
   else
   {

//***DEADLOCK#1 cannot be used because SEMAPHORE-s are several-instance resources!
//***DEADLOCK#2
// successful SIGNAL of SEMAPHORE handle-m by process handle-p
      AVAILABLE[handle] += 1;
      ALLOCATION[pcb->handle][handle] -= 1;
//***ENDDEADLOCK#2

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_SEMAPHORES )
      {
         fprintf(TRACE,"@%10d(%3d) signal semaphore \"%s\" (handle %d)\n",
            S16clock,pcb->handle,resources[handle].name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_CREATE_MUTEX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the NUL-terminated
      string that is the name of the mutex resource to be created
   OUT parameters: R15 contains the handle of the mutex (when allocated);
      R0 contains SVC_ERROR008 when the creation failed;
      otherwise R0 contains SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void AllocateResourceHandle(PCB *pcb,const char name[],RESOURCETYPE type,bool *allocated,HANDLE *handle);
   void ConstructQ(QUEUE *queue,char *name,bool containsPCBs,void (*TraceQNODEObject)(void *object));
   void TraceSemaphoreQObject(void *object);
   
   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   HANDLE handle;
   bool allocated;

// get mutex name from parameter block, then allocate MUTEXES resource handle
   GetNULTerminatedString(pcb,(WORD) (pcb->R[15]),name);
   AllocateResourceHandle(pcb,name,MUTEXES,&allocated,&handle);
   if ( !allocated )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MUTEXES )
      {
         fprintf(TRACE,"@%10d(%3d) create mutex \"%s\" failure\n",
            S16clock,pcb->handle,name);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR008;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// dynamically-allocate and construct mutex
   SEMAPHORE *mutex = (SEMAPHORE *) malloc(sizeof(SEMAPHORE));
   char *QName = (char *) malloc(sizeof(char)*(strlen(name)+strlen(" (mutex)")+1));

   mutex->handle = handle;
   mutex->ownerHandle = pcb->handle;
   mutex->value = 1;
   sprintf(QName,"%s (mutex)",name);
   ConstructQ(&mutex->Q,QName,true,TraceSemaphoreQObject);
   free(QName);

// initialize resource with newly-constructed mutex
   resources[handle].object = mutex;
// return handle in R15
   pcb->R[15] = handle;

//***DEADLOCK#2
   AVAILABLE[handle] = mutex->value;
   for (int n = 1; n <= MAXIMUM_RESOURCES; n++)
   {
      ALLOCATION[n][handle] = 0;
      REQUEST[n][handle] = 0;
   }
//***ENDDEADLOCK#2

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MUTEXES )
   {
      fprintf(TRACE,"@%10d(%3d) create mutex \"%s\" (handle %d), value = 1 (unlocked)\n",
         S16clock,pcb->handle,name,handle,mutex->value);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_DESTROY_MUTEX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the mutex resource to be destroyed
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; R0 contains
      SVC_ERROR010 when the job making the request is not the owner; 
      otherwise R0 contains SVC_OK
*/
   void TraceQ(QUEUE *queue);
   void DestructQ(QUEUE *queue);
   void DeallocateResourceHandle(PCB *pcb,HANDLE handle);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

/*
   ensure handle is valid, specifies an existing mutex, and destroy request is
      made by creator/owner of the resource
*/
   if ( !IsValidHandleForResourceType(handle,MUTEXES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MUTEXES )
      {
         fprintf(TRACE,"@%10d(%3d) destroy mutex (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   SEMAPHORE *mutex = (SEMAPHORE *) resources[handle].object;

   if ( mutex->ownerHandle != pcb->handle )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MUTEXES )
      {
         fprintf(TRACE,"@%10d(%3d) destroy mutex \"%s\" (handle %d) failure\n",
            S16clock,pcb->handle,resources[handle].name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR010;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MUTEXES )
   {
      fprintf(TRACE,"@%10d(%3d) destroy mutex \"%s\" (handle %d), %d locked processes",
         S16clock,pcb->handle,resources[handle].name,handle,mutex->Q.size);
      TraceQ(&mutex->Q); fprintf(TRACE,"\n");
      fflush(TRACE);
   }
   //ENDTRACE-ing

// destruct queue
   DestructQ(&mutex->Q);
// deallocate handle
   DeallocateResourceHandle(pcb,handle);
   free(mutex);

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_LOCK_MUTEX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the mutex resource
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToSemaphoreQNow(void *pcbR,void *pcb);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing mutex
   if ( !IsValidHandleForResourceType(handle,MUTEXES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MUTEXES )
      {
         fprintf(TRACE,"@%10d(%3d) lock mutex (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   SEMAPHORE *mutex = (SEMAPHORE *) resources[handle].object;

   if ( mutex->value == 1 )
   {
      mutex->value = 0;

//***DEADLOCK#1
// successful LOCK of MUTEX handle-m by process handle-p, RAG[handle-m][handle-p] = true
      RAG[handle][pcb->handle] = true;
//***ENDDEADLOCK#1

//***DEADLOCK#2
// successful LOCK of MUTEX handle-m by process handle-p
      AVAILABLE[handle] -= 1;
      ALLOCATION[pcb->handle][handle] += 1;
      REQUEST[pcb->handle][handle] -= 1;
//***ENDDEADLOCK#2

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MUTEXES )
      {
         fprintf(TRACE,"@%10d(%3d) lock mutex \"%s\" (handle %d), job \"%s\" (handle %d) not blocked\n",
            S16clock,pcb->handle,resources[handle].name,handle,resources[pcb->handle].name,pcb->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *shouldAddPCBToWaitQ = true;
   }
   else
   {
   /*
      Add the running process PCB to mutex queue of PCBs (it will
      be blocked until it is unlock()-ed off of mutex queue and onto
      the wait queue)
   */

//***DEADLOCK#1
// unsuccessful LOCK of MUTEX handle-m by process handle-p, RAG[handle-p][handle-m] = true
      RAG[pcb->handle][handle] = true;
//***ENDDEADLOCK#1
   
//***DEADLOCK#2
// unsuccessful LOCK of MUTEX handle-m by process handle-p
      REQUEST[pcb->handle][handle] += 1;
//***ENDDEADLOCK#2

      AddObjectToQ(&mutex->Q,pcb,AddPCBToSemaphoreQNow);
   // update mutex wait time/count statistics for pcb
      pcb->mutexWaitTime -= S16clock;
      pcb->mutexWaitCount++;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MUTEXES )
      {
         fprintf(TRACE,"@%10d(%3d) lock mutex \"%s\" (handle %d), job \"%s\" (handle %d) blocked\n",
            S16clock,pcb->handle,resources[handle].name,handle,resources[pcb->handle].name,pcb->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *shouldAddPCBToWaitQ = false;
   }
   *returnCode = SVC_OK;
}

//--------------------------------------------------
void DoSVC_UNLOCK_MUTEX(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the mutex resource
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToWaitQNow(void *pcbR,void *pcb);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing semaphore
   if ( !IsValidHandleForResourceType(handle,MUTEXES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MUTEXES )
      {
         fprintf(TRACE,"@%10d(%3d) unlock mutex (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   SEMAPHORE *mutex = (SEMAPHORE *) resources[handle].object;

// when mutex queue size is 0, then set mutex value to 1
   if ( mutex->Q.size == 0 )
   {
      mutex->value = 1;

//***DEADLOCK#1
// successful UNLOCK of MUTEX handle-m by process handle-p, RAG[handle-m][handle-p] = false
      RAG[handle][pcb->handle] = false;
//***ENDDEADLOCK#1

//***DEADLOCK#2
// successful UNLOCK of MUTEX handle-m by process handle-p
      AVAILABLE[handle] += 1;
      ALLOCATION[pcb->handle][handle] -= 1;
//***ENDDEADLOCK#2

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MUTEXES )
      {
         fprintf(TRACE,"@%10d(%3d) unlock mutex \"%s\" (handle %d)\n",
            S16clock,pcb->handle,resources[handle].name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }
   else
   {
      PCB *P;

   // remove process P PCB from M queue
      RemoveObjectFromQ(&mutex->Q,1,(void **) &P);
   // update mutex wait time statistics for P
      P->mutexWaitTime += S16clock;
   // add P PCB to the wait queue and update wait state/time statistics for P
      P->takeOffWaitQTime = S16clock + SVCWaitTime(SVC_LOCK_MUTEX);
      AddObjectToQ(&waitQ,P,AddPCBToWaitQNow);
      P->waitStateTime -= S16clock;
      P->waitStateCount++;

//***DEADLOCK#1
// successful UNLOCK of MUTEX handle-m by process handle-p, RAG[handle-m][handle-p] = false
      RAG[handle][pcb->handle] = false;
// successful LOCK of MUTEX handle-m by process handle-p, RAG[handle-m][handle-p] = true and RAG[handle-p][handle-m] = false
      RAG[handle][P->handle] = true;
      RAG[P->handle][handle] = false;
//***ENDDEADLOCK#1

//***DEADLOCK#2
// successful UNLOCK of MUTEX handle-m by process handle-p
      AVAILABLE[handle] += 1;
      ALLOCATION[pcb->handle][handle] -= 1;
// successful LOCK of MUTEX handle-m by process handle-p
      AVAILABLE[handle] -= 1;
      ALLOCATION[P->handle][handle] += 1;
      REQUEST[P->handle][handle] -= 1;
//***ENDDEADLOCK#2

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MUTEXES )
      {
         fprintf(TRACE,"@%10d(%3d) unlock mutex \"%s\" (handle %d), add job \"%s\" (handle %d) to waitQ\n",
            S16clock,pcb->handle,resources[handle].name,handle,resources[P->handle].name,P->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }
   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_CREATE_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the NUL-terminated
      string that is the name of the event resource to be created
   OUT parameters: R15 contains the handle of the event (when allocated);
      R0 contains SVC_ERROR008 when the creation failed;
      otherwise R0 contains SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void AllocateResourceHandle(PCB *pcb,const char name[],RESOURCETYPE type,bool *allocated,HANDLE *handle);
   void ConstructQ(QUEUE *queue,char *name,bool containsPCBs,void (*TraceQNODEObject)(void *object));
   void TraceEventQObject(void *object);
   
   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   HANDLE handle;
   bool allocated;

// get event name from parameter block, then allocate EVENTS resource handle
   GetNULTerminatedString(pcb,(WORD) (pcb->R[15]),name);
   AllocateResourceHandle(pcb,name,EVENTS,&allocated,&handle);
   if ( !allocated )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) create event \"%s\" failure\n",
            S16clock,pcb->handle,name);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR008;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// dynamically-allocate and construct event
   EVENT *event = (EVENT *) malloc(sizeof(EVENT));
   char *QName = (char *) malloc(sizeof(char)*(strlen(name)+strlen(" (event)")+1));

   event->handle = handle;
   event->ownerHandle = pcb->handle;
   event->missedSignals = 0;
   sprintf(QName,"%s (event)",name);
   ConstructQ(&event->Q,QName,true,TraceEventQObject);
   free(QName);

// initialize resource with newly-constructed event
   resources[handle].object = event;
// return handle in R15
   pcb->R[15] = handle;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_EVENTS )
   {
      fprintf(TRACE,"@%10d(%3d) create event \"%s\" (handle %d)\n",
         S16clock,pcb->handle,name,handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_DESTROY_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the event resource to be destroyed
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; R0 contains
      SVC_ERROR010 when the job making the request is not the owner;
      otherwise R0 contains SVC_OK
*/
   void TraceQ(QUEUE *queue);
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToWaitQNow(void *pcbR,void *pcb);
   void DestructQ(QUEUE *queue);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);
   void DeallocateResourceHandle(PCB *pcb,HANDLE handle);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

/*
   ensure handle is valid, specifies an existing event, and destroy request is
      made by creator/owner of the resource
*/
   if ( !IsValidHandleForResourceType(handle,EVENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) destroy event (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   EVENT *event = (EVENT *) resources[handle].object;

   if ( event->ownerHandle != pcb->handle )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) destroy event \"%s\" (handle %d) failure\n",
            S16clock,pcb->handle,resources[handle].name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR010;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_EVENTS )
   {
      fprintf(TRACE,"@%10d(%3d) destroy event \"%s\" (handle %d), processes waiting = %d",
         S16clock,pcb->handle,resources[handle].name,handle,event->Q.size);
      TraceQ(&event->Q); fprintf(TRACE,"\n");
      fflush(TRACE);
   }
   //ENDTRACE-ing

/*
   remove all processes waiting on event queue, place them on
      the waitQ and update wait/count statistics for waitQ and event queue
*/
   while ( event->Q.size > 0 )
   {
      PCB *P;

      RemoveObjectFromQ(&event->Q,1,(void **) &P);
      P->eventWaitTime += S16clock;
      P->takeOffWaitQTime = S16clock + SVCWaitTime(SVC_DESTROY_EVENT);
      AddObjectToQ(&waitQ,P,AddPCBToWaitQNow);
      P->waitStateTime -= S16clock;
      P->waitStateCount++;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) job \"%s\" (handle %d) removed from event queue, added to waitQ\n",
            S16clock,pcb->handle,resources[P->handle].name,P->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }

// destruct queue
   DestructQ(&event->Q);
// deallocate handle
   DeallocateResourceHandle(pcb,handle);
   free(event);

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_SIGNAL_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the event resource to be signal-ed
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToWaitQNow(void *pcbR,void *pcb);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing event
   if ( !IsValidHandleForResourceType(handle,EVENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) signal event (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   EVENT *event = (EVENT *) resources[handle].object;

   if ( event->Q.size == 0 )
   {
      event->missedSignals++;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) signal event \"%s\" (handle %d): ",
            S16clock,pcb->handle,resources[handle].name,handle);
         fprintf(TRACE,"missed signals = %d\n",event->missedSignals);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }
   else
   {
   /*
      remove one process waiting on event queue, place it on
         the waitQ and update wait/count statistics for waitQ and event queue
   */
      PCB *P;

      RemoveObjectFromQ(&event->Q,1,(void **) &P);
      P->eventWaitTime += S16clock;
      P->takeOffWaitQTime = S16clock + SVCWaitTime(SVC_SIGNAL_EVENT);
      AddObjectToQ(&waitQ,P,AddPCBToWaitQNow);
      P->waitStateTime -= S16clock;
      P->waitStateCount++;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) signal event \"%s\" (handle %d): ",
            S16clock,pcb->handle,resources[handle].name,handle);
         fprintf(TRACE,"job \"%s\" (handle %d) removed from event \"%s\" (handle %d) queue, added to waitQ\n",
            resources[P->handle].name,P->handle,resources[handle].name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_SIGNALALL_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the event resource to be signalAll-ed
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToWaitQNow(void *pcbR,void *pcb);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing event
   if ( !IsValidHandleForResourceType(handle,EVENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) signalAll event (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   EVENT *event = (EVENT *) resources[handle].object;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_EVENTS )
   {
      fprintf(TRACE,"@%10d(%3d) signalAll event \"%s\" (handle %d): ",
         S16clock,pcb->handle,resources[handle].name,handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   if ( event->Q.size == 0 )
   {
      event->missedSignals++;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"missed signals = %d\n",event->missedSignals);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   }
   else
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"remove %d jobs from event queue\n",event->Q.size);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   /*
      remove all processes waiting on event queue, place them on
         the waitQ and update wait/count statistics for waitQ and
         event queue
   */
      while ( event->Q.size > 0 )
      {
         PCB *P;

         RemoveObjectFromQ(&event->Q,1,(void **) &P);
         P->eventWaitTime += S16clock;
         P->takeOffWaitQTime = S16clock + SVCWaitTime(SVC_SIGNALALL_EVENT);
         AddObjectToQ(&waitQ,P,AddPCBToWaitQNow);
         P->waitStateTime -= S16clock;
         P->waitStateCount++;

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_EVENTS )
         {
            fprintf(TRACE,"@%10d(%3d) add job \"%s\" (handle %d) to waitQ\n",
               S16clock,pcb->handle,resources[P->handle].name,P->handle);
            fflush(TRACE);
         }
         //ENDTRACE-ing

      }
   }
   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_WAIT_EVENT(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the event resource to be wait-ed
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid;
      otherwise R0 contains SVC_OK
*/
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   bool AddPCBToEventQNow(void *pcbR,void *pcb);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing event
   if ( !IsValidHandleForResourceType(handle,EVENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) wait event (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   EVENT *event = (EVENT *) resources[handle].object;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_EVENTS )
   {
      fprintf(TRACE,"@%10d(%3d) wait event \"%s\" (handle %d): ",
         S16clock,pcb->handle,resources[handle].name,handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   if ( event->missedSignals > 0 )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"missed signals = %d, job \"%s\" (handle %d) not put on event queue\n",
            event->missedSignals,resources[pcb->handle].name,pcb->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      event->missedSignals--;

      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = true;
   }
   else
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"missed signals = 0, job \"%s\" (handle %d) put on event queue\n",
            resources[pcb->handle].name,pcb->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   /*
      put requesting PCB on event queue (it will be blocked until
         it is Signal()-ed off of queue and onto the wait queue)
   */
      AddObjectToQ(&event->Q,pcb,AddPCBToEventQNow);
   // update event wait time/count statistics for requester.
      pcb->eventWaitTime -= S16clock;
      pcb->eventWaitCount++;

      *returnCode = SVC_OK;
      *shouldAddPCBToWaitQ = false;
   }
}

//--------------------------------------------------
void DoSVC_GET_EVENT_QUEUE_SIZE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the event resource
   OUT parameters: R15 contains the number of processes waiting on the event queue;
      R0 contains SVC_ERROR012 when the handle is not valid; 
      otherwise R0 contains SVC_OK
*/
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing event
   if ( !IsValidHandleForResourceType(handle,EVENTS) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_EVENTS )
      {
         fprintf(TRACE,"@%10d(%3d) get event queue size (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   EVENT *event = (EVENT *) resources[handle].object;

   pcb->R[15] = event->Q.size;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_EVENTS )
   {
      fprintf(TRACE,"@%10d(%3d) get event queue size \"%s\" (handle %d), size = %d\n",
         S16clock,pcb->handle,resources[handle].name,handle,event->Q.size);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_CREATE_PIPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: HOB/LOB of pipe capacity
      word   1: name[0] character 1 of n-character, NUL-terminated string
      word   2: name[1]
             .       .
      word n+1: name[n] = NULL
   OUT parameters: R15 contains the handle of the pipe (when allocated);
      R0 contains SVC_ERROR007 when the capacity is out of range; R0 contains 
      SVC_ERROR008 when the creation failed, otherwise R0 contains SVC_OK
*/
   void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[]);
   void AllocateResourceHandle(PCB *pcb,const char name[],RESOURCETYPE type,bool *allocated,HANDLE *handle);

   char name[MEMORY_PAGE_SIZE_IN_BYTES+1];
   HANDLE handle;
   bool allocated;
   int capacity;
   BYTE HOB,LOB;

// get capacity and pipe name from parameter block
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+1),&LOB);
   capacity = MAKEWORD(HOB,LOB);
   GetNULTerminatedString(pcb,(WORD) (pcb->R[15]+2),name);
   if ( !((1 <= capacity) && (capacity <= MEMORY_PAGE_SIZE_IN_BYTES)) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PIPES )
      {
         fprintf(TRACE,"@%10d(%3d) create pipe \"%s\", capacity = %d failure\n",
            S16clock,pcb->handle,name,capacity);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR007;
      *shouldAddPCBToWaitQ = true;
      return;
   }
   
// allocate PIPES resource handle
   AllocateResourceHandle(pcb,name,PIPES,&allocated,&handle);
   if ( !allocated )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PIPES )
      {
         fprintf(TRACE,"@%10d(%3d) create pipe \"%s\", capacity = %d failure\n",
            S16clock,pcb->handle,name,capacity);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR008;
      *shouldAddPCBToWaitQ = true;
      return;
   }

// dynamically-allocate and initialize pipe
   PIPE *pipe = (PIPE *) malloc(sizeof(PIPE));

   pipe->ownerHandle = pcb->handle;
   pipe->capacity = capacity;
   pipe->head = -1;
   pipe->tail = -1;
   pipe->size = 0;
   pipe->buffer = (WORD *) malloc(sizeof(WORD)*capacity);
// initialize resource with newly-constructed event
   resources[handle].object = pipe;
// return handle in R15
   pcb->R[15] = handle;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PIPES )
   {
      fprintf(TRACE,"@%10d(%3d) create pipe \"%s\" (handle %d), capacity = %d\n",
         S16clock,pcb->handle,name,handle,capacity);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_DESTROY_PIPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the pipe resource to be destroyed
   OUT parameters: R0 contains SVC_ERROR012 when the handle is not valid; R0 contains
      SVC_ERROR010 when the job making the request is not the owner; 
      otherwise R0 contains SVC_OK
*/
   void DeallocateResourceHandle(PCB *pcb,HANDLE handle);
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

/*
   ensure handle is valid, specifies an existing pipe, and destroy request is
      made by creator/owner of the resource
*/
   if ( !IsValidHandleForResourceType(handle,PIPES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PIPES )
      {
         fprintf(TRACE,"@%10d(%3d) destroy pipe (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   PIPE *pipe = (PIPE *) resources[handle].object;

   if ( pipe->ownerHandle != pcb->handle )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PIPES )
      {
         fprintf(TRACE,"@%10d(%3d) destroy pipe \"%s\" (handle %d) failure\n",
            S16clock,pcb->handle,resources[handle].name,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR010;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PIPES )
   {
      fprintf(TRACE,"@%10d(%3d) destroy pipe \"%s\" (handle %d), size = %d\n",
         S16clock,pcb->handle,resources[handle].name,handle,pipe->size);
      fflush(TRACE);
   }
   //ENDTRACE-ing

// deallocate buffer
   free(pipe->buffer);
// deallocate handle
   DeallocateResourceHandle(pcb,handle);
   free(pipe);

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_READ_PIPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: HOB/LOB of handle
      word   1: HOB/LOB of logical address-of buffer
      word   2: HOB/LOB of count
   OUT parameters: R15 contains the count of the number of words read; R0 contains
      SVC_ERROR012 when the handle is not valid; otherwise R0 contains SVC_OK
*/
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle;
   WORD bufferAddress;
   int count;
   BYTE HOB,LOB;

// get handle, bufferAddress and transfer count from parameter block
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 1),&LOB);
   handle = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 2),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 3),&LOB);
   bufferAddress = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 4),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 5),&LOB);
   count = MAKEWORD(HOB,LOB);

// ensure handle is valid and specifies an existing pipe
   if ( !IsValidHandleForResourceType(handle,PIPES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PIPES )
      {
         fprintf(TRACE,"@%10d(%3d) read pipe (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   PIPE *pipe = (PIPE *) resources[handle].object;
   WORD buffer[MEMORY_PAGE_SIZE_IN_BYTES];

/*
   transfer only size words when requested transfer count is 
      greater than the size of pipe buffer
*/
   if ( count > pipe->size )
      count = pipe->size;
/*
   transfer count words from pipe buffer to process buffer indirectly through local buffer
      to facilitate output of the transfered words to the TRACE file
      (*Note* pipe head always points-to the last buffer element read)
*/
   for (int i = 0; i <= count-1; i++)
   {
      if ( pipe->head == pipe->capacity-1)
         pipe->head = 0;
      else
         pipe->head++;
      pipe->size--;
      buffer[i] = pipe->buffer[pipe->head];
      HOB = HIBYTE(buffer[i]);
      LOB = LOBYTE(buffer[i]);
      WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (bufferAddress+2*i+0),HOB);
      WriteDataLogicalMainMemory(pcb->MMURegisters,(WORD) (bufferAddress+2*i+1),LOB);
   }

// return transfer count in R15
   pcb->R[15] = count;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PIPES )
   {
      fprintf(TRACE,"@%10d(%3d) read pipe \"%s\" (handle %d) %d words, size = %4d: ",
         S16clock,pcb->handle,resources[handle].name,handle,count,pipe->size);
      DISPLAY_PIPE_BUFFER()
      fprintf(TRACE,"\n");
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_WRITE_PIPE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains logical address-of the following parameter block
      word   0: HOB/LOB of handle
      word   1: HOB/LOB of logical address-of buffer
      word   2: HOB/LOB of count
   OUT parameters: R15 contains the count of the number of words written; R0 contains
      SVC_ERROR012 when the handle is not valid; otherwise R0 contains SVC_OK
*/
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle;
   WORD bufferAddress;
   int count;
   BYTE HOB,LOB;

// get handle, bufferAddress and transfer count from parameter block
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 0),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 1),&LOB);
   handle = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 2),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 3),&LOB);
   bufferAddress = MAKEWORD(HOB,LOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 4),&HOB);
   ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(pcb->R[15]+ 5),&LOB);
   count = MAKEWORD(HOB,LOB);

// ensure handle is valid and specifies an existing pipe
   if ( !IsValidHandleForResourceType(handle,PIPES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PIPES )
      {
         fprintf(TRACE,"@%10d(%3d) write pipe (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,count,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   PIPE *pipe = (PIPE *) resources[handle].object;
   WORD buffer[MEMORY_PAGE_SIZE_IN_BYTES];
   int freeSpace,discardedCount;

/*
   freeSpace in pipe buffer = (capacity-size); transfer only freeSpace words 
      when requested transfer count is greater than freeSpace
*/
   freeSpace = pipe->capacity-pipe->size;
   if ( count > freeSpace )
   {
      discardedCount = count-freeSpace;
      count = freeSpace;
   }
   else
      discardedCount = 0;
/*
   tranfer count words from process buffer to pipe buffer indirectly through
      local buffer to facilitate output of the transfered words to the TRACE file
      (*Note* pipe tail always points-to the last buffer element written)
*/
   for (int i = 0; i <= count-1; i++)
   {
      ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (bufferAddress+2*i+0),&HOB);
      ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) (bufferAddress+2*i+1),&LOB);
      buffer[i] = MAKEWORD(HOB,LOB);
      if ( pipe->tail == pipe->capacity-1)
         pipe->tail = 0;
      else
         pipe->tail++;
      pipe->buffer[pipe->tail] = buffer[i];
      pipe->size++;
   }
// return transfer count in R15
   pcb->R[15] = count;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PIPES )
   {
      fprintf(TRACE,"@%10d(%3d) write pipe \"%s\" (handle %d) %d words (%d words discarded), size = %4d: ",
         S16clock,pcb->handle,resources[handle].name,handle,count,discardedCount,pipe->size);
      DISPLAY_PIPE_BUFFER()
      fprintf(TRACE,"\n");
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//--------------------------------------------------
void DoSVC_GET_PIPE_SIZE(PCB *pcb,SVC_RETURN_CODE *returnCode,bool *shouldAddPCBToWaitQ)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: R15 contains the handle of the pipe resource whose size is requested
   OUT parameters: R15 contains the size of the pipe; R0 contains SVC_ERROR012 when the
      handle is not valid; otherwise R0 contains SVC_OK
*/
   bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type);

   HANDLE handle = pcb->R[15];

// ensure handle is valid and specifies an existing pipe
   if ( !IsValidHandleForResourceType(handle,PIPES) )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PIPES )
      {
         fprintf(TRACE,"@%10d(%3d) get pipe size (handle %d) failure, invalid handle\n",
            S16clock,pcb->handle,handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      *returnCode = SVC_ERROR012;
      *shouldAddPCBToWaitQ = true;
      return;
   }

   PIPE *pipe = (PIPE *) resources[handle].object;

// get the size of the buffer and return in R15
   pcb->R[15] = pipe->size;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PIPES )
   {
      fprintf(TRACE,"@%10d(%3d) get pipe size \"%s\" (handle %d), size = %d\n",
         S16clock,pcb->handle,resources[handle].name,handle,pipe->size);
      fflush(TRACE);
   }
   //ENDTRACE-ing

   *returnCode = SVC_OK;
   *shouldAddPCBToWaitQ = true;
}

//=============================================================================
// S16OS memory management
//=============================================================================
//--------------------------------------------------
void S16OS_AllocateMemoryPage(const PCB *pcb,WORD *physicalPage,const char situation[])
//--------------------------------------------------
{
   bool found;
   
// Use sequential search to find a free page
   *physicalPage = 0;
   found = false;
   while ( (*physicalPage <= MEMORY_PAGES-1) && !found )
   {
      if ( memoryPages[*physicalPage] )
         *physicalPage += 1;
      else
         found = true;
   }

// when no free page exists, display error message, and abort S16 execution
   if ( !found )
   {

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_MEMORY_ALLOCATION )
      {
         fprintf(TRACE,"@%10d(%3d) allocate physical page failure (%s)\n",
            S16clock,pcb->handle,situation);
         fflush(TRACE);
      }
      //ENDTRACE-ing

      ProcessWarningOrError(S16OSERROR,"Unrecoverable memory allocation error");
   }

// otherwise, mark physicalPage as "allocated"
   memoryPages[*physicalPage] = true;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MEMORY_ALLOCATION )
   {
      fprintf(TRACE,"@%10d(%3d) allocated physical page %d (%s)\n",
         S16clock,pcb->handle,*physicalPage,situation);
      fflush(TRACE);
   }
   //ENDTRACE-ing

}

//--------------------------------------------------
void S16OS_DeallocateMemoryPage(const PCB *pcb,WORD physicalPage,const char situation[])
//--------------------------------------------------
{
// 
// mark physicalPage as "not-allocated"
   memoryPages[physicalPage] = false;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_MEMORY_ALLOCATION )
   {
      fprintf(TRACE,"@%10d(%3d) deallocated physical page %d (%s)\n",
         S16clock,pcb->handle,physicalPage,situation);
      fflush(TRACE);
   }
   //ENDTRACE-ing

}

//=============================================================================
// S16OS Process-next-clock-tick
//=============================================================================
//--------------------------------------------------
void S16OS_ProcessNextClockTick()
//--------------------------------------------------
{
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void S16OS_CreateNewJob(PCB *pcb);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *objectR,void *object));
   void *ObjectFromQ(QUEUE *queue,int index);
   bool AddPCBToReadyQNow(void *pcbR,void *pcb);
   bool AddPCBToWaitQNow(void *pcbR,void *pcb);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);
   void S16OS_CPUScheduler();
   void S16OS_Dispatcher();
   void DoDeadlockDetection(bool *deadlockDetected);
   void ProcessWarningOrError(const int type,const char message[]);

   bool deadlockDetected;

// remove jobs from job queue that have takeOffJobQTime <= S16clock
   while ( (jobQ.size > 0) &&
           (((PCB *)(jobQ.head->object))->takeOffJobQTime <= S16clock) )
   {
      PCB *pcb;

   // remove PCB from job queue
      RemoveObjectFromQ(&jobQ,1,(void **) &pcb);
   // create new S16OS job
      S16OS_CreateNewJob(pcb);
   // add PCB to ready queue
      pcb->FCFSTime = S16clock;
      AddObjectToQ(&readyQ,pcb,AddPCBToReadyQNow);
   // update ready state time statistics for PCB
      pcb->readyStateTime -= S16clock;
      pcb->readyStateCount++;
   }

// remove jobs from wait queue that have takeOffWaitQTime <= S16clock
   while ( (waitQ.size > 0) &&
           (((PCB *)(waitQ.head->object))->takeOffWaitQTime <= S16clock) )
   {
      PCB *pcb;

   // remove PCB from wait queue
      RemoveObjectFromQ(&waitQ,1,(void **) &pcb);
   // add PCB to ready queue
      pcb->FCFSTime = S16clock;
      AddObjectToQ(&readyQ,pcb,AddPCBToReadyQNow);
   // update wait and ready state time statistics for PCB
      pcb->waitStateTime += S16clock;
      pcb->readyStateTime -= S16clock;
      pcb->readyStateCount++;
   }

// remove jobs from sleep queue that have takeOffSleepQTime <= S16clock
   while ( (sleepQ.size > 0) &&
           (((PCB *)(sleepQ.head->object))->takeOffSleepQTime <= S16clock) )
   {
      PCB *pcb;

   // remove PCB from sleep queue
      RemoveObjectFromQ(&sleepQ,1,(void **) &pcb);

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
      {
         fprintf(TRACE,"@%10d(%3d) process removed from sleepQ\n",S16clock,pcb->handle);
         fflush(TRACE);
      }
      //ENDTRACE-ing

   // add PCB to wait queue
      pcb->takeOffWaitQTime = S16clock + SVCWaitTime(SVC_SLEEP_PROCESS);
      AddObjectToQ(&waitQ,pcb,AddPCBToWaitQNow);
   // update sleep and wait state time statistics for PCB
      pcb->sleepStateTime += S16clock;
      pcb->waitStateTime -= S16clock;
      pcb->waitStateCount++;
   }

// remove jobs from join queue when child thread joined has terminated (not isActive)
   int index = 1;
   while ( index <= joinQ.size )
   {
      PCB *pcb;

   // "peek at" PCB on join queue
      pcb = (PCB *) ObjectFromQ(&joinQ,index);

   // find child thread handle in child thread list
      int index2 = 1;
      bool found = false;
      CHILDTHREAD *childThread;
      while ( !found && (index2 <= pcb->childThreadQ.size) )
      {
      // "peek at" child thread queue index-th CHILDTHREAD record
         childThread = (CHILDTHREAD *) ObjectFromQ(&pcb->childThreadQ,index2);
         if ( pcb->handleOfChildThreadJoined == childThread->handle )
            found = true;
         else
            index2++;
      }
      if ( !found ) // ***THIS CONDITION SHOULD NEVER OCCUR!!!***
      {
         ProcessWarningOrError(S16OSWARNING,"Parent process joined non-existent child thread");
         index++;
      }
      else if ( childThread->isActive )
      {
         index++;
      }
      else
      {
         RemoveObjectFromQ(&joinQ,index,(void **) &pcb);
      // add PCB to wait queue
         pcb->takeOffWaitQTime = S16clock + SVCWaitTime(SVC_JOIN_CHILD_THREAD);
         AddObjectToQ(&waitQ,pcb,AddPCBToWaitQNow);
      // update join and wait state time statistics for PCB
         pcb->joinStateTime += S16clock;
         pcb->waitStateTime -= S16clock;
         pcb->waitStateCount++;
      }
   }

// do deadlock detection
   DoDeadlockDetection(&deadlockDetected);
   if ( deadlockDetected ) ProcessWarningOrError(S16OSERROR,"Deadlock detected");

/*
   When the CPU state is IDLE, check the ready queue. When the ready queue is
      not empty, then schedule and dispatch the next job on the ready
      queue (which changes the CPU state to RUN).
*/
   if ( (GetCPUState() == IDLE) && (readyQ.size > 0) )
   {
      S16OS_CPUScheduler();
      S16OS_Dispatcher();
   }
}

//=============================================================================
// S16OS Deadlock detection
//=============================================================================
//--------------------------------------------------
void DoDeadlockDetection(bool *deadlockDetected)
//--------------------------------------------------
{
   void DoDeadlockDetectionMethod1(bool *deadlockDetected);
   void DoDeadlockDetectionMethod2(bool *deadlockDetected);

   switch ( DEADLOCK_DETECTION_ALGORITHM )
   {
      case DEADLOCK_DETECTION_METHOD1:
         DoDeadlockDetectionMethod1(deadlockDetected);
         break;
      case DEADLOCK_DETECTION_METHOD2:
         DoDeadlockDetectionMethod2(deadlockDetected);
         break;
      case NO_DEADLOCK_DETECTION:
         *deadlockDetected = false;
   }
}

//--------------------------------------------------
void DoDeadlockDetectionMethod1(bool *deadlockDetected)
//--------------------------------------------------
{
   void TraceStateOfSystemQueues();

   static bool R[ MAXIMUM_RESOURCES+1 ][ MAXIMUM_RESOURCES+1 ];

// initialize (R)eachable[][] with RAG[1:resourcesCount][1:resourcesCount]
   for (int i = 1; i <= resourcesCount; i++)
      for (int j = 1; j <= resourcesCount; j++)
         R[i][j] = RAG[i][j];

// use Warshall's algorithm to compute transitive closure of (R)eachable[][]
   for (int k = 1; k <= resourcesCount; k++)
      for (int i = 1; i <= resourcesCount; i++)
         for (int j = 1; j <= resourcesCount; j++)
            R[i][j] = R[i][j] || (R[i][k] && R[k][j]);

   *deadlockDetected = false;
   for (HANDLE handle = 1; handle <= resourcesCount; handle++) 
      *deadlockDetected = *deadlockDetected || R[handle][handle];

   if ( *deadlockDetected )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_DEADLOCK_DETECTION )
      {
         fprintf(TRACE,"\n***Trace of Method #1 data structures when deadlock discovered***\n");
         fprintf(TRACE,"RAG\n");
         for (int i = 1; i <= resourcesCount; i++)
         {
            fprintf(TRACE,"%3d: ",i);
            for (int j = 1; j <= resourcesCount; j++)
               fprintf(TRACE,"%c",( (RAG[i][j]) ? 'T' : '-' ));
            fprintf(TRACE,"   \"%s\"\n",resources[i].name);
         }
         fflush(TRACE);
   
         fprintf(TRACE,"R\n");
         for (int i = 1; i <= resourcesCount; i++)
         {
            fprintf(TRACE,"%3d: ",i);
            for (int j = 1; j <= resourcesCount; j++)
               fprintf(TRACE,"%c",( (R[i][j]) ? 'T' : '-' ));
            fprintf(TRACE,"\n");
         }
         fprintf(TRACE,"***End trace of Method #1 data structures***\n");
         fflush(TRACE);
         
         TraceStateOfSystemQueues();
      }
      //ENDTRACE-ing

      fprintf(TRACE,"\n***Trace of Method #1 deadlocked resources\n");
      fprintf(TRACE,"@%10d Deadlock cycle = ",S16clock);
      for (HANDLE handle = 1; handle <= resourcesCount; handle++)
         if ( R[handle][handle] ) fprintf(TRACE,"(%3d)\"%s\" ",handle,resources[handle].name);
      fprintf(TRACE,"\n***End trace of Method #1 deadlocked resources\n\n");
      fflush(TRACE);
   }
}
//--------------------------------------------------
void DoDeadlockDetectionMethod2(bool *deadlockDetected)
//--------------------------------------------------
{
   void TraceStateOfSystemQueues();

   int m,n;
   HANDLE mHandles[resourcesCount+1],nHandles[resourcesCount+1];
   int available[resourcesCount+1];
   int allocation[resourcesCount+1][resourcesCount+1];
   int request[resourcesCount+1][resourcesCount+1];

// gather JOBS handles and non-JOBS handles
   m = 0; 
   n = 0;
   for (HANDLE handle = 1; handle <= resourcesCount; handle++)
      if ( resources[handle].allocated )
         if ( resources[handle].type == JOBS )
            nHandles[++n] = handle;
         else
            mHandles[++m] = handle;

// build data structures
   for (int j = 1; j <= m; j++)
      available[j] = AVAILABLE[mHandles[j]];
   for (int i = 1; i <= n; i++)
      for (int j = 1; j <= m; j++)
      {
         allocation[i][j] = ALLOCATION[nHandles[i]][mHandles[j]];
         request[i][j] = REQUEST[nHandles[i]][mHandles[j]];
      }

/*
   Taken from Problem18.c: The four-step, banker's-algorithm-like deadlock-detection algorithm that is
      applicable to a resource-allocation system with multiple instances of each resource type.
      (taken from Section 7.6.2 of 9th edition of Silberschatz's textbook "Operating System Concepts")
*/
   int work[resourcesCount+1];
   bool finish[resourcesCount+1];
   bool indexIFound;

   //-------
   // Step 1
   //-------
   for (int j = 1; j <= m; j++)
      work[j] = available[j];
   for (int i = 1; i <= n; i++)
   {
      finish[i] = true;
      for (int j = 1; j <= m; j++)
         finish[i] = finish[i] && ( allocation[i][j] == 0 );
   }
   do
   {
      //-------
      // Step 2
      //-------
      int i = 1;

      do
      {
         indexIFound = !finish[i];
         for (int j = 1; j <= m; j++)
            indexIFound = indexIFound && ( request[i][j] <= work[j] );
         if ( !indexIFound ) i++;
      } while ( (i <= n) && !indexIFound );
   
      //-------
      // Step 3
      //-------
      if ( indexIFound )
      {
         for (int j = 1; j <= m; j++)
            work[j] += allocation[i][j];               
         finish[i] = true;
      }
   } while ( indexIFound );
   
   //-------
   // Step 4
   //-------
   *deadlockDetected = false;
   for (int i = 1; i <= n; i++)
      *deadlockDetected = *deadlockDetected || !finish[i];

   if ( *deadlockDetected )
   {
      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_DEADLOCK_DETECTION )
      {
         fprintf(TRACE,"\n***Trace of Method #2 data structures when deadlock discovered***\n");
         fprintf(TRACE,"m = %3d, n = %3d\n",m,n);
         fprintf(TRACE,"\nmHandles\n");
         fprintf(TRACE,  "--------\n");
         for (int j = 1; j <= m; j++)
            fprintf(TRACE,"(%3d) \"%s\"\n",mHandles[j],resources[mHandles[j]].name);
      
         fprintf(TRACE,"\nnHandles\n");
         fprintf(TRACE,  "--------\n");
         for (int i = 1; i <= n; i++)
            fprintf(TRACE,"(%3d) \"%s\"\n",nHandles[i],resources[nHandles[i]].name);
      
         fprintf(TRACE,"\navailable\n");
         fprintf(TRACE,  "---------\n");
         for (int j = 1; j <= m; j++)
            fprintf(TRACE,"%3d ",available[j]);
         fprintf(TRACE,"\n");
      
         fprintf(TRACE,"\nallocation\n");
         fprintf(TRACE,  "----------\n");
         for (int i = 1; i <= n; i++)
         {
            for (int j = 1; j <= m; j++)
               fprintf(TRACE,"%3d ",allocation[i][j]);
            fprintf(TRACE,"| %3d (%3d)\n",i,nHandles[i]);
         }
      
         fprintf(TRACE,"\nrequest\n");
         fprintf(TRACE,  "-------\n");
         for (int i = 1; i <= n; i++)
         {
            for (int j = 1; j <= m; j++)
               fprintf(TRACE,"%3d ",request[i][j]);
            fprintf(TRACE,"| %3d (%3d)\n",i,nHandles[i]);
         }
         fprintf(TRACE,"***End trace of Method #2 data structures***\n");
         fflush(TRACE);
   
         TraceStateOfSystemQueues();
      }
      //ENDTRACE-ing

      fprintf(TRACE,"\n***Trace of Method #2 deadlocked processes\n");
      fprintf(TRACE,"@%10d Deadlock cycle = ",S16clock);
      for (int i = 1; i <= n; i++)
         if ( !finish[i] ) fprintf(TRACE,"(%3d)\"%s\" ",nHandles[i],resources[nHandles[i]].name);
      fprintf(TRACE,"\n***End trace of Method #2 deadlocked processes\n\n");
      fflush(TRACE);
   }
}

//=============================================================================
// S16OS QUEUE management
//=============================================================================
//--------------------------------------------------
void ConstructQ(QUEUE *queue,char *name,bool containsPCBs,void (*TraceQNODEObject)(void *object))
//--------------------------------------------------
{

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_QUEUES )
   {
      fprintf(TRACE,"@%10d      construct empty queue \"%s\"\n",S16clock,name); fflush(TRACE);
   }
   //ENDTRACE-ing

   queue->size = 0;
   queue->name = (char *) malloc(sizeof(char)*(strlen(name)+1));
   strcpy(queue->name,name);
   queue->containsPCBs = containsPCBs;
   queue->TraceQNODEObject = TraceQNODEObject;
   queue->head = NULL;
   queue->tail = NULL;
}

//--------------------------------------------------
void DestructQ(QUEUE *queue)
//--------------------------------------------------
{
   void TraceQ(QUEUE *queue);
   
   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_QUEUES )
   {
      if ( queue->size == 0 )
         fprintf(TRACE,"@%10d      destruct empty queue \"%s\"\n",S16clock,queue->name);
      else
      {
         fprintf(TRACE,"@%10d      destruct non-empty queue",S16clock);
         TraceQ(queue); fprintf(TRACE,"\n");
      }
      fflush(TRACE);
   }
   //ENDTRACE-ing

   free(queue->name);
}

//--------------------------------------------------
void AddObjectToQ(QUEUE *queue,void *object,
                  bool (*AddObjectToQNow)(void *objectR,void *object))
//--------------------------------------------------
{
/*
   Traverse queue to determine nodeL (pointer-to the node that
   belongs to the immediate (L)eft of object node insertion point) and
   nodeR (pointer-to the node that belongs to the immediate (R)ight of object
   node insertion point). objectL/objectR is the object in the node pointed
   to by nodeL/nodeR.
*/
   void TraceQ(QUEUE *queue);
   
   QNODE *nodeL,*nodeR;
   void *objectL,*objectR;
   QNODE *node;

   node = (QNODE *) malloc(sizeof(QNODE));
   node->object = object;
   nodeL = NULL;
   objectL = NULL;
   nodeR = (queue->size == 0) ? NULL : queue->head;
   objectR = (nodeR == NULL) ? NULL : nodeR->object;
   while ( !AddObjectToQNow(objectR,object) )
   {
      nodeL = nodeR;
      objectL = nodeL->object;
      nodeR = (QNODE *) nodeR->FLink;
      objectR = (nodeR == NULL) ? NULL : nodeR->object;
   }
   if      ( (nodeL == NULL) && (nodeR == NULL) ) // queue is empty
   {
      node->FLink = NULL;
      queue->head = queue->tail = node;
   }
   else if ( (nodeL == NULL) && (nodeR != NULL) ) // insert at head of queue
   {
      node->FLink = (struct QNODE *) nodeR;
      queue->head = node;
   }
   else if ( (nodeL != NULL) && (nodeR == NULL) ) // insert at tail of queue
   {
      node->FLink = NULL;
      nodeL->FLink = (struct QNODE *) node;
      queue->tail = node;
   }
   else if ( (nodeL != NULL) && (nodeR != NULL) ) // insert in "middle" of queue
   {
      node->FLink = (struct QNODE *) nodeR;
      nodeL->FLink = (struct QNODE *) node;
   }
   queue->size++;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_QUEUES )
   {
      if ( queue->containsPCBs )
         fprintf(TRACE,"@%10d(%3d) added to",S16clock,((PCB *)object)->handle);
      else
         fprintf(TRACE,"@%10d      node added to",S16clock);
      TraceQ(queue); fprintf(TRACE,"\n");
      fflush(TRACE);
   }
   //ENDTRACE-ing

}

//--------------------------------------------------
void RemoveObjectFromQ(QUEUE *queue,int index,void **object)
//--------------------------------------------------
{
/*
   Remove node at index position in queue, return object contained in the removed
      node and return the removed node to heap.
*/
   void TraceQ(QUEUE *queue);

   QNODE *nodeL = NULL;
   QNODE *node = queue->head;
   int i = 1;

   assert( index <= queue->size );

   while ( i < index )
   {
      nodeL = node;
      node = (QNODE *) node->FLink;
      i++;
   }
   *object = node->object;
   if      ( queue->size == 1 )
      queue->head = queue->tail = NULL;
   else if ( index == 1 )
      queue->head = (QNODE *) node->FLink;
   else if ( index == queue->size )
   {
      queue->tail = nodeL;
      nodeL->FLink = NULL;
   }
   else
      nodeL->FLink = node->FLink;
   queue->size--;
   free(node);

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_QUEUES )
   {
      if ( queue->containsPCBs )
         fprintf(TRACE,"@%10d(%3d) removed from",S16clock,((PCB *)(*object))->handle);
      else
         fprintf(TRACE,"@%10d      node removed from",S16clock);
      TraceQ(queue); fprintf(TRACE,"\n");
      fflush(TRACE);
   }
   //ENDTRACE-ing

}

//--------------------------------------------------
void *ObjectFromQ(QUEUE *queue,int index)
//--------------------------------------------------
{
   QNODE *node = queue->head;

   assert( index <= queue->size );

   while ( index > 1 )
   {
      node = (QNODE *) node->FLink;
      index--;
   }
   return( node->object );
}

//--------------------------------------------------
bool AddPCBToJobQNow(void *pcbR,void *pcb)
//--------------------------------------------------
{
/*
   Add PCB to job queue when either
      1. At end of list, that is, (pcbR == NULL).
      2. The PCB that pcbR points to must be to the immediate right of the PCB
         that pcb points to that is, (pcb->takeOffJobQTime < pcbR->takeOffJobQTime).
*/
   bool r;

   if ( (PCB *) pcbR == NULL )
      r = true;
   else if ( ((PCB *) pcb)->takeOffJobQTime < ((PCB *) pcbR)->takeOffJobQTime )
      r = true;
   else
      r = false;
   return( r );
}

//--------------------------------------------------
bool AddPCBToReadyQNow(void *pcbR,void *pcb)
//--------------------------------------------------
{
   bool r;

   switch ( CPU_SCHEDULER )
   {
   // First Come, First Serve (FCFS): always add PCB to tail of ready queue (pcbR == NULL)
      case FCFS_CPU_SCHEDULER:
              r =( (PCB *) pcbR == NULL );
              break;
   /*
      pcb points to the PCB whose placement on the readyQ is trying to be ascertained
         and pcbR points to the PCB already on the readyQ which is immediately to the
         right of the position currently being considered for the PCB pcb points to.

      Add PCB pointed to by pcb to readyQ in priority order when any of the following
         three conditions is true
         1. Always add when (pcbR == NULL). This condition occurs only when the position
            being considered is the one beyond the tail of the readyQ.
         2. When (pcb->priority < pcbR->priority). Remember, MINIMUM_PRIORITY is 10
            and MAXIMUM_PRIORITY is 1, so the smallest priority value (which is the highest
            priority) should be at the head of the readyQ and the largest priority value
            (which is the smallest priority) should be at the tail of the readyQ.
         3. When ((pcb->priority == pcbR->priority) && (pcb->FCFSTime < pcbR->FCFSTime)) because
            PCBs with equal priority-s must be in FCFS order.
   */
      case PRIORITY_CPU_SCHEDULER:
           {
              if      ( (PCB *) pcbR == NULL )
                 r = true;
              else if ( ((PCB *) pcb)->priority  < ((PCB *) pcbR)->priority )
                 r = true;
              else if ( ((PCB *) pcb)->priority == ((PCB *) pcbR)->priority )
                 r = ( ((PCB *) pcb)->FCFSTime < ((PCB *) pcbR)->FCFSTime );
              else
                 r = false;
           }
           break;
   /*
      pcb points to the PCB whose placement on the readyQ is trying to be ascertained
         and pcbR points to the PCB already on the readyQ which is immediately to the
         right of the position currently being considered for the PCB pcb points to

      Add PCB pointed to by pcb to readyQ in tau order when any of the following
         three conditions is true
         1. Always add when (pcbR == NULL). This condition occurs only when the position
            being considered is the one beyond the tail of the readyQ.
         2. When (pcb->tau < pcbR->tau). Remember, for SJF short-term CPU scheduling, the
            value of tau represents the length of the next expected CPU burst, so the
            tau value should be at the head of the readyQ and the largest tau value
            should be at the tail of the readyQ.
         3. When ((pcb->tau == pcbR->tau) && (pcb->FCFSTime < pcbR->FCFSTime)) because
            PCBs with equal tau-s must be in FCFS order.
   */
      case SJF_CPU_SCHEDULER:
           {
              if      ( (PCB *) pcbR == NULL )
                 r = true;
              else if ( ((PCB *) pcb)->tau  < ((PCB *) pcbR)->tau )
                 r = true;
              else if ( ((PCB *) pcb)->tau == ((PCB *) pcbR)->tau )
                 r = ( ((PCB *) pcb)->FCFSTime < ((PCB *) pcbR)->FCFSTime );
              else
                 r = false;
           }
           break;
      default:
           ProcessWarningOrError(S16OSWARNING,"Invalid CPU scheduler");
           r = ( (PCB *) pcbR == NULL );
           break;
   }
   return( r );
}

//--------------------------------------------------
bool AddPCBToWaitQNow(void *pcbR,void *pcb)
//--------------------------------------------------
{
/*
   Add PCB to wait queue when either
      1. At end of list, that is, (pcbR == NULL).
      2. The PCB that pcbR points to must be to the immediate right of the PCB
         that pcb points to that is, (pcb->takeOffWaitQTime < pcbR->takeOffWaitQTime).
*/
   bool r;

   if ( (PCB *) pcbR == NULL )
      r = true;
   else if ( ((PCB *)pcb)->takeOffWaitQTime < ((PCB *)pcbR)->takeOffWaitQTime )
      r = true;
   else
      r = false;
   return( r );
}

//--------------------------------------------------
bool AddPCBToJoinQNow(void *pcbR,void *pcb)
//--------------------------------------------------
{
// always add PCB to tail of join queue (pcbR == NULL)
   return( (PCB *) pcbR == NULL );
}

//--------------------------------------------------
bool AddPCBToSuspendedQNow(void *pcbR,void *pcb)
//--------------------------------------------------
{
// always add PCB to tail of suspended queue (pcbR == NULL)
   return( (PCB *) pcbR == NULL );
}

//--------------------------------------------------
bool AddPCBToSleepQNow(void *pcbR,void *pcb)
//--------------------------------------------------
{
/*
   Add PCB to sleep queue when either
      1. At end of list, that is, (pcbR == NULL).
      2. The PCB that pcbR points to must be to the immediate right of the PCB
         that pcb points to that is, (pcb->takeOffSleepQTime < pcbR->takeOffSleepQTime).
*/
   bool r;

   if ( (PCB *) pcbR == NULL )
      r = true;
   else if ( ((PCB *)pcb)->takeOffSleepQTime < ((PCB *)pcbR)->takeOffSleepQTime )
      r = true;
   else
      r = false;
   return( r );
}

//--------------------------------------------------
bool AddPCBToSemaphoreQNow(void *pcbR,void *pcb)
//--------------------------------------------------
{
// always add PCB to tail of semaphore queue (pcbR == NULL)
   return( (PCB *) pcbR == NULL );
}

//--------------------------------------------------
bool AddPCBToEventQNow(void *pcbR,void *pcb)
//--------------------------------------------------
{
// always add PCB to tail of event list (pcbR == NULL)
   return( (PCB *) pcbR == NULL );
}

//--------------------------------------------------
bool AddSignalToSignalsQNow(void *signalR,void *signal)
//--------------------------------------------------
{
// always add SIGNAL to tail of signals list (signalR == NULL)
   return( (SIGNAL *) signalR == NULL );
}

//--------------------------------------------------
bool AddDiskIOToDiskIOQNow(void *diskIOR,void *diskIO)
//--------------------------------------------------
{
   bool r;

   switch ( DISKIOQ_SCHEDULER )
   {
// always add disk IO request *diskIO to tail of diskIOQ (diskIOR == NULL)
      case FCFS_DISKIOQ_SCHEDULER:
              r = ( (DISKIO *) diskIOR == NULL);
              break;
      case some_other_diskIOQ_scheduler2:
      case some_other_diskIOQ_scheduler3:
      default:
              ProcessWarningOrError(S16OSWARNING,"Invalid disk IO queue scheduler");
              r = ( (DISKIO *)diskIOR == NULL );
   }
   return( r );
}

//--------------------------------------------------
bool AddResourceWaitToResourcesWaitQNow(void *resourceWaitR,void *resourceWait)
//--------------------------------------------------
{
// always add RESOURCEWAIT to tail of resources wait list (resourceWaitR == NULL)
   return( (RESOURCEWAIT *) resourceWaitR == NULL );
}

//--------------------------------------------------
bool AddMessageToMessageBoxQNow(void *messageR,void *message)
//--------------------------------------------------
{
   bool r;

   switch ( MQ_SCHEDULER )
   {
// always add message MESSAGE to tail of messageQ (messageR == NULL)
      case FCFS_MQ_SCHEDULER:
              r = ( (MESSAGE *) messageR == NULL );
              break;
      case some_other_MQ_scheduler2: //***UNUSED
      case some_other_MQ_scheduler3: //***UNUSED
      default:
              ProcessWarningOrError(S16OSWARNING,"Invalid message queue scheduler");
              r = ( (MESSAGE *) messageR == NULL );
              break;
    }
    return( r );
}

//--------------------------------------------------
bool AddChildThreadToChildThreadQNow(void *childThreadR,void *childThread)
//--------------------------------------------------
{
// always add CHILDTHREAD to tail of child thread list (childThreadR == NULL)
   return( (CHILDTHREAD *) childThreadR == NULL );
}

//--------------------------------------------------
void TraceQ(QUEUE *queue)
//--------------------------------------------------
{
/*
   XX...XX = { XXX,XXX,...,XXX }
*/
   void *ObjectFromQ(QUEUE *queue,int index);
   
   fprintf(TRACE," \"%s\"",queue->name);
   if ( queue->size == 0 )
      fprintf(TRACE," = { empty }");
   else
   {
      int index;

      fprintf(TRACE," = { ");
      for (index = 1; index <= queue->size; index++)
      {
         queue->TraceQNODEObject(ObjectFromQ(queue,index));
         fprintf(TRACE,"%c",((index < queue->size) ? ',' : ' '));
      }
      fprintf(TRACE,"}");
   }
}

//--------------------------------------------------
void TraceReadyQObject(void *object)
//--------------------------------------------------
{
   PCB *pcb = (PCB *) object;
   
   fprintf(TRACE,"%d(%d,%d,%d)",pcb->handle,pcb->FCFSTime,pcb->priority,pcb->tau);
}

//--------------------------------------------------
void TraceJobQObject(void *object)
//--------------------------------------------------
{
   PCB *pcb = (PCB *) object;
   
   fprintf(TRACE,"%d(%d)",pcb->handle,pcb->takeOffJobQTime);
}

//--------------------------------------------------
void TraceWaitQObject(void *object)
//--------------------------------------------------
{
   PCB *pcb = (PCB *) object;
   
   fprintf(TRACE,"%d(%d)",pcb->handle,pcb->takeOffWaitQTime);
}

//--------------------------------------------------
void TraceJoinQObject(void *object)
//--------------------------------------------------
{
   PCB *pcb = (PCB *) object;
   
   fprintf(TRACE,"%d(%d)",pcb->handle,pcb->handleOfChildThreadJoined);
}

//--------------------------------------------------
void TraceSignalsQObject(void *object)
//--------------------------------------------------
{
   SIGNAL *p = (SIGNAL *) object;
   
   fprintf(TRACE,"(%d->%d: %d)",p->senderProcessHandle,p->signaledProcessHandle,p->signal);
}

//--------------------------------------------------
void TraceSuspendedQObject(void *object)
//--------------------------------------------------
{
   PCB *pcb = (PCB *) object;
   
   fprintf(TRACE,"%d(%d)",pcb->handle,pcb->parentPCB->handle);
}

//--------------------------------------------------
void TraceSleepQObject(void *object)
//--------------------------------------------------
{
   PCB *pcb = (PCB *) object;
   
   fprintf(TRACE,"%d(%d)",pcb->handle,pcb->takeOffSleepQTime);
}

//--------------------------------------------------
void TraceResourcesWaitQObject(void *object)
//--------------------------------------------------
{
   RESOURCEWAIT *resourceWait = (RESOURCEWAIT *) object;
   
   fprintf(TRACE,"%d(%s)",resourceWait->pcb->handle,resourceWait->name);
}

//--------------------------------------------------
void TraceDiskIOQObject(void *object)
//--------------------------------------------------
{
   DISKIO *diskIO = (DISKIO *) object;
   
   fprintf(TRACE,"%d(%d,%d)",diskIO->pcb->handle,diskIO->command,diskIO->sectorAddress);
}

//--------------------------------------------------
void TraceSemaphoreQObject(void *object)
//--------------------------------------------------
{
   PCB *pcb = (PCB *) object;

   fprintf(TRACE,"%d",pcb->handle);
}

//--------------------------------------------------
void TraceMessageBoxQObject(void *object)
//--------------------------------------------------
{
   MESSAGE *message = (MESSAGE *) object;
   
   fprintf(TRACE,"(%d,%d,%d)",message->header.length,
      message->header.fromMessageBoxHandle,
      message->header.toMessageBoxHandle);
}

//--------------------------------------------------
void TraceEventQObject(void *object)
//--------------------------------------------------
{
   PCB *pcb = (PCB *) object;

   fprintf(TRACE,"%d",pcb->handle);
}

//--------------------------------------------------
void TraceChildThreadQObject(void *object)
//--------------------------------------------------
{
   CHILDTHREAD *p = (CHILDTHREAD *) object;
   
   fprintf(TRACE,"(%d '%c')",p->handle,((p->isActive) ? 'T' : 'F'));
}

//=============================================================================
// S16OS RESOURCE management
//=============================================================================
//--------------------------------------------------
void FindNameInResources(const char name[],bool *found,HANDLE *handle)
//--------------------------------------------------
{
   char uName1[MEMORY_PAGE_SIZE_IN_BYTES+1];

// do case-neutral search for name in resources[]
   for (int i = 0; i <= (int) strlen(name); i++)
      uName1[i] = toupper(name[i]);
   *handle = 1;
   *found  = false;
   while ( (*handle <= resourcesCount) && !*found )
   {
      if ( resources[*handle].allocated )
      {
         char uName2[MEMORY_PAGE_SIZE_IN_BYTES+1];

         for (int i = 0; i <= (int) strlen(resources[*handle].name); i++)
            uName2[i] = toupper(resources[*handle].name[i]);
         if ( strcmp(uName1,uName2) == 0 )
            *found = true;
         else
            *handle += 1;
      }
      else
         *handle += 1;
   }
}

//--------------------------------------------------
void AllocateResourceHandle(PCB *pcb,const char name[],RESOURCETYPE type,bool *allocated,HANDLE *handle)
//--------------------------------------------------
{
   void FindNameInResources(const char name[],bool *found,HANDLE *handle);
   void *ObjectFromQ(QUEUE *queue,int index);
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber);

   bool found;

// name *MUST NOT* be a null string
   if ( strlen(name) == 0 )
   {
      ProcessWarningOrError(S16OSWARNING,"Resource name is null string");
      *allocated = false;

      //TRACE-ing
      if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
      {
          if ( type == JOBS )
            fprintf(TRACE,"@%10d      resource name is null string\n",
               S16clock);
          else
            fprintf(TRACE,"@%10d(%3d) resource name is null string\n",
               S16clock,pcb->handle);
          fflush(TRACE);
      }
      //ENDTRACE-ing

   }
   else
   {
   // name must be globally-unique, therefore it *MUST NOT* already exist in resources
      FindNameInResources(name,&found,handle);
      if ( found )
      {
         ProcessWarningOrError(S16OSWARNING,"Resource name already in use");
         *allocated = false;

         //TRACE-ing
         if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
         {
            if ( type == JOBS )
               fprintf(TRACE,"@%10d      resource name \"%s\" already in use\n",
                  S16clock,name);
            else
               fprintf(TRACE,"@%10d(%3d) resource name \"%s\" already in use\n",
                  S16clock,pcb->handle,name);
            fflush(TRACE);
         }
         //ENDTRACE-ing

      }
      else
      {
         if ( resourcesCount == MAXIMUM_RESOURCES )
         {
            ProcessWarningOrError(S16OSWARNING,"Resources table is full");
            *allocated = false;

            //TRACE-ing
            if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
            {
               if ( type == JOBS )
                  fprintf(TRACE,"@%10d      Resources table is full, unable to allocate resource handle for \"%s\"\n",
                     S16clock,name);
               else
                  fprintf(TRACE,"@%10d(%3d) Resources table is full, unable to allocate resource handle for \"%s\"\n",
                     S16clock,pcb->handle,name);
               fflush(TRACE);
            }
            //ENDTRACE-ing

         }
         else
         {
         // allocate handle to uniquely-named resource
            resourcesCount++;
            *handle = resourcesCount;
            *allocated = true;
            if ( type != JOBS )
               pcb->allocatedResourcesCount++;
            resources[*handle].allocated = true;
            resources[*handle].type = type;
            resources[*handle].name = (char *) malloc(sizeof(char)*(strlen(name)+1));
            strcpy(resources[*handle].name,name);

            //TRACE-ing
            if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
            {
               if ( type == JOBS )
                  fprintf(TRACE,"@%10d      allocate JOBS resource \"%s\" (handle %d)\n",
                     S16clock,name,*handle);
               else
                  fprintf(TRACE,"@%10d(%3d) allocate resource \"%s\" (handle %d)\n",
                     S16clock,pcb->handle,name,*handle);
               fflush(TRACE);
            }
            //ENDTRACE-ing

//***DEADLOCK#1
         /*
            when resource handle is allocated, initialize 
               RAG row    RAG[handle][1:resourcesCount] = false 
               RAG column RAG[1:resourcesCount][handle] = false
         */
         for (int c = 1; c <= resourcesCount; c++) RAG[*handle][c] = false;
         for (int r = 1; r <= resourcesCount; r++) RAG[r][*handle] = false;
//***ENDDEADLOCK#1

//***DEADLOCK#2
   AVAILABLE[*handle] = 0;
   for (int n = 1; n <= MAXIMUM_RESOURCES; n++)
   {
      ALLOCATION[n][*handle] = 0;
      REQUEST[n][*handle] = 0;
   }
//***ENDDEADLOCK#2

         /*
            traverse resources wait queue to find any processes that are waiting for
               the handle just allocated
         */
            {
               char uName1[MEMORY_PAGE_SIZE_IN_BYTES+1];
               int index;

               for (int i = 0; i <= (int) strlen(name); i++)
                  uName1[i] = toupper(name[i]);
               index = 1;
               while ( index <= resourcesWaitQ.size )
               {
                  char uName2[MEMORY_PAGE_SIZE_IN_BYTES+1];
                  RESOURCEWAIT *resourceWait = (RESOURCEWAIT *) ObjectFromQ(&resourcesWaitQ,index);

                  for (int i = 0; i <= (int) strlen(resourceWait->name); i++)
                     uName2[i] = toupper(resourceWait->name[i]);
                  if ( strcmp(uName1,uName2) != 0 )
                     index++;
                  else
                  {
                     RemoveObjectFromQ(&resourcesWaitQ,index,(void **) &resourceWait);

                     //TRACE-ing
                     if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
                     {
                        fprintf(TRACE,"@%10d(%3d) process (handle %d) finished waiting for resource \"%s\" (handle %d)\n",
                           S16clock,pcb->handle,resourceWait->pcb->handle,name,*handle);
                        fflush(TRACE);
                     }
                     //ENDTRACE-ing


                  // add PCB onto the wait queue
                     resourceWait->pcb->takeOffWaitQTime = S16clock + SVCWaitTime(SVC_WAIT_FOR_RESOURCE_HANDLE);
                     AddObjectToQ(&waitQ,resourceWait->pcb,AddPCBToWaitQNow);
                  // update resources wait and wait state time statistics for PCB
                     resourceWait->pcb->resourcesWaitTime += S16clock;
                     resourceWait->pcb->waitStateTime -= S16clock;
                     resourceWait->pcb->waitStateCount++;
                  // return handle in R15
                     resourceWait->pcb->R[15] = *handle;
                  //-----------------------------------------------------------
                  // be good heap citizen (*Note* these memory leaks were not found until FA2022)!
                  //-----------------------------------------------------------
                     free(resourceWait->name);
                     free(resourceWait);
                  }
               }
            }
         }
      }
   }
}

//--------------------------------------------------
void DeallocateResourceHandle(PCB *pcb,HANDLE handle)
//--------------------------------------------------
{
// mark resource as "not allocated" and deallocate dynamically-allocated the name string

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_RESOURCE_MANAGEMENT )
   {
      if ( resources[handle].type == JOBS )
         fprintf(TRACE,"@%10d      deallocate JOBS resource \"%s\" (handle %d)\n",
            S16clock,resources[handle].name,handle);
      else
         fprintf(TRACE,"@%10d(%3d) deallocate resource \"%s\" (handle %d)\n",
            S16clock,pcb->handle,resources[handle].name,handle);
      fflush(TRACE);
   }
   //ENDTRACE-ing

//***DEADLOCK#1
/*
   when resource handle is deallocated, re-set   
      RAG row    RAG[handle][1:resourcesCount] = false 
      RAG column RAG[1:resourcesCount][handle] = false
*/
   for (int c = 1; c <= resourcesCount; c++) RAG[handle][c] = false;
   for (int r = 1; r <= resourcesCount; r++) RAG[r][handle] = false;
//***ENDDEADLOCK#1

//***DEADLOCK#2
   AVAILABLE[handle] = 0;
   for (int n = 1; n <= MAXIMUM_RESOURCES; n++)
   {
      ALLOCATION[n][handle] = 0;
      REQUEST[n][handle] = 0;
   }
//***ENDDEADLOCK#2

// handles are *NEVER* "recycled"
   if ( resources[handle].type != JOBS )
      pcb->deallocatedResourcesCount++;
   resources[handle].allocated = false;
   free(resources[handle].name);
   resources[handle].name = NULL;
}

//--------------------------------------------------
bool IsValidHandleForResourceType(HANDLE handle,RESOURCETYPE type)
//--------------------------------------------------
{
   bool r;

   if ( (1 <= handle) && (handle <= resourcesCount) )
      if ( resources[handle].allocated && (resources[handle].type == type) )
         r = true;
      else
         r = false;
   else
      r = false;
// a false return value represents an error that the caller *MUST* handle
   return( r );
}

//=============================================================================
// S16OS debugger
//=============================================================================
//--------------------------------------------------
void S16OS_Debugger()
//--------------------------------------------------
{
   void DoHCommand();
   void DoSDCommand();
   void DoDDCommand();
   void DoDSCommand();
   void DoDRCommand();

   char command[80+1];

   printf("DEBUG> ");
   while ( gets(command) != NULL )
   {
      char uCommand[80+1];

      for (int i = 0; i <= (int) strlen(command); i++)
         uCommand[i] = toupper(command[i]);
      if      (strcmp(uCommand,  "H") == 0 )
         DoHCommand();
      else if ( strcmp(uCommand,"SD") == 0 )
         DoSDCommand();
      else if ( strcmp(uCommand,"DD") == 0 )
         DoDDCommand();
      else if ( strcmp(uCommand,"DS") == 0 )
         DoDSCommand();
      else if ( strcmp(uCommand,"DR") == 0 )
         DoDRCommand();
      else
      {
         printf("Unknown command\n");
         DoHCommand();
      }
      printf("DEBUG> ");
   }
   clearerr(stdin);
}

//--------------------------------------------------
void DoHCommand()
//--------------------------------------------------
{
   printf(" H (H)elp\n");
   printf("SD (S)how (D)isk sectors\n");
   printf("DD (D)ump process (D)ata-segment\n");
   printf("DS (D)ump process (S)tack-segment\n");
   printf("DR (D)ump process (R)egisters\n");
   printf("\n");
}

//--------------------------------------------------
void DoSDCommand()
//--------------------------------------------------
{
   BYTE sector[BYTES_PER_SECTOR];
   int sectorAddress;

   printf("sectorAddress [0,%d] or Ctrl+Z? ",SECTORS_PER_DISK-1);
   while ( scanf("%d",&sectorAddress) != EOF )
   {
      if ( !((0 <= sectorAddress) && (sectorAddress <= (SECTORS_PER_DISK-1))) )
         printf("sectorAddress out-of-range!\n");
      else
      {
         fseek(GetComputerDISK(),(sectorAddress)*BYTES_PER_SECTOR,SEEK_SET);
         fread(sector,sizeof(BYTE),BYTES_PER_SECTOR,GetComputerDISK());
      /*
         1         2         3         4         5         6         7         8
      12345678901234567890123456789012345678901234567890123456789012345678901234567890
      XXX-XXX: XXXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX  XXXXXXXXXXXXXXXX
      */
         for (int byte = 0; byte <= BYTES_PER_SECTOR-1; byte += 16)
         {
            printf("%3d-%3d:",(byte+1),(byte+1+16-1));
            for (int i = 0; i <= 3; i++)
            {
               printf(" ");
               for (int j = 0; j <= 3; j++)
                  printf("%02X",sector[ byte+i*4+j ]);
            }
            printf("  ");
            for (int i = 0; i <= 3; i++)
            {
               for (int j = 0; j <= 3; j++)
               {
                  char c = (char) sector[ byte+i*4+j ];
   
                  printf("%c",(isprint(c) ? c : ' '));
               }
            }
            printf("\n");
         }
      }
      printf("\nsectorAddress [0,%d] or Ctrl+Z? ",SECTORS_PER_DISK-1);
   }
   printf("\n");
   clearerr(stdin);
}

//--------------------------------------------------
void DoDDCommand()
//--------------------------------------------------
{
   void DumpProcessData(FILE *OUT,PCB *pcb,bool dumpRegisters,bool dumpData,bool dumpStack);

   DumpProcessData(stdout,pcbInRunState,false,true,false);
}

//--------------------------------------------------
void DoDSCommand()
//--------------------------------------------------
{
   void DumpProcessData(FILE *OUT,PCB *pcb,bool dumpRegisters,bool dumpData,bool dumpStack);

   DumpProcessData(stdout,pcbInRunState,false,false,true);
}

//--------------------------------------------------
void DoDRCommand()
//--------------------------------------------------
{
   void DumpProcessData(FILE *OUT,PCB *pcb,bool dumpRegisters,bool dumpData,bool dumpStack);

   DumpProcessData(stdout,pcbInRunState,true,false,false);
}

//--------------------------------------------------
void DumpProcessData(FILE *OUT,PCB *pcb,bool dumpRegisters,bool dumpData,bool dumpStack)
//--------------------------------------------------
{
/*
  PC   SP   FB   R0   R1   R2   R3   R4   R5   R6   R7   R8   R9  R10  R11  R12  R13  R14  R15
XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX
*/
   if ( dumpRegisters )
   {
      fprintf(OUT,"  PC   SP   FB   R0   R1   R2   R3   R4   R5   R6   R7   R8   R9  R10  R11  R12  R13  R14  R15\n");
      fprintf(OUT,"%04X %04X %04X ",(int) GetPC(),(int) GetSP(),(int) GetFB());
      for (int i = 0; i <= 15; i++)
         fprintf(OUT,"%04X ",(int) GetRn(i));
      fprintf(OUT,"\n\n");
      fflush(OUT);
   }

/*
Data   0 1 2 3 4 5 6 7 8 9 A B C D E F  0 1 2 3 4 5 6 7 8 9 A B C D E F
0000: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
   .
   .
XXXX: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
*/
   if (      dumpData )
   {
      fprintf(OUT,"Data   0 1 2 3 4 5 6 7 8 9 A B C D E F "
                        " 0 1 2 3 4 5 6 7 8 9 A B C D E F\n");
      for (int address = pcb->DSBase; address <= (pcb->DSBase+pcb->DSSize-1); address++)
      {
         BYTE byte;

         ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) address,&byte);
         if ( address%32 == 0 ) fprintf(OUT,"%04X: ",address);
         fprintf(OUT,"%02X",(int) byte);
         if ( address%16 == 15 )
            if ( address%32 == 31 )
               fprintf(OUT,"\n");
            else
               fprintf(OUT," ");
      }
      fprintf(OUT,"\n\n");
      fflush(OUT);
   }

/*
Stack  0 1 2 3 4 5 6 7 8 9 A B C D E F  0 1 2 3 4 5 6 7 8 9 A B C D E F
0000: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
   .
   .
XXXX: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
*/
   if (     dumpStack )
   {
      fprintf(OUT,"Stack  0 1 2 3 4 5 6 7 8 9 A B C D E F "
                        " 0 1 2 3 4 5 6 7 8 9 A B C D E F\n");
      for (int address = pcb->SSBase; address <= (int) GetSP()-1; address++)
      {
         BYTE byte;

         ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD) address,&byte);
         if ( address%32 == 0 ) fprintf(OUT,"%04X: ",address);
         fprintf(OUT,"%02X",(int) byte);
         if ( address%16 == 15 )
            if ( address%32 == 31 )
               fprintf(OUT,"\n");
            else
               fprintf(OUT," ");
      }
      fprintf(OUT,"\n\n");
      fflush(OUT);
   }
}

//=============================================================================
// S16OS utility functions
//=============================================================================
//--------------------------------------------------
void S16OS_CreateNewJob(PCB *pcb)
//--------------------------------------------------
{
/*
   S16OS_CreateNewJob() is called by S16OS_ProcessNextClockTick() when a
      LoadJobs()-ed job is removed from the job queue to initialize
      all remaining PCB fields. The job is then added the ready queue.
   S16OS_TerminateProcess() (see below) is called when S16OS_GoToSVCEntryPoint()
      processes the job SVC_TERMINATE_PROCESS and SVC_ABORT_PROCESS system service requests.
*/

   void S16OS_AllocateMemoryPage(const PCB *pcb,WORD *physicalPage,const char situation[]);
   void ConstructQ(QUEUE *queue,char *name,bool containsPCBs,void (*DisplayQNODEObject)(void *object));
   void TraceChildThreadQObject(void *object);

/* *Note* The following PCB fields set by LoadJobs()
   pcb->handle
   pcb->priority
   pcb->takeOffJobQTime
   pcb->CSBase,pcb->CSSize  
   pcb->DSBase,pcb->DSSize  
   pcb->SSBase,pcb->SSSize  
   pcb->memory[]
   pcb->labelTable
*/

// (H)eavyweight process
   pcb->type = 'H';
   pcb->childThreadActiveCount = 0;

// create empty child thread queue
   char *QName = (char *) malloc(sizeof(char)*(strlen(resources[pcb->handle].name)+strlen(" (childThreadQ)")+1));
   sprintf(QName,"%s (childThreadQ)",resources[pcb->handle].name);
   ConstructQ(&pcb->childThreadQ,QName,false,TraceChildThreadQObject);
   free(QName);

   pcb->terminateProcess = false;
   pcb->suspendProcess = false;
   pcb->parentPCB = NULL;

// let S16OS do default handling of SVC errors instead of running process handler
   pcb->SVCErrorHandler = NULLWORD;
   
// let S16OS do default handling of signals (ignore signal) rather instead of running process handler
   pcb->signalHandler = NULLWORD;

// initialize PCB register fields
   pcb->PC = 0X0000;
   pcb->SP = pcb->SSBase;
   pcb->FB = pcb->SSBase;
   for (int n = 0; n <= 15; n++)
      pcb->R[n] = 0;
/*
   allocate main memory segment pages, set MMURegisters[], and load code-segment
      and data-segment with contents of pcb->memory[], stack-segment with 0X00
      Recall 16-bit MMURegister = MVEWRPPP PPPPPPPP
*/
   for (int page = 0; page <= SIZE_IN_PAGES(LOGICAL_ADDRESS_SPACE_IN_BYTES)-1; page++)
   {
   // code-segment is M=1, V=1, E=1, W=0, R=0, PPPPPPPPPPP=11-bit physicalPage
      if      ( (PAGE_NUMBER(pcb->CSBase) <= page) && (page <= PAGE_NUMBER(pcb->CSBase+pcb->CSSize-1)) )
      {
         WORD physicalPage;

         S16OS_AllocateMemoryPage(pcb,&physicalPage,"code-segment for new job");
         pcb->MMURegisters[page] = 0XE000 | physicalPage;
         for (int logicalAddress = page*MEMORY_PAGE_SIZE_IN_BYTES; logicalAddress <= (page+1)*MEMORY_PAGE_SIZE_IN_BYTES-1; logicalAddress++)
         {
            int physicalAddress = (physicalPage << 9) + (logicalAddress & 0X01FF);
            
            WritePhysicalMainMemory(physicalAddress,pcb->memory[logicalAddress]);
         }
      }
   // data-segment is M=1, V=1, E=0, W=1, R=1, PPPPPPPPPPP=11-bit physicalPage
      else if ( (PAGE_NUMBER(pcb->DSBase) <= page) && (page <= PAGE_NUMBER(pcb->DSBase+pcb->DSSize-1)) )
      {
         WORD physicalPage;

         S16OS_AllocateMemoryPage(pcb,&physicalPage,"data-segment for new job");
         pcb->MMURegisters[page] = 0XD800 | physicalPage;
         for (int logicalAddress = page*MEMORY_PAGE_SIZE_IN_BYTES; logicalAddress <= (page+1)*MEMORY_PAGE_SIZE_IN_BYTES-1; logicalAddress++)
         {
            int physicalAddress = (physicalPage << 9) + (logicalAddress & 0X01FF);
            
            WritePhysicalMainMemory(physicalAddress,pcb->memory[logicalAddress]);
         }
      }
   // stack-segment is M=1, V=1, E=0, W=1, R=1, PPPPPPPPPPP=11-bit physicalPage
      else if ( (PAGE_NUMBER(pcb->SSBase) <= page) && (page <= PAGE_NUMBER(pcb->SSBase+pcb->SSSize-1)) )
      {
         WORD physicalPage;

         S16OS_AllocateMemoryPage(pcb,&physicalPage,"stack-segment for new job");
         pcb->MMURegisters[page] = 0XD800 | physicalPage;
         for (int logicalAddress = page*MEMORY_PAGE_SIZE_IN_BYTES; logicalAddress <= (page+1)*MEMORY_PAGE_SIZE_IN_BYTES-1; logicalAddress++)
         {
            int physicalAddress = (physicalPage << 9) + (logicalAddress & 0X01FF);
            
            WritePhysicalMainMemory(physicalAddress,0X00);
         }
      }
   // unused pages     M=0, V=0, E=0, W=0, R=0, PPPPPPPPPPP=0B00000000000
      else
         pcb->MMURegisters[page] = 0X0000;
   }

// initialize terminal OUT buffer to empty
   pcb->OUT[0] = '\0';

// initialize lifetime statistics
   pcb->runStateTime = 0;
   pcb->runStateCount = 0;
   pcb->readyStateTime = 0;
   pcb->readyStateCount = 0;
   pcb->waitStateTime = 0;
   pcb->waitStateCount = 0;
   pcb->joinStateTime = 0;
   pcb->joinStateCount = 0;
   pcb->suspendedStateTime = 0;
   pcb->suspendedStateCount = 0;
   pcb->sleepStateTime = 0;
   pcb->sleepStateCount = 0;
   pcb->semaphoreWaitTime = 0;
   pcb->semaphoreWaitCount = 0;
   pcb->mutexWaitTime = 0;
   pcb->mutexWaitCount = 0;
   pcb->messageWaitTime = 0;
   pcb->messageWaitCount = 0;
   pcb->eventWaitTime = 0;
   pcb->eventWaitCount = 0;
   pcb->diskIOWaitTime = 0;
   pcb->diskIOWaitCount = 0;
   pcb->resourcesWaitTime = 0;
   pcb->resourcesWaitCount = 0;
   pcb->contextSwitchTime = 0;
   pcb->contextSwitchCount = 0;
   pcb->HWInterruptCount = 0;
   pcb->CPUBurstCount = 0;
   pcb->IOBurstCount = 0;
   pcb->signalsSentCount = 0;
   pcb->signalsIgnoredCount = 0;
   pcb->signalsHandledCount = 0;

// *Note* FCFSTime initialized immediately before adding job to ready queue

// initialize CPU SJF scheduler quantities
   pcb->t   = TIME_QUANTUM;
   pcb->tau = TIME_QUANTUM;

// initialize resource usage quantities
   pcb->allocatedResourcesCount = 0;
   pcb->deallocatedResourcesCount = 0;

// compute turnaround time = CPU S16clock at job termination - CPU S16clock at job creation
   pcb->turnAroundTime = -S16clock;

// update number-of-jobs statistic
   numberOfJobsCreated++;

// update throughput statistic
   if ( numberOfJobsCreated == 1 )
      throughputTime = -S16clock;

   //TRACE-ing
   if ( ENABLE_TRACING && TRACE_PROCESS_MANAGEMENT )
   {
      fprintf(TRACE,"@%10d(%3d) create heavyweight process \"%s\"\n",
         S16clock,pcb->handle,resources[pcb->handle].name);
      fflush(TRACE);
   }
   //ENDTRACE-ing

}

//--------------------------------------------------
void S16OS_TerminateProcess(PCB *pcb)
//--------------------------------------------------
{
/*
   Service request parameter(s) description
   ----------------------------------------
    IN parameters: (none)
   OUT parameters: (not applicable)
*/
   void S16OS_DeallocateMemoryPage(const PCB *pcb,WORD physicalPage,const char situation[]);
   void DeallocateResourceHandle(PCB *pcb,HANDLE handle);
   void *ObjectFromQ(QUEUE *queue,int index);
   void RemoveObjectFromQ(QUEUE *queue,int index,void **object);
   void DestructQ(QUEUE *queue);

   HANDLE handle = pcb->handle;
   WORD physicalPage;

   if ( pcb->type == 'H' )   // (H)eavy-weight process
   {
   // deallocate the *ALL* allocated memory segment pages
      for (int page = PAGE_NUMBER(pcb->CSBase); page <= PAGE_NUMBER(pcb->CSBase+pcb->CSSize-1); page++)        //  code-segment page
      {
         physicalPage = pcb->MMURegisters[page] & 0X01FF;
         S16OS_DeallocateMemoryPage(pcb,physicalPage,"code-segment during terminate job");
      }
      for (int page = PAGE_NUMBER(pcb->DSBase); page <= PAGE_NUMBER(pcb->DSBase+pcb->DSSize-1); page++)        //  data-segment page
      {
         physicalPage = pcb->MMURegisters[page] & 0X01FF;
         S16OS_DeallocateMemoryPage(pcb,physicalPage,"data-segment during terminate job");
      }
      for (int page = PAGE_NUMBER(pcb->SSBase); page <= PAGE_NUMBER(pcb->SSBase+pcb->SSSize-1); page++)        // stack-segment page
      {
         physicalPage = pcb->MMURegisters[page] & 0X01FF;
         S16OS_DeallocateMemoryPage(pcb,physicalPage,"stack-segment during terminate job");
      }
   // be a good heap citizen and return memory[] to heap storage
      free(pcb->memory);
   // destruct label table
      DestructLabelTable(&pcb->labelTable);

   // destruct child thread queue
      while ( pcb->childThreadQ.size != 0 )
      {
         CHILDTHREAD *childThread;
         
         RemoveObjectFromQ(&pcb->childThreadQ,1,(void **) &childThread);
         free(childThread);
      }
      DestructQ(&pcb->childThreadQ);
   }
   else// ( pcb->type == 'T' ) (T)hread process
   {
   // deallocate *ONLY* stack-segment pages
      for (int page = PAGE_NUMBER(pcb->SSBase); page <= PAGE_NUMBER(pcb->SSBase+pcb->SSSize-1); page++)        // stack-segment page
      {
         physicalPage = pcb->MMURegisters[page] & 0X01FF;
         S16OS_DeallocateMemoryPage(pcb,physicalPage,"stack-segment during terminate child thread");
      }

   // find terminating child thread in parent child thread list and set isActive to false
      PCB *pcb2 = pcb->parentPCB;
      int index = 1;
      bool found = false;
      CHILDTHREAD *childThread;
      while ( !found && (index <= pcb2->childThreadQ.size) )
      {
      // "peek at" child thread queue index-th CHILDTHREAD record
         childThread = (CHILDTHREAD *) ObjectFromQ(&pcb2->childThreadQ,index);
         if ( handle == childThread->handle )
         {
            childThread->isActive = false;
            found = true;
         }
         else
            index++;
      }
      if ( !found ) // ***THIS CONDITION SHOULD NEVER OCCUR!!!***
      {
         ProcessWarningOrError(S16OSWARNING,"Parent process child thread queue is missing child thread");
      }

   // decrement parent child thread active count
      pcb2->childThreadActiveCount--;
   // destruct empty child thread queue
      DestructQ(&pcb->childThreadQ);
   }

/*
   *Note* Terminal OUT buffer "flush"-ed by S16OS_GoToSVCEntryPoint() when processing the job SVC_TERMINATE_PROCESS 
      and SVC_ABORT_PROCESS system service requests prior to calling S16OS_TerminateProcess() because it makes more 
      sense to display the contents of the terminating job non-empty OUT buffer *BEFORE* the job is terminated.
*/

// deallocate job handle
   DeallocateResourceHandle(pcb,handle);

// deallocate the PCB
   free(pcb);

// update number-of-jobs statistic
   numberOfJobsTerminated++;
   
//--------------------------------------------------
// *Note* Tracing related to process termination is done by S16OS_GoToSVCEntryPoint() processing
//    associated with SVC_TERMINATE_PROCESS and SVC_ABORT_PROCESS service requests
//--------------------------------------------------

}

//--------------------------------------------------
void TraceStateOfSystemQueues()
//--------------------------------------------------
{
   void TraceQueuedProcesses(QUEUE *queue);
   void *ObjectFromQ(QUEUE *queue,int index);

   fprintf(TRACE,"\n***Trace the state of system queues\n");

   TraceQueuedProcesses(&jobQ);
   TraceQueuedProcesses(&readyQ);
   TraceQueuedProcesses(&waitQ);
   TraceQueuedProcesses(&joinQ);

// trace signals on signals queue
   if ( signalsQ.size == 0 )
   {
      fprintf(TRACE,"@%10d   \"signalsQ\" is empty\n",S16clock);
      fflush(TRACE);
   }
   else
   {
      fprintf(TRACE,"@%10d   Signals on \"signalsQ\"\n",S16clock);
      fflush(TRACE);
      for (int i = 1; i <= signalsQ.size; i++)
      {
         SIGNAL *signal = (SIGNAL *) ObjectFromQ(&signalsQ,i);

         fprintf(TRACE,"                 \"%s\"->\"%s\": %d\n",
            resources[signal->senderProcessHandle].name,
            resources[signal->signaledProcessHandle].name,
            signal->signal);
         fflush(TRACE);
      }
   }

   TraceQueuedProcesses(&sleepQ);
   TraceQueuedProcesses(&suspendedQ);

// trace process PCBs on resources wait queue
   if ( resourcesWaitQ.size == 0 )
   {
      fprintf(TRACE,"@%10d   \"resourcesWaitQ\" is empty\n",S16clock);
      fflush(TRACE);
   }
   else
   {
      fprintf(TRACE,"@%10d   Processes on \"resourcesWaitQ\"\n",S16clock);
      fflush(TRACE);
      for (int i = 1; i <= resourcesWaitQ.size; i++)
      {
         RESOURCEWAIT *resourceWait = (RESOURCEWAIT *) ObjectFromQ(&resourcesWaitQ,i);

         fprintf(TRACE,"                 \"%s\" waiting for resource \"%s\"\n",
            resources[resourceWait->pcb->handle].name,resourceWait->name);
         fflush(TRACE);
      }
   }

// trace process PCBs on disk IO queue
   if ( diskIOQ.size == 0 )
   {
      fprintf(TRACE,"@%10d   \"diskIOQ\" is empty\n",S16clock);
      fflush(TRACE);
   }
   else
   {
      fprintf(TRACE,"@%10d   Processes on \"diskIOQ\"\n",S16clock);
      fflush(TRACE);
      for (int i = 1; i <= diskIOQ.size; i++)
      {
         DISKIO *diskIO = (DISKIO *) ObjectFromQ(&diskIOQ,i);

         fprintf(TRACE,"                 \"%s\"\n",resources[diskIO->pcb->handle].name);
         fflush(TRACE);
      }
   }

/*
   trace process PCBs on wait queues of resource types SEMAPHORES, MUTEXES, and EVENTS
      and/or the process PCB waiting for a message to be sent to its MESSAGE_BOX
*/
   for (int handle = 1; handle <= resourcesCount; handle++)
   {
      if ( resources[handle].allocated )
      {
         QUEUE Q;
         bool showProcesses;

         //************************************************************************************
         //***THIS SWITCH STATEMENT MAY NEED TO BE UPDATED WHEN NEW RESOURCE TYPES ARE ADDED***
         //************************************************************************************
         switch ( resources[handle].type )
         {
            case   SEMAPHORES:
               Q = ((SEMAPHORE *) resources[handle].object)->Q;
               fprintf(TRACE,"              Processes on semaphore \"%s\" wait queue\n",resources[handle].name);
               fflush(TRACE);
               showProcesses = true;
               break;
            case      MUTEXES:
               Q = ((SEMAPHORE *) resources[handle].object)->Q;
               fprintf(TRACE,"              Processes on mutex \"%s\" wait queue\n",resources[handle].name);
               fflush(TRACE);
               showProcesses = true;
               break;
            case MESSAGEBOXES:
               if ( ((MESSAGEBOX *) resources[handle].object)->waiting )
               {
                  fprintf(TRACE,"              Process \"%s\" waiting for message on message box \"%s\"\n",
                     resources[((MESSAGEBOX *) resources[handle].object)->ownerHandle].name,resources[handle].name);
                  fflush(TRACE);
               }
               showProcesses = false;
               break;
            case       EVENTS:
               Q = ((EVENT *) resources[handle].object)->Q;
               fprintf(TRACE,"              Processes on event \"%s\" wait queue\n",resources[handle].name);
               fflush(TRACE);
               showProcesses = true;
               break;
            default:
               showProcesses = false;
               break; // ignore *ALL* other resource types
         }
         if ( showProcesses )
         {
            for (int i = 1; i <= Q.size; i++)
            {
               PCB *pcb = (PCB *) ObjectFromQ(&Q,i);

               fprintf(TRACE,"                 \"%s\"\n",resources[pcb->handle].name);
               fflush(TRACE);
            }
         }
      }
   }
   fprintf(TRACE,"***End trace the state of system queues\n\n");
}

//--------------------------------------------------
void TraceQueuedProcesses(QUEUE *queue)
//--------------------------------------------------
{
   void *ObjectFromQ(QUEUE *queue,int index);

// trace processes on PCB queue
   if ( queue->size == 0 )
   {
      fprintf(TRACE,"@%10d   \"%s\" is empty\n",S16clock,queue->name);
      fflush(TRACE);
   }
   else
   {
      fprintf(TRACE,"@%10d   Processes on \"%s\"\n",S16clock,queue->name);
      fflush(TRACE);
      for (int i = 1; i <= queue->size; i++)
      {
         PCB *pcb = (PCB *) ObjectFromQ(queue,i);

         fprintf(TRACE,"                 \"%s\"\n",resources[pcb->handle].name);
         fflush(TRACE);
      }
   }
}

//--------------------------------------------------
void GetNULTerminatedString(const PCB *pcb,WORD logicalAddress,char string[])
//--------------------------------------------------
{
   const WORD NUL = 0X00;

   WORD c;
   int i;

   i = 0;
   do
   {
      BYTE HOB,LOB;

      ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(logicalAddress+2*i+0),&HOB);
      ReadDataLogicalMainMemory(pcb->MMURegisters,(WORD)(logicalAddress+2*i+1),&LOB);
      c = MAKEWORD(HOB,LOB);
      string[i] = (char) c;
      i++;
   } while ( c != NUL );
}

//--------------------------------------------------
int SVCWaitTime(SVC_REQUEST_NUMBER SVCRequestNumber)
//--------------------------------------------------
{
/*
   Randomly-chose the service request wait time from the interval [ MINIMUM_SVCWAITTIME,MAXIMUM_SVCWAITTIME ]
      *Note* When you want to make wait times deterministic, make the minimum and maximum SVC 
         wait times equal to each other.
*/
   return( RandomInt(MINIMUM_SVCWAITTIME,MAXIMUM_SVCWAITTIME) );
}

//=============================================================================
// End of S16OS code
//=============================================================================

//=============================================================================
// Start of S16 code: S16 entry point, the function main()
//=============================================================================
//--------------------------------------------------
int main(int argc,char *argv[])
//--------------------------------------------------
{
   void SetSimulationConfiguration(const char fileName[]);
   void DisplaySimulationConfiguration();
   void LoadJobs(const char fileName[],int *numberJobs,bool *syntaxError);
   void PreExecuteHWInstruction();
   void PostExecuteHWInstruction(const WORD memoryEA,const WORD memoryWORD);

   char fileName[SOURCE_LINE_LENGTH],fullFileName[SOURCE_LINE_LENGTH];
   int numberJobs;
   bool syntaxError;
   char *pLastSlash;
   int S16clockCheckPoint;

/*
   Assuming the file "Sample.job" or "Sample.object" is stored in the same folder
   as the S16 load module (".job" and ".object" are the only legal file name extensions
   allowed), then
   
   1. When S16 is run *WITHOUT* a command line argument, then S16 prompts for the 
      file name as shown here

      C:\CS3350\S16>S16
      File name (.job or .object assumed)? Sample

   2. or, when the file name provided as the only command line argument as shown here, then
      S16 does not prompt for the file name

      C:\CS3350\S16>S16 Sample

   Notice, in either case, only the first name part of the file name may be entered
*/
   if      ( argc == 1 )
   {
      printf("File name (.job or .object assumed)? ");
      gets(fileName);
   }
   else if ( argc == 2 )
   {
      strcpy(fileName,argv[1]);
   }
   else
   {
      printf("Too many command line arguments\n");
      system("PAUSE");
      exit( 1 );
   }

/*
   Determine path to working directory; that is, the folder that contains
      S16 job-related .job files, .object, and .config files.

   **********************
   ***MAC USERS BEWARE***
   **********************
*/
   pLastSlash = strrchr(fileName,SLASH);
   WORKINGDIRECTORY[0] = '\0';
   if ( pLastSlash != NULL )
   {
      int len = pLastSlash-fileName+1;
      
      strncat(WORKINGDIRECTORY,fileName,len);
      strcpy(fileName,&fileName[len]);
   }

/*
 set path the S16.config file

   **********************
   ***MAC USERS BEWARE***
   **********************
*/
   strcpy(S16DIRECTORY,".\\");

// open trace file
   strcpy(fullFileName,WORKINGDIRECTORY);
   strcat(fullFileName,fileName);
   strcat(fullFileName,".trace");
   if ( (TRACE = fopen(fullFileName,"w")) == NULL )
   {
      printf("Error opening trace file \"%s\".\n",fullFileName);
      system("PAUSE");
      exit( 1 );
   }

// set and display simulation configuration
   SetSimulationConfiguration(fileName);
   DisplaySimulationConfiguration();

// initialize random-number generator, S16clock, and the S16 computer
   SetRandomSeed();
   S16clock = 0;
   ConstructComputer();

// "boot" S16OS
   S16OS_GoToStartEntryPoint();

// add job(s) to empty jobQ
   LoadJobs(fileName,&numberJobs,&syntaxError);
   if ( syntaxError )
      ProcessWarningOrError(S16ERROR,"Syntax errors found in .job file!");
   else
   {
      printf("Successfully loaded %d %s\n",numberJobs,((numberJobs == 1) ? "job" : "jobs"));

//--------------------------------------------------
// Do S16clock-tick-by-S16clock-tick execution of loaded jobs by executing 
//    S16clock-tick loop body until computer is HALT-ed. Computer HALT-s
//    when *ALL* loaded jobs have terminated or because an unrecoverable (that is, fatal)
//    hardware error has occurred.
//--------------------------------------------------

      S16clockCheckPoint = S16clock;
      do
      {
      // allow S16OS to process S16clock tick and update S16clock
         S16OS_ProcessNextClockTick();
         S16clock = S16clock+1;

      // when S16 is in RUN state, then trace execution of running process next instruction
         if ( GetCPUState() == RUN )
         {
            WORD memoryEA,memoryWORD;
            
            PreExecuteHWInstruction();
            ExecuteHWInstruction(pcbInRunState->SSBase,&memoryEA,&memoryWORD);
            PostExecuteHWInstruction(memoryEA,memoryWORD);

      // update HW instruction count
            HWInstructionCount++;
         }

      // when S16 is in IDLE state, update IDLE count
         if ( GetCPUState() == IDLE )
         {
            IDLETime++;
         }
      /*
         Poll external hardware devices for interrupts. When hardware interrupt is raised, 
            let S16OS handle the interrupt. This loop allows for one internal interrupt 
            and/or multiple external interrupts to be handled after each S16clock tick.
      */
         PollDevicesForHWInterrupt();
         while ( HWInterruptRaised() )
         {
            S16OS_GoToHWInterruptEntryPoint();
            PollDevicesForHWInterrupt();
         }

      // guard against infinite loops *Note* HWInstructionCount is a poor surrogate for S16clock "ticks"
         if ( USE_S16CLOCK_QUANTUM && ((S16clock-S16clockCheckPoint) >= S16CLOCK_QUANTUM) && (GetCPUState() == RUN) )
         {
            char YorN;

            printf("%7d S16 clock ticks expired. Continue [Y/N]? ",S16clock);
            scanf(" %c",&YorN);
            if ( toupper(YorN) != 'Y' ) SetCPUState(HALT);
            S16clockCheckPoint = S16clock;
         }
      } while ( GetCPUState() != HALT );
      DestructComputer();
       printf(      "\nCPU halted\n"); fflush(stdout);
      fprintf(TRACE,"\nCPU halted\n"); fflush( TRACE);
   }
   fclose(TRACE);
   system("PAUSE");
   return( 0 );
}

//=============================================================================
// S16 "loader"
//=============================================================================
//--------------------------------------------------
void LoadJobs(const char fileName[],int *numberJobs,bool *syntaxError)
//--------------------------------------------------
{
/*
   fileName[] may be the name of a .job file (a jobstream file) and/or the name of a .object file. 
When the .job file is found, each $JOB JCL statement is processed. The .job file must be terminated 
by an $END JCL statement. When not .job file is found for fileName[], then fileName[] must
be the name of an .object file. When neither a .job file nor a .object file are found, an error
is reported and S16 execution is aborted.

   Each  $JOB card provides parameters that specify a JOBS resource NAME (required), an object 
FILE name (required), a STACK size in pages (optional), a PRIORITY (optional), and an ARRIVAL 
time (optional). The optional parameters have defaults (see next paragraph).

   When fileName[] is an .object file (assumed when fileName[] is not a .job file) then fileName[] is
taken as the $JOB object FILE name. Defaults are provided for the JOBS resource NAME (same as fileName[]),
STACK size in pages (configuration parameter DEFAULT_SSSIZE_IN_PAGES), PRIORITY (configuration parameter
DEFAULT_PRIORITY), and ARRIVAL time (0).

   The S16 user should use a .job file to specify $JOB statement parameter values that the application
requires to be different from the defaults supplied when fileName[] is an .object FILE name.

========================================================
Jobstream context-independent syntax expressed using BNF
========================================================
<JOBStream>               ::= <JOBJCLStatement> 
                              { <JOBJCLStatement> }*
                              $END
                              EOFTOKEN

<JOBJCLStatement>         ::= $JOB NAME     = <string> 
                                   FILE     = <string>
                                 [ STACK    = <integer> ]
                                 [ PRIORITY = <integer> ]
                                 [ ARRIVAL  = <integer> ]
                              EOLTOKEN

<string>                  ::= "{ <ASCIIcharacter> }*"                     || code embedded " as ""

<integer>                 ::= [ (( + | - )) ]    <digit>    {    <digit> }* 
                            | [ (( + | - )) ] 0X <hexdigit> { <hexdigit> }*

<fixedpoint>              ::= [ (( + | - )) ] <digit> { <digit> }* . <digit> { <digit> }* 

<digit>                   ::= 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9

<hexdigit>                ::= <digit>
                            | (( a | A )) | (( b | B )) | (( c | C )) 
                            | (( d | D )) | (( e | E )) | (( f | F ))

<comment>                 ::= ; { <ASCIIcharacter> }* EOLTOKEN

<ASCIIcharacter>          ::= (blank) | ! | " | ... | } | ~               || any printable ASCII character
*/
   void GetNextToken(TOKEN *token,char lexeme[]);
   void GetNextCharacter();
   void GetSourceLine();
   void LoadOBJECTFile(PCB *pcb,const char OBJECTFileName[]);
   void AddObjectToQ(QUEUE *queue,void *object,
                     bool (*AddObjectToQNow)(void *pR,void *object));
   bool AddPCBToJobQNow(void *pR,void *pcb);
   void FindNameInResources(const char name[],bool *found,HANDLE *handle);
   void AllocateResourceHandle(PCB *pcb,const char name[],RESOURCETYPE type,bool *allocated,HANDLE *handle);

   bool allocated;
   PCB *pcb;
   HANDLE handle;
   char fullFileName[SOURCE_LINE_LENGTH];
   char OBJECTFileName[SOURCE_LINE_LENGTH];
   TOKEN token;
   char lexeme[SOURCE_LINE_LENGTH+1];

// attempt to open fileName[] as a jobstream file
   strcpy(fullFileName,WORKINGDIRECTORY);
   strcat(fullFileName,fileName);
   strcat(fullFileName,".job");
   if ( (SOURCE = fopen(fullFileName,"r")) != NULL )
   {
   // initialize scanner
      atEOF = false;
      sourceLineNumber = 0;
      GetSourceLine();
      GetNextCharacter();
      GetNextToken(&token,lexeme);

   // load every job in jobstream and add to job queue in ascending takeOffJobQTime order
      *syntaxError = false;
      *numberJobs = 0;
      do
      {
         *syntaxError = false;
      
      // allocate PCB
         pcb = (PCB *) malloc(sizeof(PCB));
      
      // parse $JOB statement
         if ( token != SJOB )
         {
            ProcessWarningOrError(S16WARNING,"$JOB statement not found");
            *syntaxError = true;
            return;
         }
         GetNextToken(&token,lexeme);

      // parse parameter NAME = <string> (required)
         if ( token != NAME )
         {
            ProcessWarningOrError(S16WARNING,"$JOB statement NAME parameter not found");
            *syntaxError = true;
            return;
         }
         GetNextToken(&token,lexeme);
         if ( token != EQUAL )
         {
            ProcessWarningOrError(S16WARNING,"$JOB statement '=' missing");
            *syntaxError = true;
            return;
         }
         GetNextToken(&token,lexeme);
         if ( token != STRING )
         {
            ProcessWarningOrError(S16WARNING,"$JOB statement NAME string expected");
            *syntaxError = true;
            return;
         }
      // allocate JOBS resource
         AllocateResourceHandle(pcb,lexeme,JOBS,&allocated,&handle);
         if ( !allocated )
            ProcessWarningOrError(S16OSERROR,"Unable to allocate handle");
         resources[handle].object = pcb;
         pcb->handle = handle;
         GetNextToken(&token,lexeme);

      // parse parameter FILE = <string> (required)
         if ( token != FILE2 )
         {
            ProcessWarningOrError(S16WARNING,"$JOB statement FILE parameter not found");
            *syntaxError = true;
            return;
         }
         GetNextToken(&token,lexeme);
         if ( token != EQUAL )
         {
            ProcessWarningOrError(S16WARNING,"$JOB statement '=' missing");
            *syntaxError = true;
            return;
         }
         GetNextToken(&token,lexeme);
         if ( token != STRING )
         {
            ProcessWarningOrError(S16WARNING,"$JOB statement FILE string expected");
            *syntaxError = true;
            return;
         }
         strcpy(OBJECTFileName,lexeme);
         GetNextToken(&token,lexeme);

      // parse [ STACK = <integer> ] (optional, default is defaultSSSizeInPages*MEMORY_PAGE_SIZE_IN_BYTES)
         pcb->SSSize = defaultSSSizeInPages*MEMORY_PAGE_SIZE_IN_BYTES;
         pcb->SSBase = PAGE_NUMBER(LOGICAL_ADDRESS_SPACE_IN_BYTES-pcb->SSSize)*MEMORY_PAGE_SIZE_IN_BYTES;
         if ( token == STACK )
         {
            GetNextToken(&token,lexeme);
            if ( token != EQUAL )
            {
               ProcessWarningOrError(S16WARNING,"$JOB statement '=' missing");
               *syntaxError = true;
            }
            GetNextToken(&token,lexeme);
            if ( token != INTEGER )
            {
               ProcessWarningOrError(S16WARNING,"$JOB statement STACK integer expected");
               *syntaxError = true;
            }
            if ( !(0 < strtol(lexeme,NULL,0)) )
            {
               ProcessWarningOrError(S16WARNING,"$JOB card STACK invalid integer");
               *syntaxError = true;
            }
            else
            {
               pcb->SSSize = strtol(lexeme,NULL,0)*MEMORY_PAGE_SIZE_IN_BYTES;
               pcb->SSBase = PAGE_NUMBER(LOGICAL_ADDRESS_SPACE_IN_BYTES-pcb->SSSize)*MEMORY_PAGE_SIZE_IN_BYTES;
            }
            GetNextToken(&token,lexeme);
         }
   
      //   parse [ PRIORITY = <integer> ] (optional, default is DEFAULT_PRIORITY)
         pcb->priority = DEFAULT_PRIORITY;
         if ( token == PRIORITY )
         {
            GetNextToken(&token,lexeme);
            if ( token != EQUAL )
            {
               ProcessWarningOrError(S16WARNING,"$JOB statement '=' missing");
               *syntaxError = true;
            }
            GetNextToken(&token,lexeme);
            if ( token != INTEGER )
            {
               ProcessWarningOrError(S16WARNING,"$JOB statement PRIORITY integer expected");
               *syntaxError = true;
            }
         // (*Note* MAXIMUM_PRIORITY <= <integer> <= MINIMUM_PRIORITY (inverse ordering))
            if ( !((MAXIMUM_PRIORITY      <=    strtol(lexeme,NULL,0)) &&
                   (strtol(lexeme,NULL,0) <=        MINIMUM_PRIORITY)) )
            {
               ProcessWarningOrError(S16WARNING,"$JOB statement PRIORITY integer must be in [ MAXIMUM_PRIORITY,MINIMUM_PRIORITY ]");
               *syntaxError = true;
            }
            else
               pcb->priority = strtol(lexeme,NULL,0);
            GetNextToken(&token,lexeme);
         }

      // parse [ ARRIVAL  = <integer> ] (optional, default is 0) (*Note* 0 <= <integer>)
         pcb->takeOffJobQTime = 0;
         if ( token == ARRIVAL )
         {
            GetNextToken(&token,lexeme);
            if ( token != EQUAL )
            {
               ProcessWarningOrError(S16WARNING,"$JOB statement '=' missing");
               *syntaxError = true;
            }
            GetNextToken(&token,lexeme);
            if ( token != INTEGER )
            {
               ProcessWarningOrError(S16WARNING,"$JOB statement ARRIVAL integer expected");
               *syntaxError = true;
            }
            if ( !(0 <= strtol(lexeme,NULL,0)) )
            {
               ProcessWarningOrError(S16WARNING,"$JOB card ARRIVAL integer must be >= 0");
               *syntaxError = true;
            }
            else
               pcb->takeOffJobQTime = strtol(lexeme,NULL,0);
            GetNextToken(&token,lexeme);
         }

      // load OBJECT file
         LoadOBJECTFile(pcb,OBJECTFileName);

      // add PCB to job queue in ascending takeOffJobQTime order
         AddObjectToQ(&jobQ,pcb,AddPCBToJobQNow);
         *numberJobs += 1;
      } while ( (token != SEND) && !*syntaxError && !atEOF );
   
      if ( token != SEND ) ProcessWarningOrError(S16WARNING,"$END card missing");
      fclose(SOURCE);
   }
   else // fileName[] must be an .object file
   {
   // allocate PCB
      pcb = (PCB *) malloc(sizeof(PCB));

   // allocate JOBS resource
      AllocateResourceHandle(pcb,fileName,JOBS,&allocated,&handle);
      if ( !allocated )
         ProcessWarningOrError(S16OSERROR,"Unable to allocate handle");
      resources[handle].object = pcb;
      pcb->handle = handle;

   // set $JOB statement parameter defaults
      strcpy(OBJECTFileName,fileName);
      pcb->SSSize = defaultSSSizeInPages*MEMORY_PAGE_SIZE_IN_BYTES;
      pcb->SSBase = PAGE_NUMBER(LOGICAL_ADDRESS_SPACE_IN_BYTES-pcb->SSSize)*MEMORY_PAGE_SIZE_IN_BYTES;
      pcb->priority = DEFAULT_PRIORITY;
      pcb->takeOffJobQTime = 0;

   // load OBJECT file
      LoadOBJECTFile(pcb,OBJECTFileName);

   // add PCB to job queue in ascending takeOffJobQTime order
      AddObjectToQ(&jobQ,pcb,AddPCBToJobQNow);
      *syntaxError = false;
      *numberJobs = 1;
   }
}

//--------------------------------------------------
void LoadOBJECTFile(PCB *pcb,const char OBJECTFileName[])
//--------------------------------------------------
{
/*
Object program (load module) use of paged-memory
================================================
Paged memory...
CSBase = 0X0000 (code-segment contents found in .object file)
       CSSize pages allocated as in-(M)emory, (V)alid, (E)xecute permission
       (*Note* last code-segment page probably suffers from internal fragmentation
DSBase = NEXT_PAGE(CSBase+CSSize-1) (contents found in .object file)
       DSSize pages allocated as in-(M)emory, (V)alid, (R)ead+(W)rite permissions
           (*Note* last data-segment page probably suffers from internal fragmentation
SSBase stack-segment

Compare with the old=fashioned segmented memory...
Data- segment in page 0
                 page (DSSizeInPages-1)
Code- segment in page 0
                 page (CSSizeInPages-1)
Stack-segment in page 0
                      (SSSizeInPages-1)
*/
   char fullFileName[SOURCE_LINE_LENGTH];
   FILE *OBJECT;
   int size;

// load object program into labelTable and memory[]
   strcpy(fullFileName,WORKINGDIRECTORY);
   strcat(fullFileName,OBJECTFileName);
   strcat(fullFileName,".object");
   if ( (OBJECT = fopen(fullFileName,"r")) == NULL )
   {
      char information[SOURCE_LINE_LENGTH+1];

      sprintf(information,"Error opening object file \"%s\"",fullFileName);
      ProcessWarningOrError(S16OSERROR,information);
   }
/*
   =====================
   OBJECT file format (see S16Assembler.c function BuildObjectFile() for details)
   =====================
   DDD                                  size (that is, number of labels)
   TS 0XXXXX DDDD LL...(lexeme #   1)   (T)ype { 'C','E','D' } (S)ource { 'S','A' } value definitionLine lexeme
   ...
   TS 0XXXXX DDDD LL...(lexeme #size)   (T)ype { 'C','E','D' } (S)ource { 'S','A' } value definitionLine lexeme
   0XXXXX DDDDD                         CSBase CSSize
   0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 16 bytes of code-segment
   ...                                                                             16 bytes of code-segment
   0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 1-to-16 bytes of code-segment
   0XXXXX DDDDD                         DSBase DSSize
   0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 16 bytes of data-segment
   ...                                                                             16 bytes of data-segment
   0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 0XXX 1-to-16 bytes of data-segment
*/
// (1) labelTable
   fscanf(OBJECT,"%d%*c",&size);
   ConstructLabelTable(&pcb->labelTable,size);
   for (int i = 1; i <= size; i++)
   {
      int index;
      bool inserted;
      char type;                          // 'E'QU, 'C'ode-segment, 'D'ata-segment
      char source;                        // 'S'tandard or 'A'pplication
      int value;
      int definitionLine;
      char lexeme[MAX_LABEL_LENGTH+1];

      fscanf(OBJECT,"%1c%1c%6X%4d%s%*c",&type,&source,&value,&definitionLine,lexeme);
      InsertLabelTable(&pcb->labelTable,lexeme,type,source,value,definitionLine,&index,&inserted);
   }

// (2) memory[] for code-segment and data-segment; code-segment and data-segment bases/sizes
   pcb->memory = (BYTE *) malloc(sizeof(BYTE)*LOGICAL_ADDRESS_SPACE_IN_BYTES);
   fscanf(OBJECT,"%hX%hd",&pcb->CSBase,&pcb->CSSize);
   for (int i = 0; i <= pcb->CSSize-1; i++) 
      fscanf(OBJECT,"%hhX",&pcb->memory[pcb->CSBase+i]);
   fscanf(OBJECT,"%hX%hd",&pcb->DSBase,&pcb->DSSize);
   for (int i = 0; i <= pcb->DSSize-1; i++)
      fscanf(OBJECT,"%hhX",&pcb->memory[pcb->DSBase+i]);

   fclose(OBJECT);
}

//=============================================================================
// S16 simulation configuration settings
//=============================================================================
//--------------------------------------------------
void SetSimulationConfiguration(const char JOBSTREAMFileName[])
//--------------------------------------------------
{
   void ProcessConfigFile(const char configFileName[],bool *syntaxErrors);

   char fullFileName[SOURCE_LINE_LENGTH];
   bool syntaxErrors;

//--------------------------------------------------
// set defaults for *ALL* configuration parameter settings
//--------------------------------------------------
// TRACE-ing settings
   ENABLE_TRACING            = true;
   TRACE_INSTRUCTIONS        = true;
   TRACE_MEMORY_ALLOCATION   = true;
   TRACE_SJF_SCHEDULING      = true;
   TRACE_SCHEDULER           = true;
   TRACE_DISPATCHER          = true;
   TRACE_QUEUES              = true;
   TRACE_STATISTICS          = true;
   TRACE_HWINTERRUPTS        = true;
   TRACE_MISCELLANEOUS_SVC   = true;
   TRACE_PROCESS_MANAGEMENT  = true;
   TRACE_RESOURCE_MANAGEMENT = true;
   TRACE_TERMINAL_IO         = true;
   TRACE_DISK_IO             = true;
   TRACE_MEMORYSEGMENTS      = true;
   TRACE_MESSAGEBOXES        = true;
   TRACE_SEMAPHORES          = true;
   TRACE_MUTEXES             = true;
   TRACE_EVENTS              = true;
   TRACE_PIPES               = true;
   TRACE_DEADLOCK_DETECTION  = true;
   TRACE_SIGNALS             = true;

// CPU short-term scheduler settings
   CPU_SCHEDULER = FCFS_CPU_SCHEDULER;

   MINIMUM_PRIORITY = 10;
   DEFAULT_PRIORITY =  5;
   MAXIMUM_PRIORITY =  1;

   alpha = 0.50;

   USE_PREEMPTIVE_CPU_SCHEDULER = false;
   TIME_QUANTUM = 100;

// message queue scheduler setting
   MQ_SCHEDULER = FCFS_MQ_SCHEDULER;

// disk IO queue scheduler setting
   DISKIOQ_SCHEDULER = FCFS_DISKIOQ_SCHEDULER;

// S16clock ticks quantum settings (*NOTE* Used to guard against infinite loops and infinite recursion)
   USE_S16CLOCK_QUANTUM = true;
   S16CLOCK_QUANTUM = 10000;

// service request wait time settings
   MINIMUM_SVCWAITTIME =  20;
   MAXIMUM_SVCWAITTIME = 200;

// total context switch time setting (1/2 incurred at S16OS HW and SW entry points,
//    1/2 incurred at dispatch time)
   CONTEXT_SWITCH_TIME =  10;

// deadlock detection algorithm setting
   DEADLOCK_DETECTION_ALGORITHM = DEADLOCK_DETECTION_METHOD2;

// default terminal IO settings
//    *terminal prompt
   strcpy(defaultTerminalPrompt,"? ");
//    * spelling for TRUE and FALSE
   strcpy( TRUEString, "true");
   strcpy(FALSEString,"false");
   
// default SSSize measured in pages (8 pages)*(512 bytes/page) = 4K bytes
   defaultSSSizeInPages = 8;

/*
   Allow two "config" files to change default configuration settings *IN THE FOLLOWING ORDER*
      (1) "S16.config" (must be in same folder as S16 load module, namely, S16DIRECTORY)
      (2) JOBSTREAMFileName.config (must be in same folder as .job file, namely, WORKINGDIRECTORY)
*/
   strcpy(fullFileName,S16DIRECTORY);
   strcat(fullFileName,"S16.config");
   ProcessConfigFile(fullFileName,&syntaxErrors);
   if ( syntaxErrors )
      ProcessWarningOrError(S16ERROR,"Config file contains syntax errors");
   
   strcpy(fullFileName,WORKINGDIRECTORY);
   strcat(fullFileName,JOBSTREAMFileName);
   strcat(fullFileName,".config");
   ProcessConfigFile(fullFileName,&syntaxErrors);
   if ( syntaxErrors )
      ProcessWarningOrError(S16ERROR,"Config file contains syntax errors");
}

//--------------------------------------------------
void ProcessConfigFile(const char configFileName[],bool *syntaxErrors)
//--------------------------------------------------
{
   void GetSourceLine();
   void GetNextCharacter();
   void GetNextToken(TOKEN *token,char lexeme[]);

   *syntaxErrors = false;
   if ( (SOURCE = fopen(configFileName,"r")) == NULL )
   {
      printf("Configuration file \"%s\" not found\n",configFileName);
      fprintf(TRACE,"Configuration file \"%s\" not found\n",configFileName);
   }
   else
   {
      TOKEN token,parameterToken,settingToken;
      char lexeme[SOURCE_LINE_LENGTH+1],parameterLexeme[SOURCE_LINE_LENGTH+1],settingLexeme[SOURCE_LINE_LENGTH+1];

      printf("Processing configuration file \"%s\"\n",configFileName);

      atEOF = false;
      sourceLineNumber = 0;
      GetSourceLine();
      GetNextCharacter();
      GetNextToken(&parameterToken,parameterLexeme);

      while ( parameterToken != EOFTOKEN )
      {
         GetNextToken(&token,lexeme);
         if ( token != EQUAL )
         {
            ProcessWarningOrError(S16WARNING,"Expecting =");
            *syntaxErrors = true;
         }
         else
         {
            GetNextToken(&settingToken,settingLexeme);
            switch ( parameterToken )
            {
               case ENABLETRACING:
                     switch ( settingToken )
                     {
                        case  TRUE: ENABLE_TRACING =  true; break;
                        case FALSE: ENABLE_TRACING = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEINSTRUCTIONS:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_INSTRUCTIONS =  true; break;
                        case FALSE: TRACE_INSTRUCTIONS = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEMEMORYALLOCATION:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_MEMORY_ALLOCATION =  true; break;
                        case FALSE: TRACE_MEMORY_ALLOCATION = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACESJFSCHEDULING:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_SJF_SCHEDULING =  true; break;
                        case FALSE: TRACE_SJF_SCHEDULING = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACESCHEDULER:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_SCHEDULER =  true; break;
                        case FALSE: TRACE_SCHEDULER = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEDISPATCHER:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_DISPATCHER =  true; break;
                        case FALSE: TRACE_DISPATCHER = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEQUEUES:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_QUEUES =  true; break;
                        case FALSE: TRACE_QUEUES = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACESTATISTICS:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_STATISTICS =  true; break;
                        case FALSE: TRACE_STATISTICS = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEHWINTERRUPTS:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_HWINTERRUPTS =  true; break;
                        case FALSE: TRACE_HWINTERRUPTS = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEMISCELLANEOUSSVC:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_MISCELLANEOUS_SVC =  true; break;
                        case FALSE: TRACE_MISCELLANEOUS_SVC = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEPROCESSMANAGEMENT:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_PROCESS_MANAGEMENT =  true; break;
                        case FALSE: TRACE_PROCESS_MANAGEMENT = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACERESOURCEMANAGEMENT:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_RESOURCE_MANAGEMENT =  true; break;
                        case FALSE: TRACE_RESOURCE_MANAGEMENT = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACETERMINALIO:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_TERMINAL_IO =  true; break;
                        case FALSE: TRACE_TERMINAL_IO = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEDISKIO:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_DISK_IO =  true; break;
                        case FALSE: TRACE_DISK_IO = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEMEMORYSEGMENTS:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_MEMORYSEGMENTS =  true; break;
                        case FALSE: TRACE_MEMORYSEGMENTS = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEMESSAGEBOXES:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_MESSAGEBOXES =  true; break;
                        case FALSE: TRACE_MESSAGEBOXES = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACESEMAPHORES:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_SEMAPHORES =  true; break;
                        case FALSE: TRACE_SEMAPHORES = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEMUTEXES:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_MUTEXES =  true; break;
                        case FALSE: TRACE_MUTEXES = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEEVENTS:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_EVENTS =  true; break;
                        case FALSE: TRACE_EVENTS = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEPIPES:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_PIPES =  true; break;
                        case FALSE: TRACE_PIPES = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACEDEADLOCKDETECTION:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_DEADLOCK_DETECTION =  true; break;
                        case FALSE: TRACE_DEADLOCK_DETECTION = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TRACESIGNALS:
                     switch ( settingToken )
                     {
                        case  TRUE: TRACE_SIGNALS =  true; break;
                        case FALSE: TRACE_SIGNALS = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;

               case CPUSCHEDULER:
                     switch ( settingToken )
                     {
                        case     FCFSCPUSCHEDULER:
                           CPU_SCHEDULER = FCFS_CPU_SCHEDULER; break;
                        case      SJFCPUSCHEDULER:
                           CPU_SCHEDULER = SJF_CPU_SCHEDULER; break;
                        case PRIORITYCPUSCHEDULER:
                           CPU_SCHEDULER = PRIORITY_CPU_SCHEDULER; break;
                        default:
                           ProcessWarningOrError(S16WARNING,"Invalid CPU scheduler");
                           *syntaxErrors = true;
                           break;
                     }
                     break;

               case MINIMUMPRIORITY:
                     if ( settingToken == INTEGER )
                        sscanf(settingLexeme,"%d",&MINIMUM_PRIORITY);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting integer");
                        *syntaxErrors = true;
                     }
                     break;
               case DEFAULTPRIORITY:
                     if ( settingToken == INTEGER )
                        sscanf(settingLexeme,"%d",&DEFAULT_PRIORITY);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting integer");
                        *syntaxErrors = true;
                     }
                     break;
               case MAXIMUMPRIORITY:
                     if ( settingToken == INTEGER )
                        sscanf(settingLexeme,"%d",&MAXIMUM_PRIORITY);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting integer");
                        *syntaxErrors = true;
                     }
                     break;
               case ALPHA:
                     if ( settingToken == FIXEDPOINT )
                        sscanf(settingLexeme,"%lf",&alpha);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting fixed-point real");
                        *syntaxErrors = true;
                     }
                     break;
               case USEPREEMPTIVECPUSCHEDULER:
                     switch ( settingToken )
                     {
                        case  TRUE: USE_PREEMPTIVE_CPU_SCHEDULER =  true; break;
                        case FALSE: USE_PREEMPTIVE_CPU_SCHEDULER = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case TIMEQUANTUM:
                     if ( settingToken == INTEGER )
                        sscanf(settingLexeme,"%d",&TIME_QUANTUM);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting integer");
                        *syntaxErrors = true;
                     }
                     break;
               case MQSCHEDULER:
                     switch ( settingToken )
                     {
                        case     FCFSMQSCHEDULER:
                           MQ_SCHEDULER = FCFS_MQ_SCHEDULER; break;
                        default:
                           ProcessWarningOrError(S16WARNING,"Invalid MQ scheduler"); break;
                           *syntaxErrors = true;
                           break;
                     }
                     break;
               case DISKIOQSCHEDULER:
                     switch ( settingToken )
                     {
                        case     FCFSDISKIOQSCHEDULER:
                           DISKIOQ_SCHEDULER = FCFS_DISKIOQ_SCHEDULER; break;
                        default:
                           ProcessWarningOrError(S16WARNING,"Invalid DISKIOQ scheduler"); break;
                           *syntaxErrors = true;
                           break;
                     }
                     break;
               case USES16CLOCKQUANTUM:
                     switch ( settingToken )
                     {
                        case  TRUE: USE_S16CLOCK_QUANTUM =  true; break;
                        case FALSE: USE_S16CLOCK_QUANTUM = false; break;
                           default: ProcessWarningOrError(S16WARNING,"Expecting true or false");
                                    *syntaxErrors = true;
                                    break;
                     }
                     break;
               case S16CLOCKQUANTUM:
                     if ( settingToken == INTEGER )
                        sscanf(settingLexeme,"%d",&S16CLOCK_QUANTUM);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting integer");
                        *syntaxErrors = true;
                     }
                     break;
               case MINIMUMSVCWAITTIME:
                     if ( settingToken == INTEGER )
                        sscanf(settingLexeme,"%d",&MINIMUM_SVCWAITTIME);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting integer");
                        *syntaxErrors = true;
                     }
                     break;
               case MAXIMUMSVCWAITTIME:
                     if ( settingToken == INTEGER )
                        sscanf(settingLexeme,"%d",&MAXIMUM_SVCWAITTIME);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting integer");
                        *syntaxErrors = true;
                     }
                     break;
               case CONTEXTSWITCHTIME:
                     if ( settingToken == INTEGER )
                        sscanf(settingLexeme,"%d",&CONTEXT_SWITCH_TIME);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting integer");
                        *syntaxErrors = true;
                     }
                     break;
               case DEADLOCKDETECTIONALGORITHM:
                     switch ( settingToken )
                     {
                        case NODEADLOCKDETECTION:
                           DEADLOCK_DETECTION_ALGORITHM = NO_DEADLOCK_DETECTION; break;
                        case DEADLOCKDETECTIONMETHOD1:
                           DEADLOCK_DETECTION_ALGORITHM = DEADLOCK_DETECTION_METHOD1; break;
                        case DEADLOCKDETECTIONMETHOD2:
                           DEADLOCK_DETECTION_ALGORITHM = DEADLOCK_DETECTION_METHOD2; break;
                        default:
                           ProcessWarningOrError(S16WARNING,"Invalid deadlock detection algorithm");
                           *syntaxErrors = true;
                           break;
                     }
                     break;
               case DEFAULTTERMINALPROMPT:
                     if ( settingToken == STRING )
                        strcpy(defaultTerminalPrompt,settingLexeme);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting string");
                        *syntaxErrors = true;
                     }
                     break;
               case TRUESTRING:
                     if ( settingToken == STRING )
                        strcpy(TRUEString,settingLexeme);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting string");
                        *syntaxErrors = true;
                     }
                     break;
               case FALSESTRING:
                     if ( settingToken == STRING )
                        strcpy(FALSEString,settingLexeme);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting string");
                        *syntaxErrors = true;
                     }
                     break;
               case DEFAULTSSSIZEINPAGES: // *Note* defaultSSSizeInPages defined as a WORD not an int
                     if ( settingToken == INTEGER )
                        sscanf(settingLexeme,"%d",&defaultSSSizeInPages);
                     else
                     {
                        ProcessWarningOrError(S16WARNING,"Expecting integer");
                        *syntaxErrors = true;
                     }
                     break;
               default:
                     ProcessWarningOrError(S16WARNING,"Invalid configuration parameter");
                     *syntaxErrors = true;
                     break;
            }
         }
         GetSourceLine();
         GetNextCharacter();
         GetNextToken(&parameterToken,parameterLexeme);
      }
      fclose(SOURCE);
   }
}

//--------------------------------------------------
void DisplaySimulationConfiguration()
//--------------------------------------------------
{
   fprintf( TRACE,"---------------------------------------------------------\n");

   printf (       "S16/S16OS version %s\n",S16_VERSION);
   fprintf( TRACE,"S16/S16OS version %s\n",S16_VERSION);

   if ( ENABLE_TRACING )
   {
      printf (       "\nTracing is enabled\n\n"); fflush(stdout);
      fprintf( TRACE,"\nTracing is enabled\n");

      if ( TRACE_INSTRUCTIONS )
         fprintf( TRACE,"   Trace instruction execution\n");

      if ( TRACE_MEMORY_ALLOCATION )
         fprintf( TRACE,"   Trace memory allocations\n");

      if ( TRACE_SJF_SCHEDULING )
         fprintf( TRACE,"   Trace SJF scheduler t and tau changes\n");

      if ( TRACE_SCHEDULER )
         fprintf( TRACE,"   Trace process scheduling decisions\n");

      if ( TRACE_DISPATCHER )
         fprintf( TRACE,"   Trace process dispatching decisions\n");

      if ( TRACE_QUEUES )
         fprintf( TRACE,"   Trace O/S queue operations\n");

      if ( TRACE_STATISTICS )
         fprintf( TRACE,"   Trace individual process statistics and system statistics\n");

      if ( TRACE_HWINTERRUPTS )
         fprintf( TRACE,"   Trace occurrence of hardware interrupts\n");

      if ( TRACE_MISCELLANEOUS_SVC )
         fprintf( TRACE,"   Trace miscellaneous service requests (ones not already traced)\n");

      if ( TRACE_PROCESS_MANAGEMENT )
         fprintf( TRACE,"   Trace process management activities\n");

      if ( TRACE_RESOURCE_MANAGEMENT )
         fprintf( TRACE,"   Trace resource management activities\n");

      if ( TRACE_TERMINAL_IO )
         fprintf( TRACE,"   Trace terminal IO\n");

      if ( TRACE_DISK_IO )
         fprintf( TRACE,"   Trace disk IO\n");

      if ( TRACE_MEMORYSEGMENTS )
         fprintf( TRACE,"   Trace memory-segment activities\n");

      if ( TRACE_MESSAGEBOXES )
         fprintf( TRACE,"   Trace message box service requests\n");

      if ( TRACE_SEMAPHORES )
         fprintf( TRACE,"   Trace semaphore service requests\n");

      if ( TRACE_MUTEXES )
         fprintf( TRACE,"   Trace mutex service requests\n");

      if ( TRACE_EVENTS )
         fprintf( TRACE,"   Trace event service requests\n");

      if ( TRACE_PIPES )
         fprintf( TRACE,"   Trace pipe service requests\n");

      if ( TRACE_DEADLOCK_DETECTION )
         fprintf( TRACE,"   Trace deadlock detection\n");

      if ( TRACE_SIGNALS )
         fprintf( TRACE,"   Trace signals\n");
   }
   else
   {
      printf (       "\nTracing is not enabled\n\n");
      fprintf( TRACE,"\nTracing is not enabled\n");
   }

   fprintf( TRACE,"\nMaximum number of resources is %4d\n",MAXIMUM_RESOURCES);

   switch ( CPU_SCHEDULER )
   {
      case FCFS_CPU_SCHEDULER:
              fprintf( TRACE,"\nUsing First Come/First Serve (FCFS) short-term CPU scheduler\n");
              break;
      case PRIORITY_CPU_SCHEDULER:
              fprintf( TRACE,"\nUsing Priority short-term CPU scheduler\n");
              fprintf( TRACE,"   Priority scheduler minimum priority is %2d\n",MINIMUM_PRIORITY);
              fprintf( TRACE,"   Priority scheduler default priority is %2d\n",DEFAULT_PRIORITY);
              fprintf( TRACE,"   Priority scheduler maximum priority is %2d\n",MAXIMUM_PRIORITY);
              break;
      case SJF_CPU_SCHEDULER:
             fprintf( TRACE,"\nUsing Shortest Job First (SJF) short-term CPU scheduler\n");
             fprintf( TRACE,"   SJF scheduler alpha is %4.2f\n",alpha);
             break;
      default:
             ProcessWarningOrError(S16WARNING,"Invalid CPU scheduler");
             break;
   }

   if ( USE_PREEMPTIVE_CPU_SCHEDULER )
   {
      fprintf( TRACE,"\nUsing preemptive short-term CPU scheduler\n");
      fprintf( TRACE,"   Preemptive scheduler time quantum is %d\n",TIME_QUANTUM);
   }
   else
      fprintf( TRACE,"\nUsing non-preemptive short-term CPU scheduler\n");
      
   switch ( MQ_SCHEDULER )
   {
      case FCFS_MQ_SCHEDULER:
         fprintf( TRACE,"\nUsing FCFS message queue scheduler\n");
         break;
      case some_other_MQ_scheduler2:
      case some_other_MQ_scheduler3:
      default:
             ProcessWarningOrError(S16WARNING,"Invalid message queue scheduler");
             break;
   }

   switch ( MQ_SCHEDULER )
   {
      case FCFS_DISKIOQ_SCHEDULER:
              fprintf( TRACE,"\nUsing FCFS disk IO queue scheduler\n");
              break;
      case some_other_diskIOQ_scheduler2:
      case some_other_diskIOQ_scheduler3:
      default:
             ProcessWarningOrError(S16WARNING,"Invalid disk IO scheduler");
             break;
   }

   if( USE_S16CLOCK_QUANTUM )
   {
      fprintf( TRACE,"\nGuarding against run-away execution\n");
      fprintf( TRACE,"   S16clock quantum is %d\n",S16CLOCK_QUANTUM);
   }
   else
      fprintf( TRACE,"\nNot guarding against run-away execution\n");

   fprintf( TRACE,"\nMinimum SVC wait time is %5d\n",MINIMUM_SVCWAITTIME);
   fprintf( TRACE,"Maximum SVC wait time is %5d\n",MAXIMUM_SVCWAITTIME);

   fprintf( TRACE,"\nContext switch time is %5d\n",CONTEXT_SWITCH_TIME);

   switch ( DEADLOCK_DETECTION_ALGORITHM )
   {
      case NO_DEADLOCK_DETECTION:
         fprintf( TRACE,"\nNo deadlock detection\n");
         break;
      case DEADLOCK_DETECTION_METHOD1:
         fprintf( TRACE,"\nUsing single-instance resource deadlock detection algorithm (method #1)\n");
         break;
      case DEADLOCK_DETECTION_METHOD2:
         fprintf( TRACE,"\nUsing several-instance resource deadlock detection algorithm (method #2)\n");
         break;
   }

   fprintf( TRACE,"\nDefault terminal prompt is \"%s\"",defaultTerminalPrompt);
   fprintf( TRACE,"\nTRUE string is \"%s\"\n",TRUEString);
   fprintf( TRACE,"FALSE string is \"%s\"\n",FALSEString);
      
   fprintf( TRACE,"\nDefault SSSize in pages is %d\n",defaultSSSizeInPages);
   
   fprintf( TRACE,"---------------------------------------------------------\n\n");

   fflush( TRACE);
   fflush(stdout);
}

//=============================================================================
// S16 trace machine instructions
//=============================================================================
/*--------------------------------------------------
   *Note* The contents of the following ***GLOBAL VARIABLES*** *MUST* remain unchanged between the end of
      execution of PreExecuteHWInstruction() and beginning of execution of the subsequent PostExecuteHWInstruction()
      (see S16clock-tick-by-S16clock-tick execution in main()).
--------------------------------------------------*/
char field1[22+1],field2[7+1],field3[18+1],field4[27+1],field5[42+1];
BYTE opcode,Rn,Rn2,Rnx,mode;

/*--------------------------------------------------
   Trace HW instruction execution using PreExecuteHWInstruction(),
      Computer::ExecuteHWInstruction(), and PostExecuteHWInstruction()
      function calls (in that order).
  *Note*
      PreExecuteHWInstruction() fills field1(1:22),field2(24:30),field3(32:49),field5(79:120)
      and PostExecuteHWInstruction() fills field4(51:77) using the following template
                                                                                                   1         1         1
         1         2         3         4         5         6         7         8         9         0         1         2
1234567890123456789012*4567890*234567890123456789*123456789012345678901234567*901234567890123456789012345678901234567890
                 AA..'C'label..AA
@TTTTTTTTTT(XXX) XXXX: NOOP
@TTTTTTTTTT(XXX) XXXX: JMP     XXXX                                           AA..'C'label..AA
@TTTTTTTTTT(XXX) XXXX: JMPN    Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
@TTTTTTTTTT(XXX) XXXX: JMPNN   Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
@TTTTTTTTTT(XXX) XXXX: JMPZ    Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
@TTTTTTTTTT(XXX) XXXX: JMPNZ   Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
@TTTTTTTTTT(XXX) XXXX: JMPP    Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
@TTTTTTTTTT(XXX) XXXX: JMPNP   Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
@TTTTTTTTTT(XXX) XXXX: CALL    XXXX               SP = XXXX                   AA..'C'label..AA
@TTTTTTTTTT(XXX) XXXX: RET                        SP = XXXX 
@TTTTTTTTTT(XXX) XXXX: SVC     #XXXX              R0 = XXXX R15(XXXX)         AA..'E'label..AA
@TTTTTTTTTT(XXX) XXXX: DEBUG

@TTTTTTTTTT(XXX) XXXX: ADDR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: SUBR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: INCR    Rn                 Rn = XXXX
@TTTTTTTTTT(XXX) XXXX: DECR    Rn                 Rn = XXXX
@TTTTTTTTTT(XXX) XXXX: ZEROR   Rn                 Rn = XXXX
@TTTTTTTTTT(XXX) XXXX: LSRR    Rn                 Rn = XXXX
@TTTTTTTTTT(XXX) XXXX: ASRR    Rn                 Rn = XXXX
@TTTTTTTTTT(XXX) XXXX: SLR     Rn                 Rn = XXXX
@TTTTTTTTTT(XXX) XXXX: CMPR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: CMPUR   Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: ANDR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: ORR     Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: XORR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: NOTR    Rn                 Rn = XXXX
@TTTTTTTTTT(XXX) XXXX: NEGR    Rn                 Rn = XXXX
@TTTTTTTTTT(XXX) XXXX: MULR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: DIVR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: MODR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
                                                                                                       <--- NOTES --------------------------------------------------------------------------------> 
@TTTTTTTTTT(XXX) XXXX: LDR     Rn,XXXX            Rn = XXXX @XXXX             AA..'D'label..AA         Rn,O16           OpCode:0X01:Rn:O16       O16 = <label>
@TTTTTTTTTT(XXX) XXXX: LDR     Rn,@Rn2            Rn = XXXX @XXXX Rn2(XXXX)                            Rn,@Rn2          OpCode:0X02:Rn:Rn2
@TTTTTTTTTT(XXX) XXXX: LDR     Rn,XXXX,Rnx        Rn = XXXX @XXXX Rnx(XXXX)   AA..'D'label..AA         Rn,O16,Rnx       OpCode:0X04:Rn:O16:Rnx   O16 = <label>
@TTTTTTTTTT(XXX) XXXX: LDR     Rn,#XXXX           Rn = XXXX @XXXX             AA..'E'-or-'D'label..AA  Rn,#O16          OpCode:0X10:Rn:O16       O16 = <label> or <integer>, <boolean>, <character>
@TTTTTTTTTT(XXX) XXXX: LDR     Rn,FB:DDDDD        Rn = XXXX @XXXX                                      Rn,FB:O16        OpCode:0X21:Rn:O16       O16 = <integer>
@TTTTTTTTTT(XXX) XXXX: LDR     Rn,@FB:DDDDD       Rn = XXXX @XXXX                                      Rn,@FB:O16       OpCode:0X22:Rn:O16       O16 = <integer>
@TTTTTTTTTT(XXX) XXXX: LDR     Rn,FB:DDDDD,Rnx    Rn = XXXX @XXXX Rnx(XXXX)                            Rn,FB:O16,Rnx    OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
@TTTTTTTTTT(XXX) XXXX: LDR     Rn,@FB:DDDDD,Rnx   Rn = XXXX @XXXX Rnx(XXXX)                            Rn,@FB:O16,Rnx   OpCode:0X26:Rn:O16:Rnx   O16 = <integer>

@TTTTTTTTTT(XXX) XXXX: LDAR    Rn,XXXX            Rn = XXXX                   AA..'D'label..AA         Rn,O16           OpCode:0X01:Rn:O16       O16 = <label>
@TTTTTTTTTT(XXX) XXXX: LDAR    Rn,@Rn2            Rn = XXXX Rn2(XXXX)                                  Rn,@Rn2          OpCode:0X02:Rn:Rn2
@TTTTTTTTTT(XXX) XXXX: LDAR    Rn,XXXX,Rnx        Rn = XXXX Rnx(XXXX)         AA..'D'label..AA         Rn,O16,Rnx       OpCode:0X04:Rn:O16:Rnx   O16 = <label>
@TTTTTTTTTT(XXX) XXXX: LDAR    Rn,FB:DDDDD        Rn = XXXX                                            Rn,FB:O16        OpCode:0X21:Rn:O16       O16 = <integer>
@TTTTTTTTTT(XXX) XXXX: LDAR    Rn,@FB:DDDDD       Rn = XXXX                                            Rn,@FB:O16       OpCode:0X22:Rn:O16       O16 = <integer>
@TTTTTTTTTT(XXX) XXXX: LDAR    Rn,FB:DDDDD,Rnx    Rn = XXXX Rnx(XXXX)                                  Rn,FB:O16,Rnx    OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
@TTTTTTTTTT(XXX) XXXX: LDAR    Rn,@FB:DDDDD,Rnx   Rn = XXXX Rnx(XXXX)                                  Rn,@FB:O16,Rnx   OpCode:0X26:Rn:O16:Rnx   O16 = <integer>

@TTTTTTTTTT(XXX) XXXX: STR     Rn,XXXX            Rn(XXXX @XXXX)              AA..'D'label..AA         Rn,O16           OpCode:0X01:Rn:O16       O16 = <label>
@TTTTTTTTTT(XXX) XXXX: STR     Rn,@Rn2            Rn(XXXX @XXXX) Rn2(XXXX)                             Rn,@Rn2          OpCode:0X02:Rn:Rn2
@TTTTTTTTTT(XXX) XXXX: STR     Rn,XXXX,Rnx        Rn(XXXX @XXXX) Rnx(XXXX)    AA..'D'label..AA         Rn,O16,Rnx       OpCode:0X04:Rn:O16:Rnx   O16 = <label>
@TTTTTTTTTT(XXX) XXXX: STR     Rn,FB:DDDDD        Rn(XXXX @XXXX)                                       Rn,FB:O16        OpCode:0X21:Rn:O16       O16 = <integer>
@TTTTTTTTTT(XXX) XXXX: STR     Rn,@FB:DDDDD       Rn(XXXX @XXXX)                                       Rn,@FB:O16       OpCode:0X22:Rn:O16       O16 = <integer>
@TTTTTTTTTT(XXX) XXXX: STR     Rn,FB:DDDDD,Rnx    Rn(XXXX @XXXX) Rnx(XXXX)                             Rn,FB:O16,Rnx    OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
@TTTTTTTTTT(XXX) XXXX: STR     Rn,@FB:DDDDD,Rnx   Rn(XXXX @XXXX) Rnx(XXXX)                             Rn,@FB:O16,Rnx   OpCode:0X26:Rn:O16:Rnx   O16 = <integer>

@TTTTTTTTTT(XXX) XXXX: COPYR   Rn,Rn2             Rn = XXXX Rn2(XXXX)
@TTTTTTTTTT(XXX) XXXX: PUSHR   Rn                 Rn(XXXX)  SP = XXXX
@TTTTTTTTTT(XXX) XXXX: POPR    Rn                 Rn = XXXX SP = XXXX
@TTTTTTTTTT(XXX) XXXX: SWAPR   Rn,Rn2             Rn = XXXX Rn2 = XXXX

@TTTTTTTTTT(XXX) XXXX: PUSHFB                     FB(XXXX)  SP = XXXX
@TTTTTTTTTT(XXX) XXXX: POPFB                      FB = XXXX SP = XXXX
@TTTTTTTTTT(XXX) XXXX: SETFB   #XXXX              FB = XXXX SP(XXXX)
@TTTTTTTTTT(XXX) XXXX: ADJSP   #XXXX              SP = XXXX

where variable fields have the following meanings

   TTTTTTTTTT       specifies a 10-dit S16clock value
   (XXX)            specifies a 3-dit handle of process executing instruction
   DDDDD            specifies a 1-to-5-dit WORD value (5 decimal digits)
   XX               specifies a 2-nibble BYTE value (2 hexadecimal digits)
   XXXX             specifies a 4-nibble WORD value (4 hexadecimal digits)
   SP = XXXX        means SP changed to XXXX as a result of instruction execution
   FB = XXXX        means FB changed to XXXX as a result of instruction execution
   FB(XXXX)         means FB value XXXX was unchanged as a result of instruction execution
   Rn = XXXX        means Rn changed to XXXX as a result of instruction execution
   Rn(XXXX)         means Rn value XXXX was unchanged as a result of instruction execution
   DBase = XXXX     means DBase changed to XXXX as a result of instruction execution
   DSize = XXXX     means DSize changed to XXXX as a result of instruction execution
   AA..'?'label..AA is the label used in the operand field of the assembly language instruction where
                       label named a '?' = 'C'(ode), 'D'(ata), or 'E'(QU) definition
--------------------------------------------------*/

//--------------------------------------------------
void PreExecuteHWInstruction()
//--------------------------------------------------
{

   // TRACE-ing
   if ( ENABLE_TRACING && TRACE_INSTRUCTIONS )
   {
      WORD PC;
      BYTE HOB,LOB;
      int index;
      bool found;
   
      PC = GetPC();
      FindByValueLabelTable(&pcbInRunState->labelTable,PC,'C',&index,&found);
      if ( found )
         fprintf( TRACE,"%16c %s:\n",' ',GetLexemeLabelTable(&pcbInRunState->labelTable,index));
      sprintf(field1,"@%10d(%3d) %04X:",S16clock,pcbInRunState->handle,(int) PC);
      ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,PC,&opcode);
      switch ( opcode )
      {
      /* ============= */
      /* CHOOSE        */
      /* ============= */
         case 0X00: // NOOP
            strcpy(field2,"NOOP");
            field3[0] = '\0';
            field5[0] = '\0';
            break;
         case 0X01: // JMP     XXXX                                           AA..'C'label..AA
            strcpy(field2,"JMP");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&LOB);
            sprintf(field3,"%04X",MAKEWORD(HOB,LOB));
            FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'C',&index,&found);
            if ( found )
               strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
            else
               field5[0] = '\0';
            break;
         case 0X02: // JMPN    Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            strcpy(field2,"JMPN");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&LOB);
            sprintf(field3,"R%d,%04X",(int) Rn,MAKEWORD(HOB,LOB));
            FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'C',&index,&found);
            if ( found )
               strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
            else
               field5[0] = '\0';
            break;
         case 0X03: // JMPNN   Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            strcpy(field2,"JMPNN");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&LOB);
            sprintf(field3,"R%d,%04X",(int) Rn,MAKEWORD(HOB,LOB));
            FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'C',&index,&found);
            if ( found )
               strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
            else
               field5[0] = '\0';
            break;
         case 0X04: // JMPZ    Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            strcpy(field2,"JMPZ");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&LOB);
            sprintf(field3,"R%d,%04X",(int) Rn,MAKEWORD(HOB,LOB));
            FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'C',&index,&found);
            if ( found )
               strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
            else
               field5[0] = '\0';
            break;
         case 0X05: // JMPNZ   Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            strcpy(field2,"JMPNZ");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&LOB);
            sprintf(field3,"R%d,%04X",(int) Rn,MAKEWORD(HOB,LOB));
            FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'C',&index,&found);
            if ( found )
               strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
            else
               field5[0] = '\0';
            break;
         case 0X06: // JMPP    Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            strcpy(field2,"JMPP");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&LOB);
            sprintf(field3,"R%d,%04X",(int) Rn,MAKEWORD(HOB,LOB));
            FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'C',&index,&found);
            if ( found )
               strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
            else
               field5[0] = '\0';
            break;
         case 0X07: // JMPNP   Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
//***BUGFIX 9.12.2020 found by Mujeeb Adelekan (FA2020)      
//          strcpy(field2,"JMPP");
            strcpy(field2,"JMPNP");
//***ENDBUGFIX
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&LOB);
            sprintf(field3,"R%d,%04X",(int) Rn,MAKEWORD(HOB,LOB));
            FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'C',&index,&found);
            if ( found )
               strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
            else
               field5[0] = '\0';
            break;
         case 0X08: // CALL    XXXX               SP = XXXX                   AA..'C'label..AA
            strcpy(field2,"CALL");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&LOB);
            sprintf(field3,"%04X",MAKEWORD(HOB,LOB));
            FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'C',&index,&found);
            if ( found )
               strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
            else
               field5[0] = '\0';
            break;
         case 0X09: // RET                        SP = XXXX 
            strcpy(field2,"RET");
            field3[0] = '\0';
            field5[0] = '\0';
            break;
         case 0X0A: // SVC     #XXXX              R0 = XXXX R15(XXXX)         AA..'E'label..AA
            strcpy(field2,"SVC");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&LOB);
            sprintf(field3,"#%04X",MAKEWORD(HOB,LOB));
            FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'E',&index,&found);
            if ( found )
               strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
            else
               field5[0] = '\0';
            break;
         case 0X0B: // DEBUG
            strcpy(field2,"DEBUG");
            field3[0] = '\0';
            field5[0] = '\0';
            break;
      /* ============= */
      /* COMPUTE       */
      /* ============= */
         case 0X20: // ADDR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"ADDR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X21: // SUBR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"SUBR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X22: // INCR    Rn                 Rn = XXXX
            strcpy(field2,"INCR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X23: // DECR    Rn                 Rn = XXXX
            strcpy(field2,"DECR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X24: // ZEROR   Rn                 Rn = XXXX
            strcpy(field2,"ZEROR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X25: // LSRR    Rn                 Rn = XXXX
            strcpy(field2,"LSRR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X26: // ASRR    Rn                 Rn = XXXX
            strcpy(field2,"ASRR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X27: // SLR     Rn                 Rn = XXXX
            strcpy(field2,"SLR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X28: // CMPR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"CMPR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X29: // CMPUR   Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"CMPUR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X2A: // ANDR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"ANDR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X2B: // ORR     Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"ORR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X2C: // XORR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"XORR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X2D: // NOTR    Rn                 Rn = XXXX
            strcpy(field2,"NOTR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X2E: // NEGR    Rn                 Rn = XXXX
            strcpy(field2,"NEGR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X2F: // MULRR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"MULR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X30: // DIVR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"DIVR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X31: // MODR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"MODR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
      /* ============= */
      /* COPY          */
      /* ============= */
         case 0X40: // LDR    Rn,operands
            strcpy(field2,"LDR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&mode);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn);
            switch ( mode )
            {
               case 0X01: // Rn,XXXX            Rn = XXXX @XXXX             AA..'D'label..AA         Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,%04X",(int) Rn,MAKEWORD(HOB,LOB));
                  FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'D',&index,&found);
                  if ( found )
                     strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
                  else
                     field5[0] = '\0';
                  break;
               case 0X02: // Rn,@Rn2            Rn = XXXX @XXXX Rn2(XXXX)                            Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&Rn2);
                  sprintf(field3,"R%d,@R%d",(int) Rn,(int) Rn2);
                  field5[0] = '\0';
                  break;
               case 0X04: // Rn,XXXX,Rnx        Rn = XXXX @XXXX Rnx(XXXX)   AA..'D'label..AA         Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+5),&Rnx);
                  sprintf(field3,"R%d,%04X,R%d",(int) Rn,MAKEWORD(HOB,LOB),(int) Rnx);
                  FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'D',&index,&found);
                  if ( found )
                     strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
                  else
                     field5[0] = '\0';
                  break;
               case 0X10: // Rn,#XXXX           Rn = XXXX @XXXX             AA..'E'-or-'D'label..AA  Rn,#O16         OpCode:0X10:Rn:O16       O16 = <label> or <integer>, <boolean>, <character>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,#%04X",(int) Rn,MAKEWORD(HOB,LOB));
                  FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'D',&index,&found);
               // Provides false-positive whenever MAKEWORD(HOB,LOB) is the same as the value of a data-segment RW,DW,DS or EQU label
                  if ( found )
                     strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
                  else
                  {
                     FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'E',&index,&found);
                     if ( found )
                        strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
                     else
                        field5[0] = '\0';
                  }
                  break;
               case 0X21: // Rn,FB:DDDDD        Rn = XXXX @XXXX                                      Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,FB:%d",(int) Rn,MAKEWORD(HOB,LOB));
                  field5[0] = '\0';
                  break;
               case 0X22: // Rn,@FB:DDDDD       Rn = XXXX @XXXX                                      Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,@FB:%d",(int) Rn,MAKEWORD(HOB,LOB));
                  field5[0] = '\0';
                  break;
               case 0X25: // Rn,FB:DDDDD,Rnx    Rn = XXXX @XXXX Rnx(XXXX)                            Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+5),&Rnx);
                  sprintf(field3,"R%d,FB:%d,R%d",(int) Rn,MAKEWORD(HOB,LOB),(int) Rnx);
                  field5[0] = '\0';
                  break;
               case 0X26: // Rn,@FB:DDDDD,Rnx   Rn = XXXX @XXXX Rnx(XXXX)                            Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+5),&Rnx);
                  sprintf(field3,"R%d,@FB:%d,R%d",(int) Rn,MAKEWORD(HOB,LOB),(int) Rnx);
                  field5[0] = '\0';
                  break;
            }
            break;
         case 0X41: // LDAR   Rn,operands
            strcpy(field2,"LDAR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&mode);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn);
            switch ( mode )
            {
               case 0X01: // Rn,XXXX            Rn = XXXX                   AA..'D'label..AA         Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,%04X",(int) Rn,MAKEWORD(HOB,LOB));
                  FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'D',&index,&found);
                  if ( found )
                     strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
                  else
                     field5[0] = '\0';
                  break;
               case 0X02: // Rn,@Rn2            Rn = XXXX Rn2(XXXX)                                  Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&Rn2);
                  sprintf(field3,"R%d,@R%d",(int) Rn,(int) Rn2);
                  field5[0] = '\0';
                  break;
               case 0X04: // Rn,XXXX,Rnx        Rn = XXXX Rnx(XXXX)         AA..'D'label..AA         Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+5),&Rnx);
                  sprintf(field3,"R%d,%04X,R%d",(int) Rn,MAKEWORD(HOB,LOB),(int) Rnx);
                  FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'D',&index,&found);
                  if ( found )
                     strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
                  else
                     field5[0] = '\0';
                  break;
               case 0X21: // Rn,FB:DDDDD        Rn = XXXX                                            Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,FB:%d",(int) Rn,MAKEWORD(HOB,LOB));
                  field5[0] = '\0';
                  break;
               case 0X22: // Rn,@FB:DDDDD       Rn = XXXX                                            Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,@FB:%d",(int) Rn,MAKEWORD(HOB,LOB));
                  field5[0] = '\0';
                  break;
               case 0X25: // Rn,FB:DDDDD,Rnx    Rn = XXXX Rnx(XXXX)                                  Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+5),&Rnx);
                  sprintf(field3,"R%d,FB:%d,R%d",(int) Rn,MAKEWORD(HOB,LOB),(int) Rnx);
                  field5[0] = '\0';
                  break;
               case 0X26: // Rn,@FB:DDDDD,Rnx   Rn = XXXX Rnx(XXXX)                                  Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+5),&Rnx);
                  sprintf(field3,"R%d,@FB:%d,R%d",(int) Rn,MAKEWORD(HOB,LOB),(int) Rnx);
                  field5[0] = '\0';
                  break;
            }
            break;
         case 0X42: // STR   Rn,operands
            strcpy(field2,"STR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&mode);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn);
            switch ( mode )
            {
               case 0X01: // Rn,XXXX            Rn = XXXX @XXXX             AA..'D'label..AA         Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,%04X",(int) Rn,MAKEWORD(HOB,LOB));
                  FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'D',&index,&found);
                  if ( found )
                     strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
                  else
                     field5[0] = '\0';
                  break;
               case 0X02: // Rn,@Rn2            Rn = XXXX @XXXX Rn2(XXXX)                            Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&Rn2);
                  sprintf(field3,"R%d,@R%d",(int) Rn,(int) Rn2);
                  field5[0] = '\0';
                  break;
               case 0X04: // Rn,XXXX,Rnx        Rn = XXXX @XXXX Rnx(XXXX)   AA..'D'label..AA         Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+5),&Rnx);
                  sprintf(field3,"R%d,%04X,R%d",(int) Rn,MAKEWORD(HOB,LOB),(int) Rnx);
                  FindByValueLabelTable(&pcbInRunState->labelTable,MAKEWORD(HOB,LOB),'D',&index,&found);
                  if ( found )
                     strncpy(field5,GetLexemeLabelTable(&pcbInRunState->labelTable,index),42);
                  else
                     field5[0] = '\0';
                  break;
               case 0X21: // Rn,FB:DDDDD        Rn = XXXX @XXXX                                      Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,FB:%d",(int) Rn,MAKEWORD(HOB,LOB));
                  field5[0] = '\0';
                  break;
               case 0X22: // Rn,@FB:DDDDD       Rn = XXXX @XXXX                                      Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  sprintf(field3,"R%d,@FB:%d",(int) Rn,MAKEWORD(HOB,LOB));
                  field5[0] = '\0';
                  break;
               case 0X25: // Rn,FB:DDDDD,Rnx    Rn = XXXX @XXXX Rnx(XXXX)                            Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+5),&Rnx);
                  sprintf(field3,"R%d,FB:%d,R%d",(int) Rn,MAKEWORD(HOB,LOB),(int) Rnx);
                  field5[0] = '\0';
                  break;
               case 0X26: // Rn,@FB:DDDDD,Rnx   Rn = XXXX @XXXX Rnx(XXXX)                            Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+3),&HOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+4),&LOB);
                  ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+5),&Rnx);
                  sprintf(field3,"R%d,@FB:%d,R%d",(int) Rn,MAKEWORD(HOB,LOB),(int) Rnx);
                  field5[0] = '\0';
                  break;
            }
            break;
         case 0X43: // COPYR   Rn,Rn2             Rn = XXXX Rn2(XXXX)
            strcpy(field2,"COPYR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X44: // PUSHR   Rn                 Rn(XXXX)  SP = XXXX
            strcpy(field2,"PUSHR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X45: // POPR    Rn                 Rn = XXXX SP = XXXX
            strcpy(field2,"POPR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            sprintf(field3,"R%d",(int) Rn);
            field5[0] = '\0';
            break;
         case 0X46: // SWAPR   Rn,Rn2             Rn = XXXX Rn2 = XXXX
            strcpy(field2,"SWAPR");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&Rn);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&Rn2);
            sprintf(field3,"R%d,R%d",(int) Rn,(int) Rn2);
            field5[0] = '\0';
            break;
         case 0X50: // PUSHFB                     FB(XXXX)  SP = XXXX
            strcpy(field2,"PUSHFB");
            field3[0] = '\0';
            field5[0] = '\0';
            break;
         case 0X51: // POPFB                      FB = XXXX SP = XXXX
            strcpy(field2,"POPFB");
            field3[0] = '\0';
            field5[0] = '\0';
            break;
         case 0X52: // SETFB   #XXXX              FB = XXXX SP(XXXX)
            strcpy(field2,"SETFB");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&LOB);
            sprintf(field3,"#%04X",MAKEWORD(HOB,LOB));
            field5[0] = '\0';
            break;
         case 0X53: // ADJSP   #XXXX              SP = XXXX
            strcpy(field2,"ADJSP");
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+1),&HOB);
            ReadCodeLogicalMainMemory(pcbInRunState->MMURegisters,(WORD)(PC+2),&LOB);
            sprintf(field3,"#%04X",MAKEWORD(HOB,LOB));
            field5[0] = '\0';
            break;
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
            INR    Rn,integer   
            OUTR   Rn,integer   
            LDMMU O16
            STMMU O16
            STOP
   */
      /* ============= */
      /* UNKNOWN       */
      /* ============= */
            strcpy(field2,"?????");
            break;
      }
   }
   // ENDTRACE-ing

}

//--------------------------------------------------
void PostExecuteHWInstruction(const WORD memoryEA,const WORD memoryWORD)
//--------------------------------------------------
{

   // TRACE-ing
   if ( ENABLE_TRACING && TRACE_INSTRUCTIONS )
   {
      switch ( opcode )
      {
      /* ============= */
      /* CHOOSE        */
      /* ============= */
         case 0X00: // NOOP
            field4[0] = '\0';
            break;
         case 0X01: // JMP     XXXX                                           AA..'C'label..AA
            field4[0] = '\0';
            break;
         case 0X02: // JMPN    Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            sprintf(field4,"R%d(%04X)",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X03: // JMPNN   Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            sprintf(field4,"R%d(%04X)",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X04: // JMPZ    Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            sprintf(field4,"R%d(%04X)",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X05: // JMPNZ   Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            sprintf(field4,"R%d(%04X)",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X06: // JMPP    Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            sprintf(field4,"R%d(%04X)",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X07: // JMPNP   Rn,XXXX            Rn(XXXX)                    AA..'C'label..AA
            sprintf(field4,"R%d(%04X)",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X08: // CALL    XXXX               SP = XXXX                   AA..'C'label..AA
            sprintf(field4,"SP = %04X",(int) GetSP());
            break;
         case 0X09: // RET                        SP = XXXX 
            sprintf(field4,"SP = %04X",(int) GetSP());
            break;
         case 0X0A: // SVC     #XXXX              R0 = XXXX R15(XXXX)         AA..'E'-or-'D'label..AA
            sprintf(field4,"R0 = %04X R15(%04X)",(int) GetRn(0),(int) GetRn(15));
            break;
         case 0X0B: // DEBUG
            field4[0] = '\0';
            break;
      /* ============= */
      /* COMPUTE       */
      /* ============= */
         case 0X20: // ADDR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X21: // SUBR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X22: // INCR    Rn                 Rn = XXXX
            sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X23: // DECR    Rn                 Rn = XXXX
            sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X24: // ZEROR   Rn                 Rn = XXXX
            sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X25: // LSRR    Rn                 Rn = XXXX
            sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X26: // ASRR    Rn                 Rn = XXXX
            sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X27: // SLR     Rn                 Rn = XXXX
            sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X28: // CMPR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X29: // CMPUR   Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X2A: // ANDR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X2B: // ORR     Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X2C: // XORR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X2D: // NOTR    Rn                 Rn = XXXX
            sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X2E: // NEGR    Rn                 Rn = XXXX
            sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
            break;
         case 0X2F: // MULR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X30: // DIVR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X31: // MODR    Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
      /* ============= */
      /* COPY          */
      /* ============= */
         case 0X40: // LDR    Rn,operands
            switch ( mode )
            {
               case 0X01: // Rn,XXXX            Rn = XXXX @XXXX             AA..'D'label..AA         Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
                  sprintf(field4,"R%d = %04X @%04X",(int) Rn,(int) GetRn((int) Rn),memoryEA);
                  break;
               case 0X02: // Rn,@Rn2            Rn = XXXX @XXXX Rn2(XXXX)                            Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  sprintf(field4,"R%d = %04X @%04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA,
                                                              (int) Rn2,(int) GetRn((int) Rn2));
                  break;
               case 0X04: // Rn,XXXX,Rnx        Rn = XXXX @XXXX Rnx(XXXX)   AA..'D'label..AA         Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
                  sprintf(field4,"R%d = %04X @%04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA,
                                                              (int) Rnx,(int) GetRn((int) Rnx));
                  break;
               case 0X10: // Rn,#XXXX           Rn = XXXX @XXXX             AA..'E'-or-'D'label..AA  Rn,#O16         OpCode:0X10:Rn:O16       O16 = <label> or <integer>, <boolean>, <character>
                  sprintf(field4,"R%d = %04X @%04X",(int) Rn,(int) GetRn((int) Rn),memoryEA);
                  break;
               case 0X21: // Rn,FB:DDDDD        Rn = XXXX @XXXX                                      Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
                  sprintf(field4,"R%d = %04X @%04X",(int) Rn,(int) GetRn((int) Rn),memoryEA);
                  break;
               case 0X22: // Rn,@FB:DDDDD       Rn = XXXX @XXXX                                      Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
                  sprintf(field4,"R%d = %04X @%04X",(int) Rn,(int) GetRn((int) Rn),memoryEA);
                  break;
               case 0X25: // Rn,FB:DDDDD,Rnx    Rn = XXXX @XXXX Rnx(XXXX)                            Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
                  sprintf(field4,"R%d = %04X @%04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA,
                                                              (int) Rnx,(int) GetRn((int) Rnx));
                  break;
               case 0X26: // Rn,@FB:DDDDD,Rnx   Rn = XXXX @XXXX Rnx(XXXX)                            Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  sprintf(field4,"R%d = %04X @%04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA,
                                                              (int) Rnx,(int) GetRn((int) Rnx));
                  break;
            }
            break;
         case 0X41: // LDAR   Rn,operands
            switch ( mode )
            {
               case 0X01: // Rn,XXXX            Rn = XXXX                   AA..'D'label..AA         Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
                  sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
                  break;
               case 0X02: // Rn,@Rn2            Rn = XXXX @XXXX Rn2(XXXX)                            Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  sprintf(field4,"R%d = %04X @%04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA,
                                                              (int) Rn2,(int) GetRn((int) Rn2));
                  break;
               case 0X04: // Rn,XXXX,Rnx        Rn = XXXX Rnx(XXXX)         AA..'D'label..AA         Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
                  sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                        (int) Rnx,(int) GetRn((int) Rnx));
                  break;
               case 0X10: // Rn,#XXXX           Rn = XXXX                   AA..'E'-or-'D'label..AA  Rn,#O16         OpCode:0X10:Rn:O16       O16 = <label> or <integer>, <boolean>, <character>
                  sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
                  break;
               case 0X21: // Rn,FB:DDDDD        Rn = XXXX                                            Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
                  sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
                  break;
               case 0X22: // Rn,@FB:DDDDD       Rn = XXXX                                            Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
                  sprintf(field4,"R%d = %04X",(int) Rn,(int) GetRn((int) Rn));
                  break;
               case 0X25: // Rn,FB:DDDDD,Rnx    Rn = XXXX Rnx(XXXX)                                  Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
                  sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                        (int) Rnx,(int) GetRn((int) Rnx));
                  break;
               case 0X26: // Rn,@FB:DDDDD,Rnx   Rn = XXXX Rnx(XXXX)                                  Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                        (int) Rnx,(int) GetRn((int) Rnx));
                  break;
            }
            break;
         case 0X42: // STR    Rn,operands
            switch ( mode )
            {
               case 0X01: // Rn,XXXX            Rn(XXXX @XXXX)              AA..'D'label..AA         Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
                  sprintf(field4,"R%d(%04X @%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA);
                  break;
               case 0X02: // Rn,@Rn2            Rn(XXXX @XXXX) Rn2(XXXX)                             Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  sprintf(field4,"R%d(%04X @%04X) R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA,
                                                             (int) Rn2,(int) GetRn((int) Rn2));
                  break;
               case 0X04: // Rn,XXXX,Rnx        Rn(XXXX @XXXX) Rnx(XXXX)    AA..'D'label..AA         Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
                  sprintf(field4,"R%d(%04X @%04X) R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA,
                                                             (int) Rnx,(int) GetRn((int) Rnx));
                  break;
               case 0X21: // Rn,FB:DDDDD        Rn(XXXX @XXXX)                                       Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
                  sprintf(field4,"R%d(%04X @%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA);
                  break;
               case 0X22: // Rn,@FB:DDDDD       Rn(XXXX @XXXX)                                       Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
                  sprintf(field4,"R%d(%04X @%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA);
                  break;
               case 0X25: // Rn,FB:DDDDD,Rnx    Rn(XXXX @XXXX) Rnx(XXXX)                             Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
                  sprintf(field4,"R%d(%04X @%04X) R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA,
                                                             (int) Rnx,(int) GetRn((int) Rnx));
                  break;
               case 0X26: // Rn,@FB:DDDDD,Rnx   Rn(XXXX @XXXX) Rnx(XXXX)                             Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  sprintf(field4,"R%d(%04X @%04X) R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),memoryEA,
                                                             (int) Rnx,(int) GetRn((int) Rnx));
                  break;
            }
            break;
         case 0X43: // COPYR   Rn,Rn2             Rn = XXXX Rn2(XXXX)
            sprintf(field4,"R%d = %04X R%d(%04X)",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X44: // PUSHR   Rn                 Rn(XXXX)  SP = XXXX
            sprintf(field4,"R%d(%04X) SP = %04X",(int) Rn,(int) GetRn((int) Rn),
                                                 (int) GetSP());
            break;
         case 0X45: // POPR    Rn                 Rn = XXXX SP = XXXX
            sprintf(field4,"R%d = %04X SP = %04X",(int) Rn,(int) GetRn((int) Rn),
                                                  (int) GetSP());
            break;
         case 0X46: // SWAPR   Rn,Rn2             Rn = XXXX Rn2 = XXXX
            sprintf(field4,"R%d = %04X R%d = %04X",(int) Rn,(int) GetRn((int) Rn),
                                                   (int) Rn2,(int) GetRn((int) Rn2));
            break;
         case 0X50: // PUSHFB                     FB(XXXX)  SP = XXXX
            sprintf(field4,"FB(%04X) SP = %04X",(int) GetFB(),
                                                (int) GetSP());
            break;
         case 0X51: // POPFB                      FB = XXXX SP = XXXX
            sprintf(field4,"FB = %04X SP = %04X",(int) GetFB(),
                                                 (int) GetSP());
            break;
         case 0X52: // SETFB   #XXXX              FB = XXXX SP(XXXX)
            sprintf(field4,"FB = %04X SP(%04X)",(int) GetFB(),
                                                (int) GetSP());
            break;
         case 0X53: // ADJSP   #XXXX              SP = XXXX
            sprintf(field4,"SP = %04X",(int) GetSP());
            break;
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
            INR    Rn,integer   
            OUTR   Rn,integer   
            LDMMU O16
            STMMU O16
            STOP
   */
      /* ============= */
      /* UNKNOWN       */
      /* ============= */
            break;
      }
   
   //----------------------------------------------------------------
   // ***DO NOT DELETE***
   // Included in case PreExecuteHWInstruction() strncpy(field5,...,42)s did not leave the string field5 with a NUL character terminator!?
      field5[42] = '\0';
   //----------------------------------------------------------------
      fprintf( TRACE,"%-22s %-7s %-18s %-27s %-42s\n",field1,field2,field3,field4,field5);
   }
   // ENDTRACE-ing

}

//=============================================================================
// S16 scanner (lexical analyzer)
//=============================================================================
//--------------------------------------------------
void GetNextToken(TOKEN *token,char lexeme[])
//--------------------------------------------------
{
   void GetNextCharacter();

   typedef struct RESERVEDWORD
   {
      char lexeme[30+1];
      TOKEN token;
   } RESERVEDWORD;

   const RESERVEDWORD reservedWords[] =
   {
      { "$JOB"                        ,SJOB                      },
      { "NAME"                        ,NAME                      },
      { "FILE"                        ,FILE2                     },
      { "STACK"                       ,STACK                     },
      { "PRIORITY"                    ,PRIORITY                  },
      { "ARRIVAL"                     ,ARRIVAL                   },
      { "$END"                        ,SEND                      },
      { "TRUE"                        ,TRUE                      },
      { "FALSE"                       ,FALSE                     },
      { "ENABLE_TRACING"              ,ENABLETRACING             },
      { "TRACE_INSTRUCTIONS"          ,TRACEINSTRUCTIONS         },
      { "TRACE_MEMORY_ALLOCATION"     ,TRACEMEMORYALLOCATION     },
      { "TRACE_SJF_SCHEDULING"        ,TRACESJFSCHEDULING        },
      { "TRACE_SCHEDULER"             ,TRACESCHEDULER            },
      { "TRACE_DISPATCHER"            ,TRACEDISPATCHER           },
      { "TRACE_QUEUES"                ,TRACEQUEUES               },
      { "TRACE_STATISTICS"            ,TRACESTATISTICS           },
      { "TRACE_HWINTERRUPTS"          ,TRACEHWINTERRUPTS         },
      { "TRACE_MISCELLANEOUS_SVC"     ,TRACEMISCELLANEOUSSVC     },
      { "TRACE_PROCESS_MANAGEMENT"    ,TRACEPROCESSMANAGEMENT    },
      { "TRACE_RESOURCE_MANAGEMENT"   ,TRACERESOURCEMANAGEMENT   },
      { "TRACE_TERMINAL_IO"           ,TRACETERMINALIO           },
      { "TRACE_DISK_IO"               ,TRACEDISKIO               },
      { "TRACE_MEMORYSEGMENTS"        ,TRACEMEMORYSEGMENTS       },
      { "TRACE_MESSAGEBOXES"          ,TRACEMESSAGEBOXES         },
      { "TRACE_SEMAPHORES"            ,TRACESEMAPHORES           },
      { "TRACE_MUTEXES"               ,TRACEMUTEXES              },
      { "TRACE_EVENTS"                ,TRACEEVENTS               },
      { "TRACE_PIPES"                 ,TRACEPIPES                },
      { "TRACE_DEADLOCK_DETECTION"    ,TRACEDEADLOCKDETECTION    },
      { "TRACE_SIGNALS"               ,TRACESIGNALS              },
      { "CPU_SCHEDULER"               ,CPUSCHEDULER              },
      { "FCFS_CPU_SCHEDULER"          ,FCFSCPUSCHEDULER          },
      { "PRIORITY_CPU_SCHEDULER"      ,PRIORITYCPUSCHEDULER      },
      { "MINIMUM_PRIORITY"            ,MINIMUMPRIORITY           },
      { "DEFAULT_PRIORITY"            ,DEFAULTPRIORITY           },
      { "MAXIMUM_PRIORITY"            ,MAXIMUMPRIORITY           },
      { "SJF_CPU_SCHEDULER"           ,SJFCPUSCHEDULER           },
      { "ALPHA"                       ,ALPHA                     },
      { "USE_PREEMPTIVE_CPU_SCHEDULER",USEPREEMPTIVECPUSCHEDULER },
      { "TIME_QUANTUM"                ,TIMEQUANTUM               },
      { "CONTEXT_SWITCH_TIME"         ,CONTEXTSWITCHTIME         },
      { "USE_S16CLOCK_QUANTUM"        ,USES16CLOCKQUANTUM        },
      { "S16CLOCK_QUANTUM"            ,S16CLOCKQUANTUM           },
      { "DEADLOCK_DETECTION_ALGORITHM",DEADLOCKDETECTIONALGORITHM},
      { "NO_DEADLOCK_DETECTION"       ,NODEADLOCKDETECTION       },
      { "DEADLOCK_DETECTION_METHOD1"  ,DEADLOCKDETECTIONMETHOD1  },
      { "DEADLOCK_DETECTION_METHOD2"  ,DEADLOCKDETECTIONMETHOD2  },
      { "MQ_SCHEDULER"                ,MQSCHEDULER               },
      { "FCFS_MQ_SCHEDULER"           ,FCFSMQSCHEDULER           },
      { "DISKIOQ_SCHEDULER"           ,DISKIOQSCHEDULER          },
      { "FCFS_DISKIOQ_SCHEDULER"      ,FCFSDISKIOQSCHEDULER      },
      { "MINIMUM_SVCWAITTIME"         ,MINIMUMSVCWAITTIME        },
      { "MAXIMUM_SVCWAITTIME"         ,MAXIMUMSVCWAITTIME        },
      { "DEFAULT_TERMINAL_PROMPT"     ,DEFAULTTERMINALPROMPT     },
      { "TRUE_STRING"                 ,TRUESTRING                },
      { "FALSE_STRING"                ,FALSESTRING               },
      { "DEFAULT_SSSIZE_IN_PAGES"     ,DEFAULTSSSIZEINPAGES      }
   };

   bool complete;

   do
   {
   // "eat" any whitespace (blanks and end-of-lines and TABs)
      while ( (nextCharacter == ' ')
           || (nextCharacter == EOLC)
           || (nextCharacter == '\t') )
         GetNextCharacter();

   /*
      "eat" any comments. Comments are always assumed to extend up to but does not include EOLC
      <comment> ::= ; {<character>}*
   */
      if ( nextCharacter == ';' )
         do
            GetNextCharacter();
         while ( nextCharacter != EOLC );
   } while ( (nextCharacter == ' ')
          || (nextCharacter == EOLC)
          || (nextCharacter == '\t') );
/*
   reserved words and <label>s

<label>                   ::= <letter> { (( <letter> | <digit> | _ )) }*

<letter>                  ::= A | B | ... | Z | a | b | ... | z

<digit>                   ::= 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9
*/
   if ( isalpha(nextCharacter) || (nextCharacter == '$') )
   {
      int i;
      char uLexeme[SOURCE_LINE_LENGTH+1];
      bool found;

      i = 0;
      lexeme[i++] = nextCharacter;
      GetNextCharacter();
      while ( isalpha(nextCharacter) ||
              isdigit(nextCharacter) ||
              (nextCharacter == '_') )
      {
         lexeme[i++] = nextCharacter;
         GetNextCharacter();
      }
      lexeme[i] = '\0';
      for (i = 0; i <= (int) strlen(lexeme); i++)
         uLexeme[i] = toupper(lexeme[i]);
      i = 0;
      found = false;
      do
      {
         if ( strcmp(reservedWords[i].lexeme,uLexeme) == 0 )
            found = true;
         else
            i++;
      } while ( (i <= sizeof(reservedWords)/sizeof(RESERVEDWORD)-1 ) && !found );
      if ( found )
         *token = reservedWords[i].token;
      else
         *token = UNKNOWN;
   }

/*
   <integer>                 ::= [ (( + | - )) ]    <digit>    {    <digit> }* 
                               | [ (( + | - )) ] 0X <hexdigit> { <hexdigit> }*

   <hexdigit>                ::= <digit>
                               | (( a | A )) | (( b | B )) | (( c | C )) 
                               | (( d | D )) | (( e | E )) | (( f | F ))

|| *Note* <fixedpoint> used in .config file parameters *ONLY*
   <fixedpoint>              ::= [ (( + | - )) ] <digit> { <digit> }* . <digit> { <digit> }* 
*/
   else if ( isdigit(nextCharacter) ||
             (nextCharacter == '+') ||
             (nextCharacter == '-') )
   {
      int i;

      i = 0;
      if ( (nextCharacter == '+') || (nextCharacter == '-') )
      {
         lexeme[i++] = nextCharacter;
         GetNextCharacter();
      }
      if ( nextCharacter == '0' )
      {
         lexeme[i++] = '0';
         GetNextCharacter();
         if      ( toupper(nextCharacter) == 'X' )
         {
            lexeme[i++] = 'X';
            GetNextCharacter();
            if ( !isxdigit(nextCharacter) )
            {
               *token = UNKNOWN; lexeme[i++] = nextCharacter; lexeme[i] = '\0'; GetNextCharacter();
            }
            else
            {
               do
               {
                  lexeme[i++] = nextCharacter;
                  GetNextCharacter();
               } while ( isxdigit(nextCharacter) ); 
               *token = INTEGER;
               lexeme[i] = '\0';
            }
         }
         else if ( nextCharacter == '.' )
         {
            lexeme[i++] = '.';
            GetNextCharacter();
            if ( !isdigit(nextCharacter) )
            {
               *token = UNKNOWN; lexeme[i++] = nextCharacter; lexeme[i] = '\0'; GetNextCharacter();
            }
            else
            {
               do
               {
                  lexeme[i++] = nextCharacter;
                  GetNextCharacter();
               } while  ( isdigit(nextCharacter) );
               *token = FIXEDPOINT;
               lexeme[i] = '\0';
            }
         }
         else if ( !isdigit(nextCharacter) )   // single-digit 0
         {
            *token = INTEGER;
            lexeme[i] = '\0';
         }
         else // '0' *NOT* followed by 'X' and *NOT* single-digit 0
         {
            *token = UNKNOWN; lexeme[i++] = nextCharacter; lexeme[i] = '\0'; GetNextCharacter();
         }
      }
      else // digit after +/- is *NOT* '0'
      {
         if ( !isdigit(nextCharacter) )
         {
            *token = UNKNOWN; lexeme[i++] = nextCharacter; lexeme[i] = '\0'; GetNextCharacter();
         }
         else
         {
            do
            {
               lexeme[i++] = nextCharacter;
               GetNextCharacter();
            } while ( isdigit(nextCharacter) );
            if ( nextCharacter == '.' )
            {
               lexeme[i++] = '.';
               GetNextCharacter();
               if ( !isdigit(nextCharacter) )
               {
                  *token = UNKNOWN; lexeme[i++] = nextCharacter; lexeme[i] = '\0'; GetNextCharacter();
               }
               else
               {
                  do
                  {
                     lexeme[i++] = nextCharacter;
                     GetNextCharacter();
                  } while ( isdigit(nextCharacter) );
                  *token = FIXEDPOINT;
                  lexeme[i] = '\0';
               }
            }
            else
            {
               *token = INTEGER;
               lexeme[i] = '\0';
            }
         }
      }
   }
   else
   {
      switch ( nextCharacter )
      {
// <string> ::= " { <character> }* "
         case  '"': {
                       int i = 0;

                       *token = STRING;
                       complete = false;
                       do
                       {
                          GetNextCharacter();
                          if ( nextCharacter == '"' )
                          {
                             GetNextCharacter();
                             if ( nextCharacter == '"' )
                                lexeme[i++] = nextCharacter;
                             else
                                complete = true;
                          }
                          else if ( nextCharacter == EOLC )
                          {
                             complete = true;
                             GetNextCharacter();
                          }
                          else
                          {
                             lexeme[i++] = nextCharacter;
                          }
                       } while ( !complete );
                       lexeme[i] = '\0';
                    }
                    break;
         case EOFC: *token = EOFTOKEN;
                    lexeme[0] = '\0';
                    break;
         case  ',': *token = COMMA;
                    lexeme[0] = nextCharacter; lexeme[1] = '\0';
                    GetNextCharacter();
                    break;
         case  '=': *token = EQUAL;
                    lexeme[0] = nextCharacter; lexeme[1] = '\0';
                    GetNextCharacter();
                    break;
         default:   *token = UNKNOWN;
                    lexeme[0] = nextCharacter; lexeme[1] = '\0';
                    GetNextCharacter();
                    break;
      }
   }
}

//--------------------------------------------------
void GetNextCharacter()
//--------------------------------------------------
{
   void GetSourceLine();

   if ( atEOF )
      nextCharacter = EOFC;
   else
   {
      if ( atEOL )
         GetSourceLine();
      if ( sourceLineIndex <= ((int) strlen(sourceLine)-1) )
      {
         nextCharacter = sourceLine[sourceLineIndex];
         sourceLineIndex += 1;
      }
      else
      {
         nextCharacter = EOLC;
         atEOL = true;
      }
   }
}

//--------------------------------------------------
void GetSourceLine()
//--------------------------------------------------
{
   if ( feof(SOURCE) )
      atEOF = true;
   else
   {
      if ( fgets(sourceLine,SOURCE_LINE_LENGTH,SOURCE) == NULL )
         atEOF = true;
      else
      {
         if ( (strchr(sourceLine,'\n') == NULL) && !feof(SOURCE) )
            ProcessWarningOrError(S16WARNING,"******* Source line is too long!");
      // erase *ALL* control characters at end of source line (when found)
         while ( (0 <= (int) strlen(sourceLine)-1) &&
                 iscntrl(sourceLine[(int) strlen(sourceLine)-1]) )
            sourceLine[(int) strlen(sourceLine)-1] = '\0';
         atEOL = false;
         sourceLineIndex = 0;
         sourceLineNumber++;

         // TRACE-ing
         if ( ENABLE_TRACING )                
         {
         //   echo source line to trace file
               fprintf(TRACE,"%3d: %s\n",sourceLineNumber,sourceLine); fflush(TRACE);
         }
         // ENDTRACE-ing

      }
   }
}
//=============================================================================
// End of S16 code
//=============================================================================
