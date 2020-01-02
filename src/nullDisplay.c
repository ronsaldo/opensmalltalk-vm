#include "pharovm/pharo.h"

#define UNIMPLEMENTED unimplementedPrimitive(__FUNCTION__);

void unimplementedPrimitive(const char* name){
	logWarn("Unimplemented primitive: %s\n", name);
}

#ifndef USE_SDL2_LEGACY_DISPLAY

void
ioInitWindowSystem(int headlessMode)
{
	(void)headlessMode;
	// The null display does nothing.
}

void
ioShutdownWindowSystem()
{
	// The null display does nothing here.
}

static void
recordMouseWheel(int keyCode)
{
    int modifiers = modifiersState ^ CtrlKeyBit;

    {
        sqEventUnion event;
        memset(&event, 0, sizeof(event));
        event.key.type = EventTypeKeyboard;
        event.key.pressCode = EventKeyDown;
        event.key.charCode = keyCode;
        event.key.utf32Code = keyCode;
        event.key.modifiers = modifiers;
        recordEvent(&event);
    }

    {
        sqEventUnion event;
        memset(&event, 0, sizeof(event));
        event.key.type = EventTypeKeyboard;
        event.key.pressCode = EventKeyChar;
        event.key.charCode = keyCode;
        event.key.utf32Code = keyCode;
        event.key.modifiers = modifiers;
        recordEvent(&event);
    }

    {
        sqEventUnion event;
        memset(&event, 0, sizeof(event));
        event.key.type = EventTypeKeyboard;
        event.key.pressCode = EventKeyUp;
        event.key.charCode = keyCode;
        event.key.utf32Code = keyCode;
        event.key.modifiers = modifiers;
        recordEvent(&event);
    }
}

static void
handleMouseWheel(const SDL_Event *rawEvent)
{
    if(rawEvent->wheel.windowID != windowID)
    {
        recordSDLEvent(rawEvent);
        return;
    }

    if(rawEvent->wheel.x < 0)
    {
        recordMouseWheel(28);
    }
    else if(rawEvent->wheel.x > 0)
    {
        recordMouseWheel(29);
    }

    if(rawEvent->wheel.y > 0)
    {
        recordMouseWheel(30);
    }
    else if(rawEvent->wheel.y < 0)
    {
        recordMouseWheel(31);
    }
}

static void
handleKeyDown(const SDL_Event *rawEvent)
{
    int character;
    int isSpecial;
    int hasRightAlt;

    hasRightAlt = (rawEvent->key.keysym.mod & KMOD_RALT) != 0;
    modifiersState = convertModifiers(rawEvent->key.keysym.mod);
    if(rawEvent->key.windowID != windowID)
    {
        recordSDLEvent(rawEvent);
        return;
    }

    character = convertSpecialKeySymToCharacter(rawEvent->key.keysym.sym);
    isSpecial = character != 0;
    if(!character)
        character = convertKeySymToCharacter(rawEvent->key.keysym.sym);

    {
        sqEventUnion event;
        memset(&event, 0, sizeof(event));
        event.key.type = EventTypeKeyboard;
        event.key.timeStamp = rawEvent->key.timestamp;
        event.key.pressCode = EventKeyDown;
        event.key.charCode = rawEvent->key.keysym.sym;
        event.key.utf32Code = character;
        event.key.modifiers = modifiersState;
        recordEvent(&event);
    }

    /* We need to send a key stroke for some special circumstances. */
    if(!isSpecial && (!modifiersState || modifiersState == ShiftKeyBit || hasRightAlt))
        return;

    if(character && character != 27)
    {
        sqEventUnion event;
        memset(&event, 0, sizeof(event));
        event.key.type = EventTypeKeyboard;
        event.key.timeStamp = rawEvent->key.timestamp;
        event.key.pressCode = EventKeyChar;
        event.key.charCode = character;
        event.key.utf32Code = character;
        event.key.modifiers = modifiersState;
        recordEvent(&event);
    }
}

static void
handleKeyUp(const SDL_Event *rawEvent)
{
    modifiersState = convertModifiers(rawEvent->key.keysym.mod);
    if(rawEvent->key.windowID != windowID)
    {
        recordSDLEvent(rawEvent);
        return;
    }

    sqEventUnion event;
    memset(&event, 0, sizeof(event));
    event.key.type = EventTypeKeyboard;
    event.key.timeStamp = rawEvent->key.timestamp;
    event.key.pressCode = EventKeyUp;
    event.key.charCode = convertKeySymToCharacter(rawEvent->key.keysym.sym);
    event.key.utf32Code = convertKeySymToCharacter(rawEvent->key.keysym.sym);
    event.key.modifiers = modifiersState;
    recordEvent(&event);
}

static void
handleTextInput(const SDL_Event *rawEvent)
{
    int utf32;
    const char *position;

    if(rawEvent->text.windowID != windowID)
    {
        recordSDLEvent(rawEvent);
        return;
    }

    sqEventUnion event;
    memset(&event, 0, sizeof(event));
    event.key.type = EventTypeKeyboard;
    event.key.timeStamp = rawEvent->text.timestamp;
    event.key.pressCode = EventKeyChar;
    event.key.modifiers = modifiersState;

    position = rawEvent->text.text;
    while(*position)
    {
        position = sqUTF8ToUTF32Iterate(position, &utf32);
        if(!utf32)
            break;

        event.key.charCode = utf32;
        event.key.utf32Code = utf32;
        recordEvent(&event);
    }
}

static void
handleMouseButtonDown(const SDL_Event *rawEvent)
{
    if(rawEvent->button.windowID != windowID)
    {
        recordSDLEvent(rawEvent);
        return;
    }

    buttonState |= convertButton(rawEvent->button.button);

    sqEventUnion event;
    memset(&event, 0, sizeof(event));
    event.mouse.type = EventTypeMouse;
    event.mouse.timeStamp = rawEvent->button.timestamp;
    event.mouse.x = mousePositionX = rawEvent->button.x;
    event.mouse.y = mousePositionY = rawEvent->button.y;
    event.mouse.buttons = buttonState;
    event.mouse.modifiers = modifiersState;
    event.mouse.nrClicks = rawEvent->button.clicks;
    recordEvent(&event);
}

static void
handleMouseButtonUp(const SDL_Event *rawEvent)
{
    if(rawEvent->button.windowID != windowID)
    {
        recordSDLEvent(rawEvent);
        return;
    }

    buttonState &= ~convertButton(rawEvent->button.button);

    sqEventUnion event;
    memset(&event, 0, sizeof(event));
    event.mouse.type = EventTypeMouse;
    event.mouse.timeStamp = rawEvent->button.timestamp;
    event.mouse.x = mousePositionX = rawEvent->button.x;
    event.mouse.y = mousePositionY = rawEvent->button.y;
    event.mouse.buttons = buttonState;
    event.mouse.modifiers = modifiersState;
    event.mouse.nrClicks = rawEvent->button.clicks;
    recordEvent(&event);
}

static void
handleMouseMotion(const SDL_Event *rawEvent)
{
    if(rawEvent->motion.windowID != windowID)
    {
        recordSDLEvent(rawEvent);
        return;
    }

    sqEventUnion event;
    memset(&event, 0, sizeof(event));
    event.mouse.type = EventTypeMouse;
    event.mouse.timeStamp = rawEvent->motion.timestamp;
    event.mouse.x = mousePositionX = rawEvent->motion.x;
    event.mouse.y = mousePositionY = rawEvent->motion.y;
    event.mouse.buttons = buttonState;
    event.mouse.modifiers = modifiersState;
    recordLowImportanceEvent(&event);
}

static void
handleWindowEvent(const SDL_Event *rawEvent)
{
    if(rawEvent->window.windowID != windowID)
    {
        recordSDLEvent(rawEvent);
        return;
    }

    switch(rawEvent->window.event)
    {
    case SDL_WINDOWEVENT_CLOSE:
        {
            sqEventUnion event;
            memset(&event, 0, sizeof(event));
            event.window.type = EventTypeWindow;
            event.window.timeStamp = rawEvent->window.timestamp;
            event.window.action = WindowEventClose;
            recordEvent(&event);
        }
        break;
    case SDL_WINDOWEVENT_MOVED:
    case SDL_WINDOWEVENT_SIZE_CHANGED:
    case SDL_WINDOWEVENT_RESIZED:
        {
            sqEventUnion event;
            SDL_Rect rect;
            SDL_GetWindowPosition(window, &rect.x, &rect.y);
            SDL_GetRendererOutputSize(windowRenderer, &rect.w, &rect.h);
            memset(&event, 0, sizeof(event));
            event.window.type = EventTypeWindow;
            event.window.timeStamp = rawEvent->window.timestamp;
            event.window.action = WindowEventMetricChange;
            event.window.value1 = rect.x;
            event.window.value2 = rect.y;
            event.window.value3 = rect.x + rect.w;
            event.window.value4 = rect.y + rect.h;
            recordEvent(&event);
        }
        break;
    }
}


#ifdef WIN64
int ioSetCursorARGB(sqInt cursorBitsIndex, sqInt extentX, sqInt extentY, sqInt offsetX, sqInt offsetY)
#else
sqInt ioSetCursorARGB(sqInt cursorBitsIndex, sqInt extentX, sqInt extentY, sqInt offsetX, sqInt offsetY)
#endif
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioForceDisplayUpdate(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioSetFullScreen(sqInt fullScreen)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioSetCursor(sqInt cursorBitsIndex, sqInt offsetX, sqInt offsetY)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioSetCursorWithMask(sqInt cursorBitsIndex, sqInt cursorMaskIndex, sqInt offsetX, sqInt offsetY)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioShowDisplay(sqInt dispBitsIndex, sqInt width, sqInt height, sqInt depth,
		    sqInt affectedL, sqInt affectedR, sqInt affectedT, sqInt affectedB)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioHasDisplayDepth(sqInt depth)
{
	return true;
}

sqInt
ioSetDisplayMode(sqInt width, sqInt height, sqInt depth, sqInt fullscreenFlag)
{
	UNIMPLEMENTED
	return 0;
}

char*
ioGetWindowLabel(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioSetWindowLabelOfSize(void *lblIndex, sqInt sz)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioGetWindowWidth(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioGetWindowHeight(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioSetWindowWidthHeight(sqInt w, sqInt h)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioIsWindowObscured(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioGetNextEvent(sqInputEvent *evt)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioGetButtonState(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioGetKeystroke(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioMousePoint(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioPeekKeystroke(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioProcessEvents(void)
{
    aioPoll(0);
    return 0;
}

double
ioScreenScaleFactor(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioScreenSize(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt
ioScreenDepth(void)
{
	UNIMPLEMENTED
	return 0;
}

sqInt clipboardSize(void)
{
	UNIMPLEMENTED
    return 0;
}

sqInt
clipboardReadIntoAt(sqInt count, sqInt byteArrayIndex, sqInt startIndex)
{
	UNIMPLEMENTED
    return 0;
}

sqInt
clipboardWriteFromAt(sqInt count, sqInt byteArrayIndex, sqInt startIndex)
{
	UNIMPLEMENTED
    return 0;
}

static sqInt
dropInit (void)
{
	UNIMPLEMENTED
    return 0;
}

static sqInt
dropShutdown (void)
{
	UNIMPLEMENTED
    return 0;
}

char*
dropRequestFileName(sqInt dropIndex)
{
	UNIMPLEMENTED
    return 0;
}

sqInt
dropRequestFileHandle(sqInt dropIndex)
{
	UNIMPLEMENTED
    return nilObject();
}

sqInt ioSetInputSemaphore(sqInt semaIndex){
	UNIMPLEMENTED;
	return 1;
}

void ioSignalInputEvent(void){
	UNIMPLEMENTED;
}

#endif

sqInt
ioFormPrint(sqInt bitsAddr, sqInt width, sqInt height, sqInt depth,
		  double hScale, double vScale, sqInt landscapeFlag)
{
	UNIMPLEMENTED
	return 0;
}

void
ioNoteDisplayChangedwidthheightdepth(void *b, int w, int h, int d)
{
	UNIMPLEMENTED
}

sqInt
ioBeep(void)
{
	UNIMPLEMENTED
    return 0;
}

sqInt
ioDisablePowerManager(sqInt disableIfNonZero){
	UNIMPLEMENTED
    return true;
}

void
ioClearProfile(void){
	UNIMPLEMENTED
}

long
ioControlNewProfile(int on, unsigned long buffer_size){
	UNIMPLEMENTED
	return 0;
}


int plugInNotifyUser(char *msg) {
	UNIMPLEMENTED
	return 0;
}

int plugInTimeToReturn(void) {
    return false;
}

void
ioNewProfileStatus(sqInt *running, long *buffersize)
{
	UNIMPLEMENTED
}

long
ioNewProfileSamplesInto(void *sampleBuffer)
{
	UNIMPLEMENTED
    return 0;
}


sqInt crashInThisOrAnotherThread(sqInt flags)
{
	UNIMPLEMENTED
	return 0;
}

char* ioGetLogDirectory(void){
    return "";
}

sqInt ioSetLogDirectoryOfSize(void* lblIndex, sqInt sz){
    return 1;
}
