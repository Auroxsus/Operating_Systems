//--------------------------------------------------
// Dr. Art Hanna
// CS3350 S16 Simulation (LABELTABLE abstract data type)
// LabelTable.c
//--------------------------------------------------
#include <stdlib.h>
#include <stdlib.h>
#include <stdbool.h>

#include <string.h>
#include <ctype.h>

#include "LabelTable.h"

//--------------------------------------------------
void ConstructLabelTable(LABELTABLE *table,const int capacity)
//--------------------------------------------------
{
   table->size = 0;
   table->capacity = capacity;
   table->labels = (LABELRECORD *) malloc(sizeof(LABELRECORD)*(capacity+1));
}

//--------------------------------------------------
void DestructLabelTable(LABELTABLE *table)
//--------------------------------------------------
{
   free(table->labels);
}

//--------------------------------------------------
void InsertLabelTable(LABELTABLE *table,
                      const char lexeme[],const char type,const char source,const int value,const int definitionLine,
                      int *index,bool *inserted)
//--------------------------------------------------
{
	if ( table->size >= table->capacity )
	   *inserted = false;
	else
   {
	   table->size++;
      table->labels[table->size].type = type;
      table->labels[table->size].source = source;
	   table->labels[table->size].value = value;
      table->labels[table->size].definitionLine = definitionLine;
      table->labels[table->size].numberReferences = 0;
      strcpy(table->labels[table->size].lexeme,lexeme);
	   *index = table->size;
	   *inserted = true;
	}
}

//--------------------------------------------------
void SetValueLabelTable(LABELTABLE *table,const int index,const int value)
//--------------------------------------------------
{
   table->labels[index].value = value;
}

//--------------------------------------------------
void SetTypeLabelTable(LABELTABLE *table,const int index,const char type)
//--------------------------------------------------
{
   table->labels[index].type = type;
}

//--------------------------------------------------
void IncrementNumberReferencesLabelTable(LABELTABLE *table,const int index)
//--------------------------------------------------
{
      table->labels[index].numberReferences++;
}

//--------------------------------------------------
char GetTypeLabelTable(const LABELTABLE *table,const int index)
//--------------------------------------------------
{
   return( table->labels[index].type );
}

//--------------------------------------------------
char GetSourceLabelTable(const LABELTABLE *table,const int index)
//--------------------------------------------------
{
   return( table->labels[index].source );
}

//--------------------------------------------------
WORD GetValueLabelTable(const LABELTABLE *table,const int index)
//--------------------------------------------------
{
   return( table->labels[index].value );
}

//--------------------------------------------------
int GetDefinitionLineLabelTable(const LABELTABLE *table,const int index)
//--------------------------------------------------
{
   return( table->labels[index].definitionLine );
}

//--------------------------------------------------
int GetNumberReferencesLabelTable(const LABELTABLE *table,const int index)
//--------------------------------------------------
{
   return( table->labels[index].numberReferences );
}

//--------------------------------------------------
char *GetLexemeLabelTable(const LABELTABLE *table,const int index)
//--------------------------------------------------
{ 
   return( table->labels[index].lexeme );
}

//--------------------------------------------------
int GetSizeLabelTable(const LABELTABLE *table)
//--------------------------------------------------
{ 
   return( table->size );
}

//--------------------------------------------------
int GetCapacityLabelTable(const LABELTABLE *table) 
//--------------------------------------------------
{ 
   return( table->capacity );
}

//--------------------------------------------------
void FindByLexemeLabelTable(const LABELTABLE *table,const char lexeme[],
                            int *index,bool *found)
//--------------------------------------------------
{
	*found = false;
	*index = 1;
	while ( (*index <= table->size) && !*found )
   {
      char uLexeme1[MAX_LABEL_LENGTH+1];
      char uLexeme2[MAX_LABEL_LENGTH+1];
      int i;

      for (i = 0; i <= (int) strlen(lexeme)-1; i++)
         uLexeme1[i] = toupper(lexeme[i]);
      uLexeme1[i] = '\0';
      for (i = 0; i <= (int) strlen(table->labels[*index].lexeme)-1; i++)
         uLexeme2[i] = toupper(table->labels[*index].lexeme[i]);
      uLexeme2[i] = '\0';
      if ( strcmp(uLexeme1,uLexeme2) == 0 )
	      *found = true;
	   else 
	      (*index)++;
   }
}

//--------------------------------------------------
void FindByValueLabelTable(const LABELTABLE *table,const int value,const char type,
                           int *index,bool *found)
//--------------------------------------------------
{
	*found = false;
	*index = 1;
	while ( (*index <= table->size) && !*found )
   {
      if ( (value == table->labels[*index].value) && (table->labels[*index].type == type) )
      {
	      *found = true;
      }
	   else 
	      (*index)++;
   }
}
