//--------------------------------------------------
// Dr. Art Hanna
// CS3350 S16 Simulation (LABELTABLE abstract data type)
// LabelTable.h
//--------------------------------------------------
#ifndef LABELTABLE_H
#define LABELTABLE_H

#include <stdbool.h>

#include "Computer.h"

#define MAX_LABEL_LENGTH  64

typedef struct LABELRECORD
{
   char type;                          // 'E'QU, 'C'ode-segment, 'D'ata-segment
   char source;                        // 'S'tandard or 'A'pplication
   WORD value;
   int definitionLine;
   int numberReferences;
   char lexeme[MAX_LABEL_LENGTH+1];
} LABELRECORD;

typedef struct LABELTABLE
{
   int size;
   int capacity;
   LABELRECORD *labels;
} LABELTABLE;

void ConstructLabelTable(LABELTABLE *table,const int capacity);
void DestructLabelTable(LABELTABLE *table);

void InsertLabelTable(LABELTABLE *table,
                      const char lexeme[],const char type,const char source,const int value,const int definitionLine,
                      int *index,bool *inserted);

void SetTypeLabelTable(LABELTABLE *table,const int index,const char type);
void SetValueLabelTable(LABELTABLE *table,const int index,const int value);
void IncrementNumberReferencesLabelTable(LABELTABLE *table,const int index);

char GetTypeLabelTable(const LABELTABLE *table,const int index);
char GetSourceLabelTable(const LABELTABLE *table,const int index);
WORD GetValueLabelTable(const LABELTABLE *table,const int index);
int GetDefinitionLineLabelTable(const LABELTABLE *table,const int index);
int GetNumberReferencesLabelTable(const LABELTABLE *table,const int index);
char *GetLexemeLabelTable(const LABELTABLE *table,const int index);

int GetSizeLabelTable(const LABELTABLE *table);
int GetCapacityLabelTable(const LABELTABLE *table);

void FindByLexemeLabelTable(const LABELTABLE *table,const char lexeme[],
                            int *index,bool *found);
void FindByValueLabelTable(const LABELTABLE *table,const int value,const char type,
                           int *index,bool *found);

#endif
