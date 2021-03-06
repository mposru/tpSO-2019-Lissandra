#include "Helper.h"
#include "Compactador.h"

errorNo procesarCreate(char*, char*, char*, char*);
errorNo crearParticiones(char*, int);
errorNo procesarInsert(char*, uint16_t, char*, unsigned long long);
errorNo procesarSelect(char*, char*, char**, int);
errorNo procesarDescribe(char*, char**);
errorNo procesarDrop(char*);
void borrarParticiones(DIR*, char*);
t_list* buscarEnBloques(char**, int, int);
t_list* obtenerRegistrosDeMemtable(char*, int);
t_list* obtenerRegistrosDeTmp(char*, int);
t_list* obtenerRegistrosDeParticiones(char*, int, int);
void borrarArchivosYLiberarBloques(DIR*, char*);
char* obtenerMetadata(char*);
