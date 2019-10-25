#ifndef PHAROVM_PARAMETERS_H
#define PHAROVM_PARAMETERS_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "errorCodes.h"
#include <stdbool.h> // For bool
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/**

 OSX:

  --memory <size>[mk]    use fixed heap size (added to image size)
  --nohandlers           disable sigsegv & sigusr1 handlers
  --timephases           print start load and run times
  --breaksel selector    set breakpoint on send of selector
  --breakmnu selector    set breakpoint on MNU of selector
  --eden <size>[mk]      set eden memory to bytes
  --leakcheck num        check for leaks in the heap
  --stackpages num       use n stack pages
  --numextsems num       make the external semaphore table num in size
  --noheartbeat          disable the heartbeat for VM debugging. disables input
  --pollpip              output . on each poll for input
  --checkpluginwrites    check for writes past end of object in plugins
  --trace[=num]          enable tracing (optionally to a specific value)
  --warnpid              print pid in warnings
  --codesize <size>[mk]  set machine code memory to bytes
  --tracestores          enable store tracing (assert check stores)
  --cogmaxlits <n>       set max number of literals for methods to be compiled to machine code
  --cogminjumps <n>      set min number of backward jumps for interpreted methods to be considered for compilation to machine code
  --reportheadroom       report unused stack headroom on exit
  --maxoldspace <size>[mk]      set max size of old space memory to bytes
  --logscavenge          log scavenging to scavenge.log
  --headless             run in headless (no window) mode (default: false)
  --headfull             run in headful (window) mode (default: true)
  --version              print version information, then exit
  --blockonerror         on error or segv block, not exit.  useful for attaching gdb
  --blockonwarn          on warning block, don't warn.  useful for attaching gdb
  --exitonwarn           treat warnings as errors, exiting on warn


LINUX:

  -encoding <enc>       set the internal character encoding (default: MacRoman)
  -help                 print this help message, then exit
  -memory <size>[mk]    use fixed heap size (added to image size)
  -mmap <size>[mk]      limit dynamic heap size (default: 1024m)
  -timephases           print start load and run times
  -breaksel selector    set breakpoint on send of selector
  -breakmnu selector    set breakpoint on MNU of selector
  -eden <size>[mk]      use given eden size
  -leakcheck num        check for leaks in the heap
  -stackpages <num>     use given number of stack pages
  -noevents             disable event-driven input support
  -nohandlers           disable sigsegv & sigusr1 handlers
  -pollpip              output . on each poll for input
  -checkpluginwrites    check for writes past end of object in plugins
  -pathenc <enc>        set encoding for pathnames (default: UTF-8)
  -plugins <path>       specify alternative plugin location (see manpage)
  -textenc <enc>        set encoding for external text (default: UTF-8)
  -version              print version information, then exit
  -vm-<sys>-<dev>       use the <dev> driver for <sys> (see below)
  -trace[=num]          enable tracing (optionally to a specific value)
  -warnpid              print pid in warnings
  -codesize <size>[mk]  set machine code memory to bytes
  -tracestores          enable store tracing (assert check stores)
  -cogmaxlits <n>       set max number of literals for methods compiled to machine code
  -cogminjumps <n>      set min number of backward jumps for interpreted methods to be considered for compilation to machine code
  -reportheadroom       report unused stack headroom on exit
  -maxoldspace <size>[mk]    set max size of old space memory to bytes
  -logscavenge          log scavenging to scavenge.log
  -blockonerror         on error or segv block, not exit.  useful for attaching gdb
  -blockonwarn          on warning block, don't warn.  useful for attaching gdb
  -exitonwarn           treat warnings as errors, exiting on warn
Deprecated:
  -notimer              disable interval timer for low-res clock
  -display <dpy>        equivalent to '-vm-display-X11 -display <dpy>'
  -headless             equivalent to '-vm-display-X11 -headless'
  -nodisplay            equivalent to '-vm-display-null'
  -nomixer              disable modification of mixer settings
  -nosound              equivalent to '-vm-sound-null'
  -quartz               equivalent to '-vm-display-Quartz'


 */

/**
 * Parameter vector.
 * I am used to hold an array of arguments.
 */
typedef struct pharovm_parameter_vector_s
{
	uint32_t count;
	const char ** parameters;
} pharovm_parameter_vector_t;

pharovm_error_code_t pharovm_parameter_vector_destroy(pharovm_parameter_vector_t *vector);
pharovm_error_code_t pharovm_parameter_vector_insertFrom(pharovm_parameter_vector_t *vector, uint32_t count, const char **arguments);

typedef struct pharovm_parameters_s
{
	/**
	 * The image file name.
	 * This string is owned by the \ref pharovm_parameters_t structure, so it should be assigned by using strdup.
	 */
	char* imageFileName;

	bool isDefaultImage;
	/// Do we have multiple default image, so a file dialog should be shown?
	uint32_t defaultImageCount;

	/// Is this a forced startup image?
	bool isForcedStartupImage;

	/// Has the image been selected interactively by an user (e.g: by using a file open dialog.).
	bool hasBeenSelectedByUserInteractively;

	// FIXME: Why passing this is needed when we have the separated vectors?
	int processArgc;
	const char** processArgv;

	// FIXME: Passing this environment vector seems hackish. getenv should be used instead.
	const char** environmentVector;

	pharovm_parameter_vector_t vmParameters;
	pharovm_parameter_vector_t imageParameters;
} pharovm_parameters_t;

/**
 * Parse an argument vector into a VM parameter holding structure.
 * \param parsedParameters the resulting parsed parameters.
 */
pharovm_error_code_t pharovm_parameters_parse(int argc, const char** argv, pharovm_parameters_t *parsedParameters);

/**
 * This ensures that the interactive parameter is passed to the image when required.
 */
pharovm_error_code_t pharovm_parameters_ensureInteractiveImageParameter(pharovm_parameters_t* parameters);

/**
 * Destroy an allocated instance \ref pharovm_parameters_t.
 */
pharovm_error_code_t pharovm_parameters_destroy(pharovm_parameters_t *parameters);

/**
 * Prints the command line parameter usage string to a file.
 */
void pharovm_printUsageTo(FILE *output);

#ifdef __cplusplus
}
#endif

#endif //PHAROVM_PARAMETERS_H