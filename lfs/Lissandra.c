#include "Lissandra.h"

/*
 * Parametros del main:
 * 1. Char*: Path del config
 * */
int main(int argc, char* argv[]) {
	iniciarLFS(argv);

	if(!pthread_create(&hiloLeerDeConsola, NULL, leerDeConsola, NULL)){
		log_info(logger_LFS, "Hilo de consola creado");
	}else{
		log_error(logger_LFS, "Error al crear hilo de consola");
	}

	if(!pthread_create(&hiloRecibirMemorias, NULL, recibirMemorias, NULL)){
		log_info(logger_LFS, "Hilo de recibir memorias creado");
	}else{
		log_error(logger_LFS, "Error al crear hilo recibir memorias");
	}

	if(!pthread_create(&hiloDumpeo, NULL, hiloDump, NULL)){
		log_info(logger_LFS, "Hilo de Dump iniciado");
	}else{
		log_error(logger_LFS, "Error al crear hilo Dumpeo");
	}

	pthread_join(hiloLeerDeConsola, NULL);
	log_info(logger_LFS, "Hilo de consola finalizado");
	pthread_join(hiloRecibirMemorias, NULL);
	log_info(logger_LFS, "Hilo recibir memorias finalizado");
	pthread_join(hiloDumpeo, NULL);
	log_info(logger_LFS, "Hilo dumpeo finalizado");


	liberarMemoriaLFS();
	return EXIT_SUCCESS;
}


void iniciarLFS(char* argv[]){
	logger_LFS = log_create("LissandraFileSystem.log", "Lfs", 1, LOG_LEVEL_DEBUG);
	log_info(logger_LFS, "----------------INICIO DE LISSANDRA FS--------------");

	if(argv[1] != NULL){
		char* configPath = argv[1];
		config = config_create(configPath);
	}else{

		config = config_create("/home/utnso/tp-2019-1c-bugbusters/LissandraFileSystem/LissandraFileSystem.config");
	}

	if(config == NULL){
		log_error(logger_LFS, "Error al leer archivo de configuracion");
		log_info(logger_LFS, "Finalizando Lissandra File System");
		exit(EXIT_FAILURE);
	}
	if(!config_has_property(config, "PUNTO_MONTAJE")){
		log_error(logger_LFS, "Key \"PUNTO_MONTAJE\" no encontrada en config");
		log_info(logger_LFS, "Finalizando Lissandra File System");
		exit(EXIT_FAILURE);
	}
	char* puntoDeMontaje = config_get_string_value(config, "PUNTO_MONTAJE");

	pathRaiz = strdup(puntoDeMontaje);
	pathTablas = string_from_format("%sTablas", pathRaiz);
	pathMetadata = string_from_format("%sMetadata", pathRaiz);
	pathBloques = string_from_format("%sBloques", pathRaiz);
	char* pathBitmap = string_from_format("%s/Bitmap.bin", pathMetadata);
	char* pathFileMetadata = string_from_format("%s/Metadata.bin", pathMetadata);

	DIR* dirPuntoDeMontaje = opendir(pathRaiz);
	if(dirPuntoDeMontaje){
		free(dirPuntoDeMontaje);
		configMetadata = config_create(pathFileMetadata);
		blocks = config_get_int_value(configMetadata, "BLOCKS");
	}else{
		crearFS(pathBitmap, pathFileMetadata);
	}

	free(pathFileMetadata);

	levantarFS(pathBitmap);
	free(pathBitmap);
}

void crearFS(char* pathBitmap, char* pathFileMetadata) {
	//TODO catchear todos los errores
	mkdir(pathRaiz, S_IRWXU);
	mkdir(pathTablas, S_IRWXU);
	mkdir(pathMetadata, S_IRWXU);
	mkdir(pathBloques, S_IRWXU);

	crearFSMetadata(pathBitmap, pathFileMetadata);
	crearBloques();
}

void crearFSMetadata(char* pathBitmap, char* pathFileMetadata){
	FILE* fileMetadata = fopen(pathFileMetadata, "w");
	if(fileMetadata == NULL){
		log_error(logger_LFS, "Error al crear metadata");
		log_info(logger_LFS, "Finalizando Lissandra File System");
		exit(EXIT_FAILURE);
	}
	fclose(fileMetadata);

	configMetadata = config_create(pathFileMetadata);

	char* tamanioDeBloque = readline("Ingresar tamanio de bloque: ");
	char* numeroDeBloques = readline("Ingrese numero de bloques: ");
	char* magicNumber = readline("Ingrese magic number: ");

	config_set_value(configMetadata, "BLOCK_SIZE", tamanioDeBloque);
	config_set_value(configMetadata, "BLOCKS", numeroDeBloques);
	config_set_value(configMetadata, "MAGIC_NUMBER", magicNumber);
	config_save(configMetadata);

	free(tamanioDeBloque);

	free(magicNumber);

	int bitmapDescriptor = open(pathBitmap, O_CREAT | O_RDWR | O_TRUNC , S_IRWXU);
	if(bitmapDescriptor == -1){
		log_error(logger_LFS, "Error al crear bitmap");
		log_info(logger_LFS, "Finalizando Lissandra File System");
		exit(EXIT_FAILURE);
	}

	blocks = atoi(numeroDeBloques);

	if(ftruncate(bitmapDescriptor, atoi(numeroDeBloques)/8)){
		//todo error al truncar archivo
	}

	bitmap = mmap(NULL, blocks/8, PROT_READ | PROT_WRITE, MAP_SHARED, bitmapDescriptor, 0);
	bitarray = bitarray_create_with_mode(bitmap, atoi(numeroDeBloques)/8, LSB_FIRST);
	bitarray_destroy(bitarray);
	munmap(bitmap, blocks/8);
	close(bitmapDescriptor);
	free(numeroDeBloques);
}

void crearBloques(){
	char* fileBloque;
	for (int i = 1; i <= blocks; i++) {
		fileBloque = string_from_format("%s/%d.bin", pathBloques, i);
		FILE* bloqueFile = fopen(fileBloque, "w");
		if(bloqueFile){
			fclose(bloqueFile);
		}
		free(fileBloque);
	}
}

void levantarFS(char* pathBitmap){
	memtable = (t_memtable*) malloc(sizeof(t_memtable));
	memtable->tablas = list_create();

	bitmapDescriptor = open(pathBitmap, O_RDWR);
	bitmap = mmap(NULL, blocks/8, PROT_READ | PROT_WRITE, MAP_SHARED, bitmapDescriptor, 0);
	bitarray = bitarray_create_with_mode(bitmap, blocks/8, LSB_FIRST);
}

void liberarMemoriaLFS(){
	log_info(logger_LFS, "Finalizando LFS");
	free(pathTablas);
	free(pathMetadata);
	free(pathBloques);
	free(pathRaiz);
	list_destroy_and_destroy_elements(memtable->tablas, (void*) vaciarTabla);
	bitarray_destroy(bitarray);
	munmap(bitmap, blocks/8);
	close(bitmapDescriptor);
	free(memtable);
	log_destroy(logger_LFS);

	config_destroy(configMetadata);
	config_destroy(config);
}

void* leerDeConsola(void* arg) {
	while (1) {
		mensaje = readline(">");
		if (!(strncmp(mensaje, "", 1) != 0)) {
			pthread_cancel(hiloRecibirMemorias);
			pthread_cancel(hiloDumpeo);
			free(mensaje);
			break;
		}
		if(!validarMensaje(mensaje, LFS, logger_LFS)){
			char** request = string_n_split(mensaje, 2, " ");
			cod_request palabraReservada = obtenerCodigoPalabraReservada(request[0], LFS);
			interpretarRequest(palabraReservada, mensaje, NULL);
			liberarArrayDeChar(request);

		}else{
			log_error(logger_LFS, "Request invalida");
		}
		free(mensaje);
	}
	return NULL;
}

void* recibirMemorias(void* arg) {
	char* puerto = config_get_string_value(config, "PUERTO");
	char* ip = config_get_string_value(config, "IP");
	int lissandraFS_fd = iniciar_servidor(puerto, ip);
	log_info(logger_LFS, "Lissandra lista para recibir Memorias");

	pthread_t hiloRequest;

	while (1) {
		int memoria_fd = esperar_cliente(lissandraFS_fd);
		if(memoria_fd > 0) {
			if(!pthread_create(&hiloRequest, NULL, (void*) conectarConMemoria, (void*) memoria_fd)) {
				char* mensaje = string_from_format("Se conecto la memoria %d", memoria_fd);
				enviarHandshakeLFS(30, memoria_fd);
				log_info(logger_LFS, mensaje);
				pthread_detach(hiloRequest);
				free(mensaje);
			} else {
				char* error = string_from_format("Error al iniciar el hilo de la memoria %d", memoria_fd);
				log_error(logger_LFS, error);
				free(error);
			}
		}
	}
	return NULL;
}

void* conectarConMemoria(void* arg) {
	int memoria_fd = (int)arg;
	while (1) {
		t_paquete* paqueteRecibido = recibir(memoria_fd);
		cod_request palabraReservada = paqueteRecibido->palabraReservada;
		//si viene -1 es porque se desconecto la memoria
		if (palabraReservada == -1){
			eliminar_paquete(paqueteRecibido);
			log_error(logger_LFS, "el cliente se desconecto. Terminando servidor");
			close(memoria_fd);
			break;
		}
		printf("De la memoria nro: %d \n", memoria_fd);
		interpretarRequest(palabraReservada, paqueteRecibido->request, &memoria_fd);
		eliminar_paquete(paqueteRecibido);
	}
	return NULL;
}

void interpretarRequest(cod_request palabraReservada, char* request, int* memoria_fd) {
	char** requestSeparada = separarRequest(request);
	errorNo errorNo = SUCCESS;
	char* mensaje = strdup("");
	//TODO case memoria se desconecto
	switch (palabraReservada){
		case SELECT:
			log_info(logger_LFS, "Me llego un SELECT");
			errorNo = procesarSelect(requestSeparada[1], requestSeparada[2], &mensaje);
			break;
		case INSERT:
			log_info(logger_LFS, "Me llego un INSERT");
			unsigned long long timestamp;
			if(longitudDeArrayDeStrings(requestSeparada) == 5) { //4 parametros + INSERT
				convertirTimestamp(requestSeparada[4], &timestamp);
				errorNo = procesarInsert(requestSeparada[1], convertirKey(requestSeparada[2]), requestSeparada[3], timestamp);
			} else {
				errorNo = procesarInsert(requestSeparada[1], convertirKey(requestSeparada[2]), requestSeparada[3], obtenerHoraActual());
			}
			break;
		case CREATE:
			log_info(logger_LFS, "Me llego un CREATE");
			//TODO validar los tipos de los parametros (ejemplo, SC, cantidad de particiones, etc.)
			errorNo = procesarCreate(requestSeparada[1], requestSeparada[2], requestSeparada[3], requestSeparada[4]);
			break;
		case DESCRIBE:
			if(longitudDeArrayDeStrings(requestSeparada) == 2){
				errorNo = procesarDescribe(requestSeparada[1], &mensaje);
			}else{
				errorNo = procesarDescribe(NULL, &mensaje);
			}

			log_info(logger_LFS, "Me llego un DESCRIBE");
			break;
		case DROP:
			log_info(logger_LFS, "Me llego un DROP");
			break;
		case NUESTRO_ERROR:
			if (memoria_fd != NULL) {
				log_error(logger_LFS, "El cliente se desconecto");
			}
			break;
		default:
			break;
	}

	char* mensajeDeError;
	//TODO case memoria se desconecto
	switch(errorNo){
		case SUCCESS:
			mensajeDeError=strdup("Request ejecutada correctamente");
			break;
		case TABLA_EXISTE:
			mensajeDeError = string_from_format("La tabla %s ya existe", requestSeparada[1]);
			log_error(logger_LFS, mensajeDeError);
			break;
		case ERROR_CREANDO_ARCHIVO:
			mensajeDeError = string_from_format("Error al crear un archivo de la tabla %s", requestSeparada[1]);
			log_error(logger_LFS, mensajeDeError);
			break;
		case TABLA_NO_EXISTE:
			mensajeDeError = string_from_format("La tabla %s no existe", requestSeparada[1]);
			log_info(logger_LFS, mensajeDeError);
			break;
		case KEY_NO_EXISTE:
			mensajeDeError = string_from_format("La KEY %s no existe", requestSeparada[2]); // ODO mostrar bien mensaje de error
			log_info(logger_LFS, mensajeDeError);
			break;
		default:
			break;
	}

	free(mensajeDeError);



	if (memoria_fd != NULL) {
		enviar(errorNo, mensaje, *memoria_fd);
	}else{
		log_info(logger_LFS, mensaje);
	}
	free(mensaje);
	liberarArrayDeChar(requestSeparada);
}


int obtenerBloqueDisponible(errorNo* errorNo) {

	int index = 0;
	while (index < blocks && bitarray_test_bit(bitarray, index) == 1) index++;
	if(index >= blocks) {
		index = -1;
	}else{
		bitarray_set_bit(bitarray, index);
	}
	return index;
}


/* vaciarTabla()
  * Parametros:
 * 	-> tabla :: t_tabla*
 * Descripcion: vacia una tabla y todos sus registros
 * Return: void */
void vaciarTabla(t_tabla *tabla) {
	void eliminarRegistros(t_registro* registro) {
	    free(registro->value);
	    free(registro);
	}
    free(tabla->nombreTabla);
    list_clean_and_destroy_elements(tabla->registros, (void*) eliminarRegistros);
    free(tabla->registros);
    free(tabla);
}