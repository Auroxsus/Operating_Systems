//----------------------------------------------------------------
// Dr. Art Hanna
// CS3350 S16Assembler for S16 Simulation
// S16Assembler.c
#define S16ASSEMBLER_VERSION "FA2022"
//----------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include "LabelTable.h"
#include "Computer.h"

#define SOURCE_LINE_LENGTH             512
#define EOLC                           1
#define EOFC                           2
#define SLASH                          '\\'
#define LABELTABLE_CAPACITY            500
#define LINES_PER_PAGE                 50

/*----------------------------------------------------------------
<S16Program>              ::= CODESEGMENT EOLTOKEN                        || CSBase = 0X0000 *ALWAYS*
                                 <CODESegmentStatements>
                              DATASEGMENT EOLTOKEN                        || DSBase = offset 0 of page that immediately
                                 <DATASegmentStatements>                  ||    follows the last code-segment byte
                              END

<CODESegmentStatements>   ::= { (( <label> EQU * | <CODESegmentHWStatement> )) EOLTOKEN }*

<CODESegmentHWStatement>  ::= [ <label> ] <HWMnemonic> [ <HWOperands>  ]

<HWMnemonic>              ::= NOOP | JMP | ... | PUSHSP                   || see complete list in Computer.h

<HWOperands>              ::= #(( <integer> | <label> )) 
                            | <label>
                            | <Rn>
                            | <Rn>,<label>
                            | <Rn>,#(( <integer> | <boolean> | <character> | <label> ))
                            | <Rn>,<Rn>
                            | <Rn>,@<Rn>
                            | <Rn>,<label>,<Rn>
                            | <Rn>,FB:<integer>
                            | <Rn>,@FB:<integer>
                            | <Rn>,FB:<integer>,<Rn>
                            | <Rn>,@FB:<integer>,<Rn>

<DATASegmentStatements>   ::= { <DATADefinitionStatement> EOLTOKEN }*

<DATADefinitionStatement> ::=   <label>   EQU (( <integer> | <boolean> | <character> | * ))
                            | [ <label> ] RW  [ {{ <integer> | <label> )) ]
                            | [ <label> ] DW  (( <label> | <integer> | <boolean> | <character> ))
                            | [ <label> ] DS  <string>

<Rn>                      ::= R0 | R1 | R2 | ... | R15

<string>                  ::= "{ <ASCIIcharacter> }*"                     || code an embedded " as ""

<label>                   ::= <letter> { (( <letter> | <digit> | _ )) }*

<integer>                 ::= [ (( + | - )) ]    <digit>    {    <digit> }* 
                            | [ (( + | - )) ] 0X <hexdigit> { <hexdigit> }*

<boolean>                 ::= TRUE | FALSE

<comment>                 ::= ; { <ASCIIcharacter> }* EOLTOKEN

<character>               ::= '<ASCIIcharacter>'                          || code an embedded ' as \'

<ASCIIcharacter>          ::= (blank) | ! | " | ... | } | ~               || any printable ASCII character

<letter>                  ::= A | B | ... | Z | a | b | ... | z           || upper- and lower-case letters

<digit>                   ::= 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9       || decimal digits

<hexdigit>                ::= <digit>
                            | (( a | A )) | (( b | B )) | (( c | C ))     || hexadecimal digits
                            | (( d | D )) | (( e | E )) | (( f | F ))
----------------------------------------------------------------*/

typedef enum
{
// pseudoterminals
      EOFTOKEN,
      EOLTOKEN,
      LABEL,
      INTEGER,
      STRING,
      CHARACTER,
      UNKNOWN,
// reserved words
      DATASEGMENT,
      CODESEGMENT,
      END,
//  boolean literals
      TRUE,
      FALSE,
//  CPU register names
      FB,
      R0,R1,R2,R3,R4,R5,R6,R7,R8,R9,R10,R11,R12,R13,R14,R15,
//  data-segment mnemonics
      RW,
      DW,
      DS,
//  shared data-segment/code-segment mnemonic
      EQU,
//  code-segment hardware operation mnemonics
      NOOP,
      JMP,
      JMPN,
      JMPNN,
      JMPZ,
      JMPNZ,
      JMPP,
      JMPNP,
      JMPT,
      JMPF,
      CALL,
      RET,
      SVC,
      DEBUG,
      ADDR,
      SUBR,
      INCR,
      DECR,
      ZEROR,
      LSRR,
      ASRR,
      SLR,
      CMPR,
      CMPUR,
      ANDR,
      ORR,
      XORR,
      NOTR,
      NEGR,
      MULR,
      DIVR,
      MODR,
      LDR,
      LDAR,
      STR,
      COPYR,
      PUSHR,
      POPR,
      SWAPR,
      PUSHFB,
      POPFB,
      SETFB,
      ADJSP,
//  delimiters
      COMMA,
      ATSIGN,
      POUNDSIGN,
      COLON,
      ASTERISK
} TOKEN;

//----------------------------------------------------------------
// global variables
//----------------------------------------------------------------
FILE *SOURCE,*LISTING,*OBJECT;
int pageNumber,linesOnPage;

char sourceLine[SOURCE_LINE_LENGTH+1],nextCharacter;
int  sourceLineIndex,sourceLineNumber;
bool atEOF,atEOL;

char WORKINGDIRECTORY[SOURCE_LINE_LENGTH+1];
char S16DIRECTORY[SOURCE_LINE_LENGTH+1];
char fullSOURCEFilename[SOURCE_LINE_LENGTH+1];

//----------------------------------------------------------------
int main(int argc,char *argv[])
//----------------------------------------------------------------
{
// globals accessed: WORKINGDIRECTORY,S16DIRECTORY,fullSOURCEFilename,SOURCE,LISTING,OBJECT

   void DoPass1(LABELTABLE *labelTable,int *CSBase,int *CSSize,int *DSBase,int *DSSize);
   void DoPass2(bool *syntaxErrorsFound,const LABELTABLE *labelTable,BYTE *memory,const int CSBase,const int DSBase);
   void BuildObjectFile(const LABELTABLE *labelTable,const BYTE *memory,
                        int CSBase,int CSSize,int DSBase,int DSSize);

   char SOURCEFilename[SOURCE_LINE_LENGTH],fullFilename[SOURCE_LINE_LENGTH];
   bool syntaxErrorsFound;
   char *pLastSlash;
   BYTE *memory;
   
   int CSBase,CSSize;
   int DSBase,DSSize;
// int SSBase,SSSize; both set by S16.c when job is LoadJobs()-ed
   LABELTABLE labelTable;

/*
   Assuming the S16 assembly source file "Sample1.s16" is stored in the same folder
   as the S16Assembler load module (".s16" is the only file name extension allowed
   for a S16 assembly language source file), S16Assembler can be run without a 
   command line argument (S16Assembler prompts for the source file name)

      C:\CS3350\S16>S16Assembler
      S16 source filename? Sample1

   or with the source file name provided as the only command line argument

      C:\CS3350\S16>S16Assembler Sample1

   Notice, only the first name part of the file name must be entered.
*/
   if      ( argc == 1 )
   {
      printf("S16 source filename [.s16]? ");
      gets(SOURCEFilename);
   }
   else if ( argc == 2 )
   {
      strcpy(SOURCEFilename,argv[1]);
   }
   else
   {
      printf("Too many command line arguments\n");
      system("PAUSE");
      exit( 1 );
   }

/*
   Determine path to working directory; that is, the folder that contains
      S16Assembler source .s16 files.
      
   **********************   
   ***MAC USERS BEWARE***
   **********************   
*/
   pLastSlash = strrchr(SOURCEFilename,SLASH);
   WORKINGDIRECTORY[0] = '\0';
   if ( pLastSlash != NULL )
   {
      int len = pLastSlash-SOURCEFilename+1;
      
      strncat(WORKINGDIRECTORY,SOURCEFilename,len);
      strcpy(SOURCEFilename,&SOURCEFilename[len]);
   }

/*
   set path the S16.config file

   **********************   
   ***MAC USERS BEWARE***
   **********************   
*/
   strcpy(S16DIRECTORY,".\\");

// open SOURCE file
   strcpy(fullSOURCEFilename,WORKINGDIRECTORY);
   strcat(fullSOURCEFilename,SOURCEFilename);
   strcat(fullSOURCEFilename,".s16");
   if ( (SOURCE = fopen(fullSOURCEFilename,"r")) == NULL )
   {
      printf("\aError opening source file \"%s\".\n",fullSOURCEFilename);
      system("PAUSE");
      exit( 1 );
   }
   printf("Using source file \"%s\".\n",fullSOURCEFilename);

// open LISTING file
   strcpy(fullFilename,WORKINGDIRECTORY);
   strcat(fullFilename,SOURCEFilename);
   strcat(fullFilename,".listing");
   if ( (LISTING = fopen(fullFilename,"w")) == NULL )
   {
      printf("\aError opening listing file \"%s\".\n",fullFilename);
      fclose(SOURCE);
      system("PAUSE");
      exit( 1 );
   }
   printf("Using listing file \"%s\".\n",fullFilename);

// initialize label table
   ConstructLabelTable(&labelTable,LABELTABLE_CAPACITY);
 
// do two-pass assembly
   DoPass1(&labelTable,&CSBase,&CSSize,&DSBase,&DSSize);
   memory = (BYTE *) malloc(sizeof(BYTE)*LOGICAL_ADDRESS_SPACE_IN_BYTES);
   assert( memory != NULL );
   rewind(SOURCE);
   DoPass2(&syntaxErrorsFound,&labelTable,memory,CSBase,DSBase);

// build OBJECT file
   if ( syntaxErrorsFound ) 
   {
      strcpy(fullFilename,WORKINGDIRECTORY);
      strcat(fullFilename,SOURCEFilename);
      strcat(fullFilename,".listing");
      printf("\aSyntax errors found during assembly. See \"%s\".\n",fullFilename," for details!\n");
      printf("Object file not created.\n");
   }
   else
   {
   // open OBJECT file
      strcpy(fullFilename,WORKINGDIRECTORY);
      strcat(fullFilename,SOURCEFilename);
      strcat(fullFilename,".object");
      if ( (OBJECT = fopen(fullFilename,"w")) == NULL )
      {
         printf("\aError opening object file \"%s\".\n",fullFilename);
         fclose(SOURCE);
         fclose(LISTING);
         system("PAUSE");
         exit( 1 );
      }
      printf("Using object file \"%s\".\n",fullFilename);

      BuildObjectFile(&labelTable,memory,CSBase,CSSize,DSBase,DSSize);
      fclose(OBJECT);
   }

   fclose(SOURCE);
   fclose(LISTING);
   system("PAUSE");
   return( 0 );
}

//----------------------------------------------------------------
void DoPass1(LABELTABLE *labelTable,int *CSBase,int *CSSize,int *DSBase,int *DSSize)
//----------------------------------------------------------------
{
// globals accessed: atEOF,sourceLineNumber,SOURCE,LISTING,OBJECT

   void GetNextNonEmptySourceLine(const bool listEmptySourceLines);
   void GetNextCharacter();
   void GetNextToken(TOKEN *token,char lexeme[]);

   TOKEN token;
   char lexeme[SOURCE_LINE_LENGTH+1];

   int index;
   bool found,inserted,labeled;
   int LC;
   
// initialize scanner
   atEOF = false;
   GetNextNonEmptySourceLine(false);
   GetNextCharacter();
   GetNextToken(&token,lexeme);

//================================================================
// code-segment assembly 
//================================================================
   if ( token != CODESEGMENT )
   {
      printf("\aCODESEGMENT missing near source line #%4d. Assembly aborted\n",sourceLineNumber);
      fclose(SOURCE);
      fclose(LISTING);
      fclose(OBJECT);
      system("PAUSE");
      exit( 1 );
   }

// parse <CODESegmentStatements> until DATASEGMENT (correct) or EOFTOKEN (error) one line at a time
   *CSBase = 0X0000;
   LC = *CSBase;
   GetNextNonEmptySourceLine(false);
   GetNextCharacter();
   GetNextToken(&token,lexeme);
   while ( !((token == DATASEGMENT) || (token == EOFTOKEN)) )
   {
      if ( token == LABEL )
      {
         FindByLexemeLabelTable(labelTable,lexeme,&index,&found);
         if ( !found )
         {
            InsertLabelTable(labelTable,lexeme,'C','A',LC,sourceLineNumber,&index,&inserted);
         // *Note* A code-segment "<label> EQU *" statement <label> is type-d as 'C', not 'E'.
            if ( !inserted )
            {
               printf("\aLabel table capacity exceeded near source line #%4d. Assembly aborted\n",sourceLineNumber);
               fclose(SOURCE);
               fclose(LISTING);
               fclose(OBJECT);
               system("PAUSE");
               exit( 1 );
            }
         }
         GetNextToken(&token,lexeme);
      }
      switch ( token )
      {
         case   EQU: // <label> EQU   *
            break;
      /* ============= */
      /* CHOOSE        */
      /* ============= */
         case   NOOP: //           OpCode
            LC += 1;
            break;
         case    JMP: // O16       OpCode:O16             O16 = <label>
            LC += 3;
            break;
         case   JMPN: // Rn,O16    OpCode:Rn:O16          O16 = <label>
            LC += 4;
            break;
         case  JMPNN: // Rn,O16    OpCode:Rn:O16          O16 = <label>
            LC += 4;
            break;
         case   JMPZ: // Rn,O16    OpCode:Rn:O16          O16 = <label> (alias JMPF)
            LC += 4;
            break;
         case  JMPNZ: // Rn,O16    OpCode:Rn:O16          O16 = <label> (alias JMPT)
            LC += 4;
            break;
         case   JMPP: // Rn,O16    OpCode:Rn:O16          O16 = <label>
            LC += 4;
            break;
         case  JMPNP: // Rn,O16    OpCode:Rn:O16          O16 = <label>
            LC += 4;
            break;
         case   CALL: // O16       OpCode:O16             O16 = <label>
            LC += 3;
            break;
         case    RET: //           OpCode
            LC += 1;
            break;
         case    SVC: // #O16      OpCode:O16             O16 = <label> or <integer>
            LC += 3;
            break;
         case  DEBUG: //           OpCode
            LC += 1;
            break;
      /* ============= */
      /* COMPUTE       */
      /* ============= */
         case   ADDR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case   SUBR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case   INCR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case   DECR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case  ZEROR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case   LSRR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case   ASRR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case    SLR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case   CMPR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case  CMPUR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case   ANDR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case    ORR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case   XORR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case   NOTR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case   NEGR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case   MULR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case   DIVR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case   MODR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
      /* ============= */
      /* COPY          */
      /* ============= */
      //=========================================================
         case    LDR:
      //=========================================================
         {
            GetNextToken(&token,lexeme);                   // Rn
            GetNextToken(&token,lexeme);                   // ,
            GetNextToken(&token,lexeme);                   // # @ FB or <label>
            if ( token == POUNDSIGN )
         // Rn,#O16         OpCode:0X10:Rn:O16       O16 = <label> or <integer>, <boolean>, <character>
               LC += 5;
            else if ( token == ATSIGN )
            {
               GetNextToken(&token,lexeme);                // FB or Rn2
               if ( token == FB )
               {
         // Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
         // Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  GetNextToken(&token,lexeme);             // :
                  GetNextToken(&token,lexeme);             // <integer>
                  GetNextToken(&token,lexeme);             // , or (empty)
                  if ( token == COMMA )
                     LC += 6;
                  else
                     LC += 5;
               }
               else
         // Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  LC += 4;
            }
            else if ( token == FB )
            {
         // Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
         // Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
               GetNextToken(&token,lexeme);                // :
               GetNextToken(&token,lexeme);                // <integer>
               GetNextToken(&token,lexeme);                // , or (empty)
               if ( token == COMMA )
                  LC += 6;
               else
                  LC += 5;
            }
            else
            {
         // Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
         // Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
               GetNextToken(&token,lexeme);                // , or (empty)
               if ( token == COMMA )
                  LC += 6;
               else
                  LC += 5;
            }
            break;
         }
      //=========================================================
         case   LDAR:
      //=========================================================
         {
            GetNextToken(&token,lexeme);                   // Rn
            GetNextToken(&token,lexeme);                   // ,
            GetNextToken(&token,lexeme);                   // @ FB or <label>
         // Rn,#O16         *** NOT MEANINGFUL ***
            if ( token == ATSIGN )
            {
               GetNextToken(&token,lexeme);                // FB or Rn2
               if ( token == FB )
               {
         // Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
         // Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  GetNextToken(&token,lexeme);             // :
                  GetNextToken(&token,lexeme);             // <integer>
                  GetNextToken(&token,lexeme);             // , or (empty)
                  if ( token == COMMA )
                     LC += 6;
                  else
                     LC += 5;
               }
               else
         // Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  LC += 4;
            }
            else if ( token == FB )
            {
         // Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
         // Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
               GetNextToken(&token,lexeme);                // :
               GetNextToken(&token,lexeme);                // <integer>
               GetNextToken(&token,lexeme);                // , or (empty)
               if ( token == COMMA )
                  LC += 6;
               else
                  LC += 5;
            }
            else
            {
         // Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
         // Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
               GetNextToken(&token,lexeme);                // , or (empty)
               if ( token == COMMA )
                  LC += 6;
               else
                  LC += 5;
            }
            break;
         }
      //=========================================================
         case    STR:
      //=========================================================
         {
            GetNextToken(&token,lexeme);                   // Rn
            GetNextToken(&token,lexeme);                   // ,
            GetNextToken(&token,lexeme);                   // @ FB or <label>
         // Rn,#O16         *** NOT MEANINGFUL ***
            if ( token == ATSIGN )
            {
               GetNextToken(&token,lexeme);                // FB or Rn2
               if ( token == FB )
               {
         // Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
         // Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  GetNextToken(&token,lexeme);             // :
                  GetNextToken(&token,lexeme);             // <integer>
                  GetNextToken(&token,lexeme);             // , or (empty)
                  if ( token == COMMA )
                     LC += 6;
                  else
                     LC += 5;
               }
               else
         // Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  LC += 4;
            }
            else if ( token == FB )
            {
         // Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
         // Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
               GetNextToken(&token,lexeme);                // :
               GetNextToken(&token,lexeme);                // <integer>
               GetNextToken(&token,lexeme);                // , or (empty)
               if ( token == COMMA )
                  LC += 6;
               else
                  LC += 5;
            }
            else
            {
         // Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
         // Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
               GetNextToken(&token,lexeme);                // , or (empty)
               if ( token == COMMA )
                  LC += 6;
               else
                  LC += 5;
            }
            break;
         }
         case  COPYR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case  PUSHR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case   POPR: // Rn            OpCode:Rn
            LC += 2;
            break;
         case  SWAPR: // Rn,Rn2        OpCode:Rn:Rn2
            LC += 3;
            break;
         case PUSHFB: //               OpCode
            LC += 1;
            break;
         case  POPFB: //               OpCode
            LC += 1;
            break;
         case  SETFB: //               OpCode:O16
            LC += 3;
            break;
         case  ADJSP: //               OpCode:O16
            LC += 3;
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
/*
         (***NOT ALLOWED***)
         INR    Rn,O16
         OUTR   Rn,O16
         LDMMU  O16
         STMMU  O16
         STOP
*/
      /* ============= */
      /* ILLEGAL       */
      /* ============= */
         default:
            LC += 1;
            break;
      }
      GetNextNonEmptySourceLine(false);
      GetNextCharacter();
      GetNextToken(&token,lexeme);
   }

// compute code-segment size and data-segment base
   *CSSize = LC;
   *DSBase = BEGINNING_ADDRESS_OF_NEXT_PAGE(*CSBase+*CSSize);

//================================================================
// data-segment assembly
//================================================================
   if ( token != DATASEGMENT )
   {
      printf("\aDATASEGMENT missing near source line #%4d. Assembly aborted\n",sourceLineNumber);
      fclose(SOURCE);
      fclose(LISTING);
      fclose(OBJECT);
      system("PAUSE");
      exit( 1 );
   }

// parse  <DataSegmentStatements> until END (correct) or EOFTOKEN (error) one line at a time
   GetNextNonEmptySourceLine(false);
   GetNextCharacter();
   GetNextToken(&token,lexeme);
   LC = *DSBase;
   while ( !((token == END) || (token == EOFTOKEN)) )
   {
      if ( token == LABEL )
      {
         FindByLexemeLabelTable(labelTable,lexeme,&index,&found);
         if ( !found )
         {
            InsertLabelTable(labelTable,lexeme,'?','A',LC,sourceLineNumber,&index,&inserted);
            // *Note*              (see below) '?' is replaced with type = 'D' for (RW,DW,DS)
            //                     (see below) '?' is replaced with type = 'E' for (EQU); value is replaced with <integer>, <boolean>, <character>, or LC
            if ( !inserted )
            {
               printf("\aLabel table capacity exceeded near source line #%4d. Assembly aborted\n",sourceLineNumber);
               fclose(SOURCE);
               fclose(LISTING);
               fclose(OBJECT);
               system("PAUSE");
               exit( 1 );
            }
         }
         GetNextToken(&token,lexeme);
         labeled = true;
      }
      else
         labeled = false;
      switch ( token )
      {
         case    EQU: // <label>   EQU (( <integer> | <boolean> | <character> | * ))
         {
            if ( labeled ) 
            {
               SetTypeLabelTable(labelTable,index,'E');
               GetNextToken(&token,lexeme);
               switch ( token )
               {
                  case INTEGER:
                     SetValueLabelTable(labelTable,index,strtol(lexeme,NULL,0));
                     break;
                  case TRUE:
                     SetValueLabelTable(labelTable,index,0XFFFF);
                     break;
                  case FALSE:
                     SetValueLabelTable(labelTable,index,0X0000);
                     break;
                  case CHARACTER:
                     SetValueLabelTable(labelTable,index,lexeme[0]);
                     break;
                  case ASTERISK:
                     SetValueLabelTable(labelTable,index,LC);
                     break;
                  default:
                     SetValueLabelTable(labelTable,index,0X0000);
                     break;
               }
            }
            break;
         }
         case     RW: // [ <label> ] RW  [ (( <integer> | <label> )) ]
         {
            WORD O16;
            
            if ( labeled ) SetTypeLabelTable(labelTable,index,'D');
            GetNextToken(&token,lexeme);
            if      ( token == LABEL )
            {
               FindByLexemeLabelTable(labelTable,lexeme,&index,&found);
               if ( found )
                  LC += 2*GetValueLabelTable(labelTable,index);
               else
                  LC += 2;
            }
            else if ( token == INTEGER )
            {
               O16 = strtol(lexeme,NULL,0);
               if ( !(0 < O16) )
                  LC += 2;
               else
                  LC += 2*O16;
            }
            else
               LC += 2;
            break;
         }
         case     DW: // [ <label> ] DW  (( <label> | <integer> | <boolean> | <character> ))
         {
            if ( labeled ) SetTypeLabelTable(labelTable,index,'D');
            LC += 2;
            break;
         }
         case     DS: // [ <label> ] DS  <string>
         {
            if ( labeled ) SetTypeLabelTable(labelTable,index,'D');
            GetNextToken(&token,lexeme);
            if ( token == STRING )
               LC += 2*((int) strlen(lexeme)+1);
            else
               LC += 2;
            break;
         }
         default:
            break;
      }
      GetNextNonEmptySourceLine(false);
      GetNextCharacter();
      GetNextToken(&token,lexeme);
   }

// compute data-segment size
   *DSSize = LC-*DSBase;
}

//----------------------------------------------------------------
void DoPass2(bool *syntaxErrorsFound,const LABELTABLE *labelTable,BYTE *memory,const int CSBase,const int DSBase)
//----------------------------------------------------------------
{
// globals accessed: pageNumber,atEOF,sourceLineNumber

   void GetNextNonEmptySourceLine(const bool listEmptySourceLines);
   void GetNextCharacter();
   void GetNextToken(TOKEN *token,char lexeme[]);
   void ListTopOfPageHeader();
   void ListObjectSourceLine(const int LC,const BYTE bytes[],const int numberBytes);
   void ListNonObjectOrEmptySourceLine();
   void ListSyntaxErrorMessage(const int errorNumber);
   void ParseHWStatementLabelOperand(const TOKEN token,const char lexeme[],
                                     int errors[],int *numberErrors,
                                     const LABELTABLE *labelTable,WORD *O16);
   void ParseHWStatementRnOperand(const TOKEN token,
                                  int errors[],int *numberErrors,
                                  BYTE *Rn);
   TOKEN token;
   char lexeme[SOURCE_LINE_LENGTH+1];

   int index;
   bool found,inserted,labeled;
   int LC,oldLC;
   int errors[SOURCE_LINE_LENGTH+1]; int numberErrors;
   BYTE bytes[SOURCE_LINE_LENGTH];    int numberBytes;
   
// initialize listing file
   pageNumber = 0;
   ListTopOfPageHeader();

// initialize scanner
   atEOF = false;
   sourceLineNumber = 0;
   GetNextNonEmptySourceLine(true);
   GetNextCharacter();
   GetNextToken(&token,lexeme);

// fill memory[] with 0X00 bytes
   for (int i = 0; i <= LOGICAL_ADDRESS_SPACE_IN_BYTES-1; i++)
      memory[i] = 0X00;

   *syntaxErrorsFound = false;
//================================================================
// code-segment assembly
//================================================================
   numberErrors = 0;
   if ( token != CODESEGMENT )
      errors[++numberErrors] = 2;                          // "CODESEGMENT missing"
   ListNonObjectOrEmptySourceLine();
   for (int i = 1; i <= numberErrors; i++)
   {
      ListSyntaxErrorMessage(errors[i]);
      *syntaxErrorsFound = true;
   }

// parse <CODESegmentStatements> until DATASEGMENT (correct) or EOFTOKEN (error) one line at a time
   LC = CSBase;
   GetNextNonEmptySourceLine(true);
   GetNextCharacter();
   GetNextToken(&token,lexeme);
   while ( !((token == DATASEGMENT) || (token == EOFTOKEN)) )
   {
      WORD word;

      oldLC = LC;
      numberErrors = 0;
      if ( token == LABEL )
      {
         FindByLexemeLabelTable(labelTable,lexeme,&index,&found);
         if ( GetDefinitionLineLabelTable(labelTable,index) != sourceLineNumber )
            errors[++numberErrors] = 3;                    // "Multiply defined label"
         GetNextToken(&token,lexeme);
         labeled = true;
      }
      else
         labeled = false;
      switch ( token )
      {
         case EQU:
            numberBytes = 0;
            if ( !labeled )
               errors[++numberErrors] = 6;                 // "EQU statement must be labeled"
            GetNextToken(&token,lexeme);
            if ( token != ASTERISK )
               errors[++numberErrors]= 17;                 // "Missing '*' in EQU HW statement operand field"
            break;
      /* ============= */
      /* CHOOSE        */
      /* ============= */
         case   NOOP: //           OpCode
         {
            numberBytes = 1;
            bytes[0] = 0X00;
            break;
         }
         case    JMP: // O16       OpCode:O16             O16 = <label>
         {
            WORD O16;
            
            numberBytes = 3;
            bytes[0] = 0X01;
            GetNextToken(&token,lexeme);
            ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
            bytes[1] = HIBYTE(O16); bytes[2] = LOBYTE(O16);
            break;
         }
         case   JMPN: // JMPN   Rn,label
         {
            BYTE Rn;
            WORD O16;
            
            numberBytes = 4;
            bytes[0] = 0X02;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
            bytes[2] = HIBYTE(O16); bytes[3] = LOBYTE(O16);
            break;
         }
         case  JMPNN: // Rn,O16    OpCode:Rn:O16          O16 = <label>
         {
            BYTE Rn;
            WORD O16;
            
            numberBytes = 4;
            bytes[0] = 0X03;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
            bytes[2] = HIBYTE(O16); bytes[3] = LOBYTE(O16);
            break;
         }
         case   JMPZ: // Rn,O16    OpCode:Rn:O16          O16 = <label> (alias JMPF)
         {
            BYTE Rn;
            WORD O16;
            
            numberBytes = 4;
            bytes[0] = 0X04;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
            bytes[2] = HIBYTE(O16); bytes[3] = LOBYTE(O16);
            break;
         }
         case  JMPNZ: // Rn,O16    OpCode:Rn:O16          O16 = <label> (alias JMPT)
         {
            BYTE Rn;
            WORD O16;
            
            numberBytes = 4;
            bytes[0] = 0X05;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
            bytes[2] = HIBYTE(O16); bytes[3] = LOBYTE(O16);
            break;
         }
         case   JMPP: // Rn,O16    OpCode:Rn:O16          O16 = <label>
         {
            BYTE Rn;
            WORD O16;
            
            numberBytes = 4;
            bytes[0] = 0X06;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
            bytes[2] = HIBYTE(O16); bytes[3] = LOBYTE(O16);
            break;
         }
         case  JMPNP: // Rn,O16    OpCode:Rn:O16          O16 = <label>
         {
            BYTE Rn;
            WORD O16;
            
            numberBytes = 4;
            bytes[0] = 0X07;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
            bytes[2] = HIBYTE(O16); bytes[3] = LOBYTE(O16);
            break;
         }
         case   CALL: // O16       OpCode:O16             O16 = <label>
         {
            WORD O16;
            
            numberBytes = 3;
            bytes[0] = 0X08;
            GetNextToken(&token,lexeme);
            ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
            bytes[1] = HIBYTE(O16); bytes[2] = LOBYTE(O16);
            break;
         }
         case    RET: //           OpCode
         {
            numberBytes = 1;
            bytes[0] = 0X09;
            break;
         }
         case    SVC: // #O16      OpCode:O16             O16 = <label> or <integer>
         {
            WORD O16;
            
            numberBytes = 3;
            bytes[0] = 0X0A;
            GetNextToken(&token,lexeme);
            if ( token != POUNDSIGN )
               errors[++numberErrors]= 19;                 // "Missing '#' in SVC statement operand field"
            else
               GetNextToken(&token,lexeme);
            if ( token == INTEGER )
               O16 = strtol(lexeme,NULL,0);
            else if ( token == LABEL )
               ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
            else
            {
               errors[++numberErrors]= 11;                 // "Expecting integer or label in SVC statement operand field, defaults to 0X0000"
               O16 = 0X0000;
            }
            bytes[1] = HIBYTE(O16); bytes[2] = LOBYTE(O16);
            break;
         }
         case  DEBUG: //           OpCode
         {
            numberBytes = 1;
            bytes[0] = 0X0B;
            break;
         }
      /* ============= */
      /* COMPUTE       */
      /* ============= */
         case   ADDR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X20;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case   SUBR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X21;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case   INCR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X22;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case   DECR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X23;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case  ZEROR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X24;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case   LSRR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X25;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case   ASRR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X26;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case    SLR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X27;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case   CMPR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X28;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case  CMPUR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X29;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case   ANDR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X2A;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case    ORR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X2B;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case   XORR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X2C;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case   NOTR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X2D;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case   NEGR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X2E;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case   MULR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X2F;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case   DIVR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X30;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case   MODR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X31;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
      /* ============= */
      /* COPY          */
      /* ============= */
      //=========================================================
         case    LDR:
      //=========================================================
         {
            BYTE Rn,Rn2,Rnx;
            WORD O16;
            
            bytes[0] = 0X40;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[2] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            if ( token == POUNDSIGN )
            {
         // Rn,#O16         OpCode:0X10:Rn:O16       O16 = <label> or <integer>, <boolean>, <character>
               bytes[1] = 0X10;
               GetNextToken(&token,lexeme);
               switch ( token  )
               {
                  case LABEL:
                     ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
                     break;
                  case INTEGER:
                     O16 = strtol(lexeme,NULL,0);
                     break;
                  case TRUE:
                     O16 = 0XFFFF;
                     break;
                  case FALSE:
                     O16 = 0X0000;
                     break;
                  case CHARACTER:
                     O16 = lexeme[0];
                     break;
                  default: 
                     errors[++numberErrors] = 23;          // "HW statement operand must be label, integer, boolean, or character, defaults to 0X0000"
                     O16 = 0X0000;
                     break;
               }
               numberBytes = 5;
               bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
            }
            else if ( token == ATSIGN )
            {
               GetNextToken(&token,lexeme);
               if ( token == FB )
               {
         // Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
         // Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  GetNextToken(&token,lexeme);
                  if ( token == COLON )
                     GetNextToken(&token,lexeme);
                  else
                     errors[++numberErrors] = 14;          // "Expecting ':' in HW statement operand field"
                  if ( token == INTEGER )
                     O16 = strtol(lexeme,NULL,0);
                  else
                  {
                     errors[++numberErrors] = 21;          // "Expecting integer in HW statement operand field, defaults to 0X0000"
                     O16 = 0X0000;
                  }
                  GetNextToken(&token,lexeme);
                  if ( token == COMMA )
                  {
                     GetNextToken(&token,lexeme);
                     ParseHWStatementRnOperand(token,errors,&numberErrors,&Rnx);
                     bytes[1] = 0X26;
                     bytes[5] = Rnx;
                     numberBytes = 6;
                  }
                  else
                  {
                     bytes[1] = 0X22;
                     numberBytes = 5;
                  }
                  bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
               }
               else
               {
         // Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
                  numberBytes = 4;
                  bytes[1] = 0X02;
                  bytes[3] = Rn2;
               }
            }
            else if ( token == FB )
            {
         // Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
         // Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
               GetNextToken(&token,lexeme);
               if ( token == COLON )
                  GetNextToken(&token,lexeme);
               else
                  errors[++numberErrors] = 14;              // "Expecting ':' in HW statement operand field"
               if ( token == INTEGER )
                  O16 = strtol(lexeme,NULL,0);
               else
               {
                  errors[++numberErrors] = 21;             // "Expecting integer in HW statement operand field, defaults to 0X0000"
                  O16 = 0X0000;
               }
               GetNextToken(&token,lexeme);
               if ( token == COMMA )
               {
                  GetNextToken(&token,lexeme);
                  ParseHWStatementRnOperand(token,errors,&numberErrors,&Rnx);
                  bytes[1] = 0X25;
                  bytes[5] = Rnx;
                  numberBytes = 6;
               }
               else
               {
                  bytes[1] = 0X21;
                  numberBytes = 5;
               }
               bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
            }
            else
            {
         // Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
         // Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
               if ( token == LABEL )
                  ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
               else
               {
                  errors[++numberErrors] = 13;             // "Expecting label in HW statement operand field, defaults to 0X0000"
                  O16 = 0X0000;
               }
               GetNextToken(&token,lexeme);
               if ( token == COMMA )
               {
                  GetNextToken(&token,lexeme);
                  ParseHWStatementRnOperand(token,errors,&numberErrors,&Rnx);
                  bytes[1] = 0X04;
                  bytes[5] = Rnx;
                  numberBytes = 6;
               }
               else
               {
                  bytes[1] = 0X01;
                  numberBytes = 5;
               }
               bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
            }
            break;
         }
      //=========================================================
         case   LDAR:
      //=========================================================
         {
            BYTE Rn,Rn2,Rnx;
            WORD O16;

      // 0X41 LDAR    Rn,operands     OpCode:mode:Rn:???

            bytes[0] = 0X41;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[2] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
         // Rn,#O16         *** NOT MEANINGFUL ***
            if ( token == ATSIGN )
            {
               GetNextToken(&token,lexeme);
               if ( token == FB )
               {
         // Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
         // Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  GetNextToken(&token,lexeme);
                  if ( token == COLON )
                     GetNextToken(&token,lexeme);
                  else
                     errors[++numberErrors] = 14;          // "Expecting ':' in HW statement operand field"
                  if ( token == INTEGER )
                     O16 = strtol(lexeme,NULL,0);
                  else
                  {
                     errors[++numberErrors] = 21;          // "Expecting integer in HW statement operand field, defaults to 0X0000"
                     O16 = 0X0000;
                  }
                  GetNextToken(&token,lexeme);
                  if ( token == COMMA )
                  {
                     GetNextToken(&token,lexeme);
                     ParseHWStatementRnOperand(token,errors,&numberErrors,&Rnx);
                     bytes[1] = 0X26;
                     bytes[5] = Rnx;
                     numberBytes = 6;
                  }
                  else
                  {
                     bytes[1] = 0X22;
                     numberBytes = 5;
                  }
                  bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
               }
               else
               {
         // Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
                  numberBytes = 4;
                  bytes[1] = 0X02;
                  bytes[3] = Rn2;
               }
            }
            else if ( token == FB )
            {
         // Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
         // Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
               GetNextToken(&token,lexeme);
               if ( token == COLON )
                  GetNextToken(&token,lexeme);
               else
                  errors[++numberErrors] = 14;              // "Expecting ':' in HW statement operand field"
               if ( token == INTEGER )
                  O16 = strtol(lexeme,NULL,0);
               else
               {
                  errors[++numberErrors] = 21;             // "Expecting integer in HW statement operand field, defaults to 0X0000"
                  O16 = 0X0000;
               }
               GetNextToken(&token,lexeme);
               if ( token == COMMA )
               {
                  GetNextToken(&token,lexeme);
                  ParseHWStatementRnOperand(token,errors,&numberErrors,&Rnx);
                  bytes[1] = 0X25;
                  bytes[5] = Rnx;
                  numberBytes = 6;
               }
               else
               {
                  bytes[1] = 0X21;
                  numberBytes = 5;
               }
               bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
            }
            else
            {
         // Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
         // Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
               if ( token == LABEL )
                  ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
               else
               {
                  errors[++numberErrors] = 13;             // "Expecting label in HW statement operand field, defaults to 0X0000"
                  O16 = 0X0000;
               }
               GetNextToken(&token,lexeme);
               if ( token == COMMA )
               {
                  GetNextToken(&token,lexeme);
                  ParseHWStatementRnOperand(token,errors,&numberErrors,&Rnx);
                  bytes[1] = 0X04;
                  bytes[5] = Rnx;
                  numberBytes = 6;
               }
               else
               {
                  bytes[1] = 0X01;
                  numberBytes = 5;
               }
               bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
            }
            break;
         }
      //=========================================================
         case    STR:
      //=========================================================
         {
            BYTE Rn,Rn2,Rnx;
            WORD O16;

      // 0X42   STR    Rn,operands     OpCode:mode:Rn:???

            bytes[0] = 0X42;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[2] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
         // Rn,#O16         *** NOT MEANINGFUL ***
            if ( token == ATSIGN )
            {
               GetNextToken(&token,lexeme);
               if ( token == FB )
               {
         // Rn,@FB:O16      OpCode:0X22:Rn:O16       O16 = <integer>
         // Rn,@FB:O16,Rnx  OpCode:0X26:Rn:O16:Rnx   O16 = <integer>
                  GetNextToken(&token,lexeme);
                  if ( token == COLON )
                     GetNextToken(&token,lexeme);
                  else
                     errors[++numberErrors] = 14;          // "Expecting ':' in HW statement operand field"
                  if ( token == INTEGER )
                     O16 = strtol(lexeme,NULL,0);
                  else
                  {
                     errors[++numberErrors] = 21;          // "Expecting integer in HW statement operand field, defaults to 0X0000"
                     O16 = 0X0000;
                  }
                  GetNextToken(&token,lexeme);
                  if ( token == COMMA )
                  {
                     GetNextToken(&token,lexeme);
                     ParseHWStatementRnOperand(token,errors,&numberErrors,&Rnx);
                     bytes[1] = 0X26;
                     bytes[5] = Rnx;
                     numberBytes = 6;
                  }
                  else
                  {
                     bytes[1] = 0X22;
                     numberBytes = 5;
                  }
                  bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
               }
               else
               {
         // Rn,@Rn2         OpCode:0X02:Rn:Rn2
                  ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
                  numberBytes = 4;
                  bytes[1] = 0X02;
                  bytes[3] = Rn2;
               }
            }
            else if ( token == FB )
            {
         // Rn,FB:O16       OpCode:0X21:Rn:O16       O16 = <integer>
         // Rn,FB:O16,Rnx   OpCode:0X25:Rn:O16:Rnx   O16 = <integer>
               GetNextToken(&token,lexeme);
               if ( token == COLON )
                  GetNextToken(&token,lexeme);
               else
                  errors[++numberErrors] = 14;              // "Expecting ':' in HW statement operand field"
               if ( token == INTEGER )
                  O16 = strtol(lexeme,NULL,0);
               else
               {
                  errors[++numberErrors] = 21;             // "Expecting integer in HW statement operand field, defaults to 0X0000"
                  O16 = 0X0000;
               }
               GetNextToken(&token,lexeme);
               if ( token == COMMA )
               {
                  GetNextToken(&token,lexeme);
                  ParseHWStatementRnOperand(token,errors,&numberErrors,&Rnx);
                  bytes[1] = 0X25;
                  bytes[5] = Rnx;
                  numberBytes = 6;
               }
               else
               {
                  bytes[1] = 0X21;
                  numberBytes = 5;
               }
               bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
            }
            else
            {
         // Rn,O16          OpCode:0X01:Rn:O16       O16 = <label>
         // Rn,O16,Rnx      OpCode:0X04:Rn:O16:Rnx   O16 = <label>
               if ( token == LABEL )
                  ParseHWStatementLabelOperand(token,lexeme,errors,&numberErrors,labelTable,&O16);
               else
               {
                  errors[++numberErrors] = 13;             // "Expecting label in HW statement operand field, defaults to 0X0000"
                  O16 = 0X0000;
               }
               GetNextToken(&token,lexeme);
               if ( token == COMMA )
               {
                  GetNextToken(&token,lexeme);
                  ParseHWStatementRnOperand(token,errors,&numberErrors,&Rnx);
                  bytes[1] = 0X04;
                  bytes[5] = Rnx;
                  numberBytes = 6;
               }
               else
               {
                  bytes[1] = 0X01;
                  numberBytes = 5;
               }
               bytes[3] = HIBYTE(O16); bytes[4] = LOBYTE(O16);
            }
            break;
         }
         case  COPYR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X43;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case  PUSHR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X44;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case   POPR: // Rn            OpCode:Rn
         {
            BYTE Rn;
            
            numberBytes = 2;
            bytes[0] = 0X45;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            break;
         }
         case  SWAPR: // Rn,Rn2        OpCode:Rn:Rn2
         {
            BYTE Rn,Rn2;
            
            numberBytes = 3;
            bytes[0] = 0X46;
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn);
            bytes[1] = Rn;
            GetNextToken(&token,lexeme);
            if ( token != COMMA )
               errors[++numberErrors]= 18;                 // "Missing ',' in HW statement operand field"
            GetNextToken(&token,lexeme);
            ParseHWStatementRnOperand(token,errors,&numberErrors,&Rn2);
            bytes[2] = Rn2;
            break;
         }
         case PUSHFB: //           OpCode
         {
            numberBytes = 1;
            bytes[0] = 0X50;
            break;
         }
         case  POPFB: //           OpCode
         {
            numberBytes = 1;
            bytes[0] = 0X51;
            break;
         }
         case  SETFB: // #O16      OpCode:O16             O16 = <integer>
         {
            WORD O16;
            
            numberBytes = 3;
            bytes[0] = 0X52;
            GetNextToken(&token,lexeme);
            if ( token != POUNDSIGN )
               errors[++numberErrors]= 24;                 // "Missing '#' in SETFB statement operand field"
            else
               GetNextToken(&token,lexeme);
            if ( token == INTEGER )
               O16 = strtol(lexeme,NULL,0);
            else
            {
               errors[++numberErrors]= 25;                 // "Expecting integer in SETFB statement operand field, defaults to 0X0000"
               O16 = 0X0000;
            }
            bytes[1] = HIBYTE(O16); bytes[2] = LOBYTE(O16);
            break;
         }
         case  ADJSP: // #O16      OpCode:O16             O16 = <integer>
         {
            WORD O16;
            
            numberBytes = 3;
            bytes[0] = 0X53;
            GetNextToken(&token,lexeme);
            if ( token != POUNDSIGN )
               errors[++numberErrors]= 26;                 // "Missing '#' in ADJSP statement operand field"
            else
               GetNextToken(&token,lexeme);
            if ( token == INTEGER )
               O16 = strtol(lexeme,NULL,0);
            else
            {
               errors[++numberErrors]= 27;                 // "Expecting integer in ADJSP statement operand field, defaults to 0X0000"
               O16 = 0X0000;
            }
            bytes[1] = HIBYTE(O16); bytes[2] = LOBYTE(O16);
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
/*
         (***NOT ALLOWED***)
         INR    Rn,O16
         OUTR   Rn,O16
         LDMMU  O16
         STMMU  O16
         STOP
*/
      /* ============= */
      /* ILLEGAL       */
      /* ============= */
         default:
         {
            errors[++numberErrors]= 20;                    // "Illegal HW statement mnemonic, defaults to NOOP"
            numberBytes = 1;
            bytes[0] = 0X00;
            break;
         }
      }
      for (int i = 0; i <= numberBytes-1; i++)
         memory[oldLC+i] = bytes[i];
      LC += numberBytes;
      ListObjectSourceLine(oldLC,bytes,numberBytes);
      for (int i = 1; i <= numberErrors; i++)
      {
         ListSyntaxErrorMessage(errors[i]);
         *syntaxErrorsFound = true;
      }
      GetNextNonEmptySourceLine(true);
      GetNextCharacter();
      GetNextToken(&token,lexeme);
   }

//================================================================
// data-segment assembly
//================================================================
   numberErrors = 0;
   if ( token != DATASEGMENT )
      errors[++numberErrors] = 1;                          // "DATASEGMENT missing"
   ListNonObjectOrEmptySourceLine();
   for (int i = 1; i <= numberErrors; i++)
   {
      ListSyntaxErrorMessage(errors[i]);
      *syntaxErrorsFound = true;
   }

// parse  <DataSegmentStatements> until CODESEGMENT or EOFTOKEN one line at a time
   LC = DSBase;
   GetNextNonEmptySourceLine(true);
   GetNextCharacter();
   GetNextToken(&token,lexeme);
   while ( !((token == END) || (token == EOFTOKEN)) )
   {
      oldLC = LC;
      numberErrors = 0;

      if ( token == LABEL )
      {
         FindByLexemeLabelTable(labelTable,lexeme,&index,&found);
         if ( GetDefinitionLineLabelTable(labelTable,index) != sourceLineNumber )
            errors[++numberErrors] = 3;                    // "Multiply defined label"
         GetNextToken(&token,lexeme);
         labeled = true;
      }
      else
         labeled = false;
      switch ( token )
      {
         case EQU: // <label>   EQU (( <integer> | <boolean> | <character> | * ))
         {
            GetNextToken(&token,lexeme);
            if ( !labeled )
               errors[++numberErrors] = 6;                 // "EQU statement must be labeled"
            if      ( !((token == INTEGER )   ||
                        (token == TRUE )      ||
                        (token == FALSE )     ||
                        (token == CHARACTER ) ||
                        (token == ASTERISK) ) )
               errors[++numberErrors] = 15;                // "Missing integer, boolean, character, or '*' in EQU data definition statement operand field"
            numberBytes = 0;
            break;
         }
         case RW: // [ <label> ] RW  [ (( <integer> | <label> )) ]
         {
            WORD O16;
            
            GetNextToken(&token,lexeme);
            if      ( token == LABEL )
            {
               FindByLexemeLabelTable(labelTable,lexeme,&index,&found);
               if ( found )
               {
                  if ( GetDefinitionLineLabelTable(labelTable,index) < sourceLineNumber )
                  {
                     LC += 2*GetValueLabelTable(labelTable,index);
                     IncrementNumberReferencesLabelTable((LABELTABLE *) labelTable,index);
                  }
                  else
                  {
                     errors[++numberErrors] = 27;          // "RW label operand must be defined on a source line before the RW source line, defaults to 1"
                     LC += 2;
                  }
               }
               else
               {
                  LC += 2;
               }
            }
            else if ( token == INTEGER )
            {
               O16 = strtol(lexeme,NULL,0);
               if ( !(0 < O16) )
               {
                  errors[++numberErrors] = 8;              // "RW operand must be positive, defaults to 1"
                  LC += 2;
               }
               else
                  LC += 2*O16;
            }
            else
               LC += 2;
            numberBytes = 0;
            break;
         }
         case DW: // [ <label> ] DW  (( <label> | <integer> | <boolean> | <character> ))
         {
            WORD O16;
            
            GetNextToken(&token,lexeme);
            switch ( token )
            {
               case LABEL:
               {
                  FindByLexemeLabelTable(labelTable,lexeme,&index,&found);
                  if ( found )
                  {
                     O16 = GetValueLabelTable(labelTable,index);
                     IncrementNumberReferencesLabelTable((LABELTABLE *) labelTable,index);
                  }
                  else
                  {
                     errors[++numberErrors]= 26;           // "Undefined label in DW statement operand field, defaults to 0X0000"
                     O16 = 0X0000;
                  }
                  break;
               }
               case INTEGER:
                  O16 = strtol(lexeme,NULL,0);
                  break;
               case TRUE:
                  O16 = 0XFFFF;
                  break;
               case FALSE:
                  O16 = 0X0000;
                  break;
               case CHARACTER:
                  O16 = lexeme[0];
                  break;
               default: 
                  errors[++numberErrors] = 12;             // "DW operand must be integer, boolean, or character, defaults to 0X0000"
                  O16 = 0X0000;
                  break;
            }
            LC += 2;
            numberBytes = 2;
            bytes[0] = HIBYTE(O16); bytes[1] = LOBYTE(O16);
            break;
         }
         case DS: // [ <label> ] DS  <string>
         {
            GetNextToken(&token,lexeme);
            numberBytes = 0;
            if ( token == STRING )
            {
               for (int i = 0; i <= (int) strlen(lexeme); i++)
               {
                  bytes[numberBytes++] = 0X00;
                  bytes[numberBytes++] = lexeme[i];
                  LC += 2;
               }
            }
            else
            {
               errors[++numberErrors] = 5;                 // "DS operand must be string, defaults to null string"
               numberBytes = 2;
               bytes[0] = 0X00; bytes[1] = 0X00;
               LC += 2;
            }
            break;
         }
         default:
         {
            errors[++numberErrors]= 16;                    // "Illegal data definition statement mnemonic, ignored"
            numberBytes = 0;
            break;
         }
      }
      for (int i = 0; i <= numberBytes-1; i++)
         memory[oldLC+i] = bytes[i];
      ListObjectSourceLine(oldLC,bytes,numberBytes);
      for (int i = 1; i <= numberErrors; i++)
      {
         ListSyntaxErrorMessage(errors[i]);
         *syntaxErrorsFound = true;
      }
      GetNextNonEmptySourceLine(true);
      GetNextCharacter();
      GetNextToken(&token,lexeme);
   }
   if ( token == END )
      ListNonObjectOrEmptySourceLine();
   else
   {
      ListSyntaxErrorMessage(4);                           // "END missing"
      *syntaxErrorsFound = true;
   }
}

//----------------------------------------------------------------
void ParseHWStatementLabelOperand(const TOKEN token,const char lexeme[],
                                  int errors[],int *numberErrors,
                                  const LABELTABLE *labelTable,WORD *O16)
//----------------------------------------------------------------
{
// globals accessed: (NONE)

   bool found,inserted;
   int index;
   
   if ( token == LABEL )
   {
      FindByLexemeLabelTable(labelTable,lexeme,&index,&found);
      if ( found )
      {
         *O16 = GetValueLabelTable(labelTable,index);
         IncrementNumberReferencesLabelTable((LABELTABLE*) labelTable,index);
      }
      else
      {
         errors[++*numberErrors] = 22;                     // "Undefined label in HW statement operand field, defaults to 0X0000"
         *O16 = 0X0000;
      }
   }
   else
   {
      errors[++*numberErrors] = 9;                         // "Missing label in operand field of HW statement, defaults to 0X0000"
      *O16 = 0X0000;
   }
}

//----------------------------------------------------------------
void ParseHWStatementRnOperand(const TOKEN token,
                               int errors[],int *numberErrors,
                               BYTE *Rn)
//----------------------------------------------------------------
{
// globals accessed: (NONE)

   switch( token )
   {
      case  R0: *Rn = 0X00; break;
      case  R1: *Rn = 0X01; break;
      case  R2: *Rn = 0X02; break;
      case  R3: *Rn = 0X03; break;
      case  R4: *Rn = 0X04; break;
      case  R5: *Rn = 0X05; break;
      case  R6: *Rn = 0X06; break;
      case  R7: *Rn = 0X07; break;
      case  R8: *Rn = 0X08; break;
      case  R9: *Rn = 0X09; break;
      case R10: *Rn = 0X0A; break;
      case R11: *Rn = 0X0B; break;
      case R12: *Rn = 0X0C; break;
      case R13: *Rn = 0X0D; break;
      case R14: *Rn = 0X0E; break;
      case R15: *Rn = 0X0F; break;
       default: *Rn = 0X00;
                 errors[++*numberErrors] = 10;             // "Missing Rn in HW statement operand field, defaults to R0"
   }
}

//----------------------------------------------------------------
void BuildObjectFile(const LABELTABLE *labelTable,const BYTE *memory,
                     int CSBase,int CSSize,int DSBase,int DSSize)
//----------------------------------------------------------------
{
// globals accessed: (none)

/*
   =====================
   OBJECT file format
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
   int size = 0;
   
// output only those labels that have (numberReferences > 0)
   for (int index = 1; index <= GetSizeLabelTable(labelTable); index++)
   {
      if ( (GetNumberReferencesLabelTable(labelTable,index) > 0) 
        || (GetSourceLabelTable(labelTable,index) == 'A') ) 
         size++;
   }
   fprintf(OBJECT,"%3d\n",size);

   for (int index = 1; index <= GetSizeLabelTable(labelTable); index++)
   {
      if ( (GetNumberReferencesLabelTable(labelTable,index) > 0) 
        || (GetSourceLabelTable(labelTable,index) == 'A') ) 
      {
         fprintf(OBJECT,"%c%c 0X%04X %4d %s\n",GetTypeLabelTable(labelTable,index),
            GetSourceLabelTable(labelTable,index),
            GetValueLabelTable(labelTable,index),
            GetDefinitionLineLabelTable(labelTable,index),
            GetLexemeLabelTable(labelTable,index));
      }
   }

   fprintf(OBJECT,"0X%04X %5d\n",CSBase,CSSize);
   for (int i = 0; i <= CSSize-1; i++)
   {
      fprintf(OBJECT,"0X%02X ",memory[CSBase+i]);
      if ( ((i+1)%16 == 0) || (i == CSSize-1) ) fprintf(OBJECT,"\n");
   }

   fprintf(OBJECT,"0X%04X %5d\n",DSBase,DSSize);
   for (int i = 0; i <= DSSize-1; i++)
   {
      fprintf(OBJECT,"0X%02X ",memory[DSBase+i]);
      if ( ((i+1)%16 == 0) || (i == DSSize-1) ) fprintf(OBJECT,"\n");
   }
}

/*----------------------------------------------------------------
Page XXX, Full source file name "XXX...XXX"
    LC  Object code Line Source code
------ ------------ ---- -------------------------------------------------------------------------------------------------
0XXXXX XXXXXXXXXXXX DDDD XXX...XXX                 object-containing source line

                    DDDD XXX...XXX                 non-object or empty source line

                    ^^^^ XXX...XXX                 error message line
----------------------------------------------------------------*/

//----------------------------------------------------------------
void ListObjectSourceLine(const int LC,const BYTE bytes[],const int numberBytes)
//----------------------------------------------------------------
{
// globals accessed: LISTING,sourceLineNumber,linesOnPage,sourceLine[]

   void ListTopOfPageHeader();
   
   if ( numberBytes == 0 )
   {
      fprintf(LISTING,"0X%04X              %4d %s\n",LC,sourceLineNumber,sourceLine);
      linesOnPage++;
      if ( linesOnPage >= LINES_PER_PAGE ) ListTopOfPageHeader();
   }
   else
   {
      int numberLines = (numberBytes/6) + ((numberBytes%6 != 0) ? 1 : 0);

      for (int line = 1; line <= numberLines; line++)
      {
         int LB = (line-1)*6;
         int UB = (line < numberLines) ? line*6-1 : numberBytes-1;

         fprintf(LISTING,"0X%04X ",LC+LB);
         for (int i = LB; i <= LB+5; i++)
            if ( i <= UB )
               fprintf(LISTING,"%02X",bytes[i]);
            else
               fprintf(LISTING,"  ");
         if ( line == 1 )
            fprintf(LISTING," %4d %s\n",sourceLineNumber,sourceLine);
         else
            fprintf(LISTING,"\n");
      }
   }
}

//----------------------------------------------------------------
void ListNonObjectOrEmptySourceLine()
//----------------------------------------------------------------
{
// globals accessed: LISTING,sourceLineNumber,linesOnPage,sourceLine[]

   void ListTopOfPageHeader();
   
   fprintf(LISTING,"                    %4d %s\n",sourceLineNumber,sourceLine);
   linesOnPage++;
   if ( linesOnPage >= LINES_PER_PAGE ) ListTopOfPageHeader();
}

//----------------------------------------------------------------
void ListTopOfPageHeader()
//----------------------------------------------------------------
{
// globals accessed: fullSOURCEFilename,LISTING,pageNumber,linesOnPage

   pageNumber++;
   fprintf(LISTING,"Page %3d, Full source file name \"%s\"\n",pageNumber,fullSOURCEFilename);
   fprintf(LISTING,"    LC  Object code Line Source code\n");
   fprintf(LISTING,"------ ------------ ---- -------------------------------------------------------------------------------------------------\n");
   linesOnPage = 0;
}

//----------------------------------------------------------------
void GetNextToken(TOKEN *token,char lexeme[])
//----------------------------------------------------------------
{
// globals accessed:  nextCharacter

   void GetNextCharacter();

   typedef struct RESERVEDWORD
   {
      char lexeme[30+1];
      TOKEN token;
   } RESERVEDWORD;

   const RESERVEDWORD reservedWords[] =
   {
      { "DATASEGMENT"  ,DATASEGMENT               },
      { "CODESEGMENT"  ,CODESEGMENT               },
      { "END"          ,END                       },
      { "TRUE"         ,TRUE                      },
      { "FALSE"        ,FALSE                     },
      { "FB"           ,FB                        },
      { "R0"           ,R0                        },
      { "R1"           ,R1                        },
      { "R2"           ,R2                        },
      { "R3"           ,R3                        },
      { "R4"           ,R4                        },
      { "R5"           ,R5                        },
      { "R6"           ,R6                        },
      { "R7"           ,R7                        },
      { "R8"           ,R8                        },
      { "R9"           ,R9                        },
      { "R10"          ,R10                       },
      { "R11"          ,R11                       },
      { "R12"          ,R12                       },
      { "R13"          ,R13                       },
      { "R14"          ,R14                       },
      { "R15"          ,R15                       },
      { "RW"           ,RW                        },
      { "DW"           ,DW                        },
      { "DS"           ,DS                        },
      { "EQU"          ,EQU                       },
      { "NOOP"         ,NOOP                      },
      { "JMP"          ,JMP                       },
      { "JMPN"         ,JMPN                      },
      { "JMPNN"        ,JMPNN                     },
      { "JMPZ"         ,JMPZ                      },
      { "JMPNZ"        ,JMPNZ                     },
      { "JMPP"         ,JMPP                      },
      { "JMPNP"        ,JMPNP                     },
      { "JMPT"         ,JMPNZ                     },
      { "JMPF"         ,JMPZ                      },
      { "CALL"         ,CALL                      },
      { "RET"          ,RET                       },
      { "SVC"          ,SVC                       },
      { "DEBUG"        ,DEBUG                     },
      { "ADDR"         ,ADDR                      },
      { "SUBR"         ,SUBR                      },
      { "INCR"         ,INCR                      },
      { "DECR"         ,DECR                      },
      { "ZEROR"        ,ZEROR                     },
      { "LSRR"         ,LSRR                      },
      { "ASRR"         ,ASRR                      },
      { "SLR"          ,SLR                       },
      { "CMPR"         ,CMPR                      },
      { "CMPUR"        ,CMPUR                     },
      { "ANDR"         ,ANDR                      },
      { "ORR"          ,ORR                       },
      { "XORR"         ,XORR                      },
      { "NOTR"         ,NOTR                      },
      { "NEGR"         ,NEGR                      },
      { "MULR"         ,MULR                      },
      { "DIVR"         ,DIVR                      },
      { "MODR"         ,MODR                      },
      { "LDR"          ,LDR                       },
      { "LDAR"         ,LDAR                      },
      { "STR"          ,STR                       },
      { "COPYR"        ,COPYR                     },
      { "PUSHR"        ,PUSHR                     },
      { "POPR"         ,POPR                      },
      { "SWAPR"        ,SWAPR                     },
      { "PUSHFB"       ,PUSHFB                    },
      { "POPFB"        ,POPFB                     },
      { "SETFB"        ,SETFB                     },
      { "ADJSP"        ,ADJSP                     }
   };

   do
   {
   // "eat" any whitespace (blanks and TABs)
      while ( (nextCharacter == ' ')
           || (nextCharacter == '\t') )
         GetNextCharacter();

   /*
      "eat" any comments. Comments are always assumed to extend to (and include) EOLC
<comment>                 ::= ; { <ASCIIcharacter> }* EOLTOKEN
   */
      if ( nextCharacter == ';' )
         do
            GetNextCharacter();
         while ( nextCharacter != EOLC );
   } while ( (nextCharacter == ' ')
          || (nextCharacter == '\t') );
/*
   reserved words and <label>s

<label>                   ::= <letter> { (( <letter> | <digit> | _ )) }*

<letter>                  ::= A | B | ... | Z | a | b | ... | z

<digit>                   ::= 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9

<hexdigit>                ::= <digit>
                            | (( a | A )) | (( b | B )) | (( c | C )) 
                            | (( d | D )) | (( e | E )) | (( f | F ))
*/
   if ( isalpha(nextCharacter) )
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
         *token = LABEL;
   }

/*
   <integer>                 ::= [ (( + | - )) ]    <digit>    {    <digit> }* 
                               | [ (( + | - )) ] 0X <hexdigit> { <hexdigit> }*

   <digit>                   ::= 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9

   <hexdigit>                ::= <digit>
                               | (( a | A )) | (( b | B )) | (( c | C )) 
                               | (( d | D )) | (( e | E )) | (( f | F ))
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
            *token = INTEGER;
            lexeme[i] = '\0';
         }
      }
   }
   else
   {
      switch ( nextCharacter )
      {
// <string>                  ::= "{ <ASCIIcharacter> }*"                     || code an embedded " as ""
         case  '"': 
         {
            int i = 0;
            bool complete;
            
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
            break;
         }
// <character>               ::= '<ASCIIcharacter>'                          || code an embedded ' as \'
         case '\'': 
            GetNextCharacter();
            lexeme[0] = nextCharacter; lexeme[1] = '\0';
            GetNextCharacter();
            if ( nextCharacter == '\'' )
            {
              *token = CHARACTER;
              GetNextCharacter();
            }
            else
              *token = UNKNOWN;
            break;
         case  '*':
            *token = ASTERISK;
            lexeme[0] = nextCharacter; lexeme[1] = '\0';
            GetNextCharacter();
            break;
         case  ',':
            *token = COMMA;
            lexeme[0] = nextCharacter; lexeme[1] = '\0';
            GetNextCharacter();
            break;
         case  '@':
            *token = ATSIGN;
            lexeme[0] = nextCharacter; lexeme[1] = '\0';
            GetNextCharacter();
            break;
         case  ':':
            *token = COLON;
            lexeme[0] = nextCharacter; lexeme[1] = '\0';
            GetNextCharacter();
            break;
         case  '#':
            *token = POUNDSIGN;
            lexeme[0] = nextCharacter; lexeme[1] = '\0';
            GetNextCharacter();
            break;
         case EOFC:
            *token = EOFTOKEN;
            lexeme[0] = '\0';
            break;
         case EOLC:
            *token = EOLTOKEN;
            lexeme[0] = '\0';
            GetNextCharacter();
            break;
         default:
            *token = UNKNOWN;
            lexeme[0] = nextCharacter; lexeme[1] = '\0';
            GetNextCharacter();
            break;
      }
   }
}

//----------------------------------------------------------------
void GetNextCharacter()
//----------------------------------------------------------------
{
// globals accessed: atEOF,atEOL,nextCharacter,sourceLine[],sourceLineIndex

   if ( atEOF )
      nextCharacter = EOFC;
   else
   {
      if ( atEOL )
         nextCharacter = EOLC;
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

//----------------------------------------------------------------
void GetNextNonEmptySourceLine(const bool listEmptySourceLines)
//----------------------------------------------------------------
{
// globals accessed: SOURCE,atEOF,atEOL,sourceLine[],sourceLineNumber,sourceLineIndex

   bool IsEmptySourceLine();
   void ListNonObjectOrEmptySourceLine();
   
   if ( feof(SOURCE) )
      atEOF = true;
   else
   {
      do
      {
         sourceLineNumber++;
         if ( fgets(sourceLine,SOURCE_LINE_LENGTH,SOURCE) == NULL )
            atEOF = true;
         else
         {
            if ( (strchr(sourceLine,'\n') == NULL) && !feof(SOURCE) )
            {
               printf("******* Source line is too long!\n");
              fprintf(LISTING,"******* Source line is too long!\n");
            }
         // Erase *ALL* control characters at end of source line (when any)
            while ( (0 <= (int) strlen(sourceLine)-1) &&
                    iscntrl(sourceLine[(int) strlen(sourceLine)-1]) )
               sourceLine[(int) strlen(sourceLine)-1] = '\0';
            if ( IsEmptySourceLine() && listEmptySourceLines )
               ListNonObjectOrEmptySourceLine();
            else
            {
               atEOL = false;
               sourceLineIndex = 0;
            }
         }
      } while ( !atEOF && IsEmptySourceLine() );
   }
}

//----------------------------------------------------------------
bool IsEmptySourceLine()
//----------------------------------------------------------------
{
// globals accessed: sourceLine[]

// a source line is empty when its first non-whitespace character (if there is one) is *NOT* ';'
   bool isAllWhiteSpace = true;
   int i = 0;

   while ( isAllWhiteSpace && (i <= (int) strlen(sourceLine)-1) ) 
   {
      if ( isspace(sourceLine[i]) )
         i++;
      else
         isAllWhiteSpace = false;
   }
   return ( isAllWhiteSpace || (!isAllWhiteSpace && (sourceLine[i] == ';')) );
}

//----------------------------------------------------------------
void ListSyntaxErrorMessage(const int error)
//----------------------------------------------------------------
{
   void ListTopOfPageHeader();

// globals accessed: LISTING,linesOnPage

   const char ERRORMESSAGES[][100+1] =
   {
    //           1         2         3         4         5         6         7         8         9        10
    //  1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890
/* 0*/ "(***NOT USED***)",
/* 1*/ "DATASEGMENT missing",
/* 2*/ "CODESEGMENT missing",
/* 3*/ "Multiply defined label",
/* 4*/ "END missing",
/* 5*/ "DS operand must be string, defaults to null string",
/* 6*/ "EQU statement must be labeled",
/* 7*/ "(***NOT USED***)",
/* 8*/ "RW operand must be positive, defaults to 1",
/* 9*/ "Missing label in HW statement operand field, defaults to 0X0000",
/*10*/ "Missing Rn in HW statement operand field, defaults to R0",
/*11*/ "Expecting integer or label in SVC statement operand field, defaults to 0X0000",
/*12*/ "DW operand must be integer, boolean, or character, defaults to 0X0000",
/*13*/ "Expecting label in HW statement operand field, defaults to 0X0000",
/*14*/ "Expecting ':' in HW statement operand field",
/*15*/ "Missing integer, boolean, character, or '*' in EQU data definition statement operand field",
/*16*/ "Illegal data definition statement mnemonic, ignored",
/*17*/ "Missing '*' in EQU HW statement operand field",
/*18*/ "Missing ',' in HW statement operand field",
/*19*/ "Missing '#' in SVC statement operand field",
/*20*/ "Illegal HW statement mnemonic, defaults to NOOP",
/*21*/ "Expecting integer in HW statement operand field, defaults to 0X0000",
/*22*/ "Undefined label in HW statement operand field, defaults to 0X0000",
/*23*/ "HW statement operand must be label, integer, boolean, or character, defaults to 0X0000",
/*24*/ "Missing '#' in SETFB statement operand field",
/*25*/ "Expecting integer in SETFB statement operand field, defaults to 0X0000",
/*26*/ "Undefined label in DW statement operand field, defaults to 0X0000",
/*27*/ "RW label operand must be defined on a source line before the RW source line, defaults to 1",
/*28*/ "(***NOT USED***)",
/*29*/ "(***NOT USED***)",
/*30*/ "(***NOT USED***)"
   };

   fprintf(LISTING,"                    ^^^^ %s\n",ERRORMESSAGES[error]);
   linesOnPage++;
   if ( linesOnPage >= LINES_PER_PAGE ) ListTopOfPageHeader();
}
