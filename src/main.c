#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>

#include <pharoClient.h>
#include <pharo.h>


int loadAndExecuteVM(VM_PARAMETERS* parameters){
	if(!initPharoVM(parameters->imageFile, parameters->vmParams, parameters->vmParamsCount, parameters->imageParams, parameters->imageParamsCount)){
		logError("Error opening image file: %s\n", parameters->imageFile);
		exit(-1);
	}
	runInterpreter();
}

int runThread(void* parameters){
	loadAndExecuteVM((VM_PARAMETERS*) parameters);
}


int main(int argc, char* argv[]){
	pthread_t thread_id;

	VM_PARAMETERS parameters;

	parseArguments(argc, argv, &parameters);

	logInfo("Opening Image: %s\n", parameters.imageFile);

	//This initialization is required because it makes awful, awful, awful code to calculate
	//the location of the machine code.
	//Luckily, it can be cached.
	osCogStackPageHeadroom();

	pthread_attr_t tattr;

	pthread_attr_init(&tattr);

	size_t size;
	pthread_attr_getstacksize(&tattr, &size);

	printf("%ld\n", size);

    if(pthread_attr_setstacksize(&tattr, size*4)){
		perror("Thread attr");
    }

	if(pthread_create(&thread_id, &tattr, runThread, &parameters)){
		perror("Thread creation");
	}

	pthread_join(thread_id, NULL);
}

void printVersion(){
	printf("%s\n", getVMVersion());
	printf("Built from: %s\n", getSourceVersion());
}
