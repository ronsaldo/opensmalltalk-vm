#include "pharovm/pharo.h"

#ifdef USE_SDL2_LEGACY_DISPLAY
#include <SDL2/SDL.h>

#define VM_EVENT_QUEUE_SIZE 256

typedef union VMEventUnion
{
    sqIntptr_t type;
    sqInputEvent input;
    sqKeyboardEvent key;
    sqMouseEvent mouse;
    sqWindowEvent window;
    sqDragDropFilesEvent dnd;
    sqMenuEvent menu;
    sqComplexEvent complex;
} VMEventUnion;

typedef struct VMEventQueue
{
    int readIndex;
    int writeIndex;
    VMEventUnion elements[VM_EVENT_QUEUE_SIZE];
} VMEventQueue;

#define UNIMPLEMENTED printf("TODO: Implement %s\n", __FUNCTION__);

void ioSignalInputEvent(void);
extern sqInt getSavedWindowSize(void);

static int sdl2DisplayHeadless;
static SDL_Window *window;
static Uint32 windowID;
static SDL_Renderer *windowRenderer;
static SDL_Texture *windowTexture;
static SDL_Cursor *currentCursor;
static int windowTextureWidth;
static int windowTextureHeight;

static int buttonState = 0;
static int modifiersState = 0;

static int newSDLEvent = 0;
static int newDisplayEvent = 0;
static VMEventQueue vmEventQueue;

static int mousePositionX = 0;
static int mousePositionY = 0;

static sqInt sdl2InputEventSemaIndex = 0;
static char droppedFileName[FILENAME_MAX];

static SDL_GLContext currentOpenGLContext = 0;
static SDL_Window *currentOpenGLWindow = 0;
static int openglStoreCount = 0;

/// FIXME: This probably should be move into an utility file.
static const char *
vm_utf8_to_utf32_iterate(const char *string, int *dest)
{
    unsigned int first;
    unsigned int sequenceSize;
    unsigned int i;
    unsigned int byte;
    *dest = 0;

    first = (*string) & 0xFF;
    if(first == 0)
        return string;

    /* Single byte case */
    ++string;
    if(first <= 127)
    {
        *dest = first;
        return string;
    }

    /* Count the size of the character */
    sequenceSize = 0;
    while(first & 0x80)
    {
        first = (first << 1) & 0xFF;
        ++sequenceSize;
    }

    first >>= sequenceSize;

    /* Decode the full code point. */
    *dest = first;
    --sequenceSize;

    for(i = 0; i < sequenceSize; ++i)
    {
        /* Fetch the next byte */
        byte = *string;
        if(!byte)
            return string;
        ++string;

        /* Append the byte data */
        *dest = (*dest << 6) | (byte & 63);
    }

    return string;
}

static int
vm_event_queue_is_empty(VMEventQueue *queue)
{
    return queue->readIndex == queue->writeIndex;
}

static int
vm_event_queue_is_full(VMEventQueue *queue)
{
    return ((queue->writeIndex + 1) & (VM_EVENT_QUEUE_SIZE - 1)) == queue->readIndex;
}

static void
vm_event_queue_push(VMEventQueue *queue, VMEventUnion value)
{
    queue->elements[queue->writeIndex] = value;
    queue->writeIndex = (queue->writeIndex + 1) & (VM_EVENT_QUEUE_SIZE - 1);
    if(vm_event_queue_is_empty(queue))
    {
        queue->readIndex = (queue->readIndex + 1) & (VM_EVENT_QUEUE_SIZE - 1);
    }
}

static void
vm_event_queue_pop_into(VMEventQueue *queue, VMEventUnion *result)
{
    if(!vm_event_queue_is_empty(queue))
    {
        *result = queue->elements[queue->readIndex];
        queue->readIndex = (queue->readIndex + 1) & (VM_EVENT_QUEUE_SIZE - 1);
    }
}

static void
storeOpenGLState(void)
{
    if(openglStoreCount == 0)
    {
        currentOpenGLContext = SDL_GL_GetCurrentContext();
        currentOpenGLWindow = SDL_GL_GetCurrentWindow();
    }
    ++openglStoreCount;
}

static void
restoreOpenGLState(void)
{
    --openglStoreCount;
    if(openglStoreCount == 0)
    {
        SDL_GL_MakeCurrent(currentOpenGLWindow, currentOpenGLContext);
        currentOpenGLContext = 0;
        currentOpenGLWindow = 0;
    }

    if(openglStoreCount < 0)
        abort();
}

static int
convertButton(int button)
{
#ifdef __APPLE__
    // On OS X, swap the middle and right buttons.
    switch(button)
    {
    case SDL_BUTTON_LEFT: return RedButtonBit;
    case SDL_BUTTON_RIGHT: return BlueButtonBit;
    case SDL_BUTTON_MIDDLE: return YellowButtonBit;
    default: return 0;
    }
#else
    switch(button)
    {
    case SDL_BUTTON_LEFT: return RedButtonBit;
    case SDL_BUTTON_MIDDLE: return YellowButtonBit;
    case SDL_BUTTON_RIGHT: return BlueButtonBit;
    default: return 0;
    }
#endif
    return 0;
}

static int
convertModifiers(int state)
{
    int result = 0;
    if(state & KMOD_SHIFT)
        result |= ShiftKeyBit;
    if(state & KMOD_CTRL) /* Alt-gr is received as RCtrl in some cases.*/
        result |= CtrlKeyBit;
    if(state & KMOD_RALT) /* Right alt is used for grammar purposes. */
        result |= OptionKeyBit;
    if(state & KMOD_LALT)
        result |= CommandKeyBit;
    return result;
}

static int
convertSpecialKeySymToCharacter(int symbol)
{
    switch(symbol)
    {
    case SDLK_RETURN: return '\r';
    case SDLK_BACKSPACE: return 8;
    case SDLK_TAB: return '\t';
    case SDLK_HOME: return 1;
    case SDLK_LEFT: return 28;
    case SDLK_UP: return 30;
    case SDLK_RIGHT: return 29;
    case SDLK_DOWN: return 31;
    case SDLK_END: return 4;
    case SDLK_INSERT: return 5;
    case SDLK_PAGEUP: return 11;
    case SDLK_PAGEDOWN: return 12;
    case SDLK_DELETE: return 127;
    default:
        return 0;
    }
}

static int
convertKeySymToCharacter(unsigned int symbol)
{
    if(symbol >= 0x400000)
        return 0;
    else
        return symbol;
}

void
ioInitWindowSystem(int headlessMode)
{
    sdl2DisplayHeadless = headlessMode;
    if(sdl2DisplayHeadless)
        return;

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE);
}

void
ioShutdownWindowSystem()
{
    if(sdl2DisplayHeadless)
        return;

    SDL_Quit();
}

static void
createWindow(sqInt width, sqInt height, sqInt fullscreenFlag)
{
    int flags;
    int actualWindowX, actualWindowY;
    int actualWindowWidth, actualWindowHeight;
    SDL_Rect displayBounds;

    if(window)
        return;

    storeOpenGLState();
    flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if(fullscreenFlag)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    modifiersState = convertModifiers(SDL_GetModState());
    window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    if(!window)
    {
        restoreOpenGLState();
        return;
    }

    if(!fullscreenFlag)
    {
        SDL_GetWindowPosition(window, &actualWindowX, &actualWindowY);
        SDL_GetWindowSize(window, &actualWindowWidth, &actualWindowHeight);
#if SDL_VERSION_ATLEAST(2, 5, 0)
        SDL_GetDisplayUsableBounds(0, &displayBounds);
#else
        SDL_GetDisplayBounds(0, &displayBounds);
#endif
        if(actualWindowWidth + actualWindowX >= displayBounds.w || actualWindowHeight + actualWindowY >= displayBounds.h)
            SDL_MaximizeWindow(window);
    }

    windowID = SDL_GetWindowID(window);
    windowRenderer = SDL_CreateRenderer(window, 0, 0);
    restoreOpenGLState();
}

static int
ensureTextureOfSize(sqInt width, sqInt height)
{
    if(windowTexture && windowTextureWidth == width && windowTextureHeight == height)
        return 0;

    storeOpenGLState();
    if(windowTexture)
        SDL_DestroyTexture(windowTexture);

    windowTexture = SDL_CreateTexture(windowRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    windowTextureWidth = width;
    windowTextureHeight = height;
    restoreOpenGLState();
    return 1;
}

static void
presentWindow()
{
    if(!window || !windowRenderer)
        return;

    storeOpenGLState();
    SDL_SetRenderDrawColor(windowRenderer, 0, 0, 0, 0);
    SDL_RenderClear(windowRenderer);

    if(windowTexture)
        SDL_RenderCopy(windowRenderer, windowTexture, NULL, NULL);

    SDL_RenderPresent(windowRenderer);
    restoreOpenGLState();
}

static void
recordSDLEvent(const SDL_Event *rawEvent)
{
    newSDLEvent = 1;
    //sqSDLEventQueuePush(sdlEventQueue, *rawEvent);
}

static void
recordEvent(VMEventUnion *event)
{
    newDisplayEvent = 1;
    vm_event_queue_push(&vmEventQueue, *event);
}

static void
recordLowImportanceEvent(VMEventUnion *event)
{
    if(vm_event_queue_is_full(&vmEventQueue))
        return;

    recordEvent(event);
}

static void
recordMouseWheel(int keyCode)
{
    int modifiers = modifiersState ^ CtrlKeyBit;

    {
        VMEventUnion event;
        memset(&event, 0, sizeof(event));
        event.key.type = EventTypeKeyboard;
        event.key.pressCode = EventKeyDown;
        event.key.charCode = keyCode;
        event.key.utf32Code = keyCode;
        event.key.modifiers = modifiers;
        recordEvent(&event);
    }

    {
        VMEventUnion event;
        memset(&event, 0, sizeof(event));
        event.key.type = EventTypeKeyboard;
        event.key.pressCode = EventKeyChar;
        event.key.charCode = keyCode;
        event.key.utf32Code = keyCode;
        event.key.modifiers = modifiers;
        recordEvent(&event);
    }

    {
        VMEventUnion event;
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
        VMEventUnion event;
        memset(&event, 0, sizeof(event));
        event.key.type = EventTypeKeyboard;
        event.key.timeStamp = rawEvent->key.timestamp;
        event.key.pressCode = EventKeyDown;
        event.key.charCode = rawEvent->key.keysym.sym;
        if(event.key.charCode >= 0x400000)
            event.key.charCode = 0;
        event.key.utf32Code = character;
        event.key.modifiers = modifiersState;
        recordEvent(&event);
    }

    /* We need to send a key stroke for some special circumstances. */
    if(!isSpecial && (!modifiersState || modifiersState == ShiftKeyBit || hasRightAlt))
        return;

    if(character && character != 27 && (character != rawEvent->key.keysym.sym || isSpecial))
    {
        VMEventUnion event;
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

    VMEventUnion event;
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

    VMEventUnion event;
    memset(&event, 0, sizeof(event));
    event.key.type = EventTypeKeyboard;
    event.key.timeStamp = rawEvent->text.timestamp;
    event.key.pressCode = EventKeyChar;
    event.key.modifiers = modifiersState;

    position = rawEvent->text.text;
    while(*position)
    {
        position = vm_utf8_to_utf32_iterate(position, &utf32);
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

    VMEventUnion event;
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

    VMEventUnion event;
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

    VMEventUnion event;
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
            VMEventUnion event;
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
            VMEventUnion event;
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

static void
handleDropFileEvent(const SDL_Event *rawEvent)
{
    VMEventUnion event;
    strcpy(droppedFileName, rawEvent->drop.file);
    SDL_free(rawEvent->drop.file);

    /* TODO: Support dropping files here or in the image.*/
    {
        event.dnd.type = EventTypeDragDropFiles;
        event.dnd.timeStamp = rawEvent->window.timestamp;
        event.dnd.dragType = SQDragDrop;
        event.dnd.numFiles = 1;
        event.dnd.x = mousePositionX;
        event.dnd.y = mousePositionY;
        event.dnd.modifiers = modifiersState;
        recordEvent(&event);
    }
}

static void
handleEvent(const SDL_Event *event)
{
    switch(event->type)
    {
    case SDL_KEYDOWN:
        handleKeyDown(event);
        break;
    case SDL_KEYUP:
        handleKeyUp(event);
        break;
    case SDL_TEXTINPUT:
        handleTextInput(event);
        break;
    case SDL_MOUSEBUTTONDOWN:
        handleMouseButtonDown(event);
        break;
    case SDL_MOUSEBUTTONUP:
        handleMouseButtonUp(event);
        break;
    case SDL_MOUSEMOTION:
        handleMouseMotion(event);
        break;
    case SDL_MOUSEWHEEL:
        handleMouseWheel(event);
        break;
    case SDL_WINDOWEVENT:
        handleWindowEvent(event);
        break;
    case SDL_DROPFILE:
        handleDropFileEvent(event);
        break;
    default:
        /* Record the unhandled SDL events for the image. */
        recordSDLEvent(event);
        break;
    }
}

static void
handleEvents()
{
    SDL_Event event;
    while(SDL_PollEvent(&event))
        handleEvent(&event);

    if(newDisplayEvent)
        ioSignalInputEvent();
    //if(newSDLEvent)
    //    sdl2SignalInputEvent();

    newDisplayEvent = 0;
    newSDLEvent = 0;
}

#ifdef WIN64
int ioSetCursorARGB(sqInt cursorBitsIndex, sqInt extentX, sqInt extentY, sqInt offsetX, sqInt offsetY)
#else
sqInt ioSetCursorARGB(sqInt cursorBitsIndex, sqInt extentX, sqInt extentY, sqInt offsetX, sqInt offsetY)
#endif
{
	return false;
}

sqInt
ioForceDisplayUpdate(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    presentWindow();
    return 0;
}

sqInt
ioSetFullScreen(sqInt fullScreen)
{
    if(sdl2DisplayHeadless)
        return 0;

	UNIMPLEMENTED
	return 0;
}

sqInt
ioSetCursor(sqInt cursorBitsIndex, sqInt offsetX, sqInt offsetY)
{
    if(sdl2DisplayHeadless)
        return 0;

	UNIMPLEMENTED
	return 0;
}

sqInt
ioSetCursorWithMask(sqInt cursorBitsIndex, sqInt cursorMaskIndex, sqInt offsetX, sqInt offsetY)
{
    if(sdl2DisplayHeadless)
        return 0;

    SDL_Cursor *newCursor;
    Uint8 convertedCursorBits[32];
    Uint8 convertedCursorMask[32];
    int i;

    unsigned int *cursorBits = (unsigned int*)pointerForOop(cursorBitsIndex);
    unsigned int *cursorMask = (unsigned int*)pointerForOop(cursorMaskIndex);

    if (cursorMaskIndex == null)
        cursorMask = cursorBits;

    /* Remove the extra padding */
    for(i = 0; i < 16; ++i)
    {
        convertedCursorBits[i*2 + 0]= (cursorBits[i] >> 24) & 0xFF;
        convertedCursorBits[i*2 + 1]= (cursorBits[i] >> 16) & 0xFF;
        convertedCursorMask[i*2 + 0]= (cursorMask[i] >> 24) & 0xFF;
        convertedCursorMask[i*2 + 1]= (cursorMask[i] >> 16) & 0xFF;
    }

    /* Create and set the new cursor. */
    newCursor = SDL_CreateCursor(convertedCursorBits, convertedCursorMask, 16, 16, -offsetX, -offsetY);
    if(newCursor)
    {
        SDL_SetCursor(newCursor);
        if(currentCursor)
            SDL_FreeCursor(currentCursor);
        currentCursor = newCursor;
    }

    return 0;
}

static void
blitRect32(
    int surfaceWidth, int surfaceHeight,
    uint8_t *sourcePixels, int sourcePitch,
    uint8_t *destPixels, int destPitch,
    int copyX, int copyY, int width, int height)
{
    int y;

    if(sourcePitch == destPitch &&
        surfaceWidth == width && surfaceHeight == height && copyX == 0 && copyY == 0)
    {
        memcpy(destPixels, sourcePixels, destPitch*height);
    }
    else if(sourcePitch == destPitch)
    {
        destPixels += copyY*destPitch;
        sourcePixels += copyY*sourcePitch;
        memcpy(destPixels, sourcePixels, destPitch*height);
    }
    else
    {
        int copyPitch = destPitch;
        if(sourcePitch < copyPitch)
            copyPitch = sourcePitch;

        destPixels += copyY*destPitch;
        sourcePixels += copyY*sourcePitch;

        for(y = 0; y < height; ++y)
        {
            memcpy(destPixels, sourcePixels, copyPitch);
            destPixels += destPitch;
            sourcePixels += sourcePitch;
        }
    }
}

sqInt
ioShowDisplay(sqInt dispBitsIndex, sqInt width, sqInt height, sqInt depth,
		    sqInt affectedL, sqInt affectedR, sqInt affectedT, sqInt affectedB)
{
    if(sdl2DisplayHeadless)
        return 0;

    storeOpenGLState();
    if(!window)
        createWindow(width, height, 0);

    SDL_Rect modifiedRect;
    modifiedRect.x = affectedL;
    modifiedRect.y = affectedT;
    modifiedRect.w = affectedR - affectedL;
    modifiedRect.h = affectedB - affectedT;

    /* Make sure the texture has the correct extent. */
    if(ensureTextureOfSize(width, height))
    {
        /*If the texture was recreated, we have to upload the whole texture*/
        modifiedRect.x = 0;
        modifiedRect.y = 0;
        modifiedRect.w = width;
        modifiedRect.h = height;
    }

    if(!windowTexture)
    {
        restoreOpenGLState();
        return 0;
    }

    uint8_t *pixels;
    int pitch;
    if(SDL_LockTexture(windowTexture, NULL, (void**)&pixels, &pitch))
    {
        restoreOpenGLState();
        return 0;
    }

    int sourcePitch = windowTextureWidth*4;
    blitRect32(windowTextureWidth, windowTextureHeight,
        (uint8_t*)pointerForOop(dispBitsIndex), sourcePitch,
        pixels, pitch,
        modifiedRect.x, modifiedRect.y, modifiedRect.w, modifiedRect.h
    );

    SDL_UnlockTexture(windowTexture);
    presentWindow();
    restoreOpenGLState();
    return 0;
}

sqInt
ioHasDisplayDepth(sqInt depth)
{
    if(sdl2DisplayHeadless)
        return true;

    return depth == 32;
}

sqInt
ioSetDisplayMode(sqInt width, sqInt height, sqInt depth, sqInt fullscreenFlag)
{
    if(sdl2DisplayHeadless)
        return 0;

    if(window)
    {
        storeOpenGLState();
        ioSetWindowWidthHeight(width, height);
        ioSetFullScreen(fullscreenFlag);
        restoreOpenGLState();
        return 0;
    }

    storeOpenGLState();
    createWindow(width, height, fullscreenFlag);
    restoreOpenGLState();
    return 0;
}

char*
ioGetWindowLabel(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    return (char*)SDL_GetWindowTitle(window);
}

sqInt
ioSetWindowLabelOfSize(void *lblIndex, sqInt size)
{
    if(sdl2DisplayHeadless)
        return 0;

    char *buffer;

    buffer = (char*)malloc(size + 1);
    memcpy(buffer, lblIndex, size);
    buffer[size] = 0;

    SDL_SetWindowTitle(window, buffer);

    free(buffer);
    return 0;
}

sqInt
ioGetWindowWidth(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    int width = 0;
    int height = 0;
    if(windowRenderer)
        SDL_GetRendererOutputSize(windowRenderer, &width, &height);
    return width;
}

sqInt
ioGetWindowHeight(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    int width = 0;
    int height = 0;
    if(windowRenderer)
        SDL_GetRendererOutputSize(windowRenderer, &width, &height);
    return height;
}

sqInt
ioSetWindowWidthHeight(sqInt w, sqInt h)
{
    if(sdl2DisplayHeadless)
        return 0;

    if(window)
        SDL_SetWindowSize(window, w, h);
    return 0;
}

sqInt
ioIsWindowObscured(void)
{
    if(sdl2DisplayHeadless)
        return 0;

	return false;
}

sqInt
ioGetNextEvent(sqInputEvent *evt)
{
    if(sdl2DisplayHeadless)
        return 0;

    if(vm_event_queue_is_empty(&vmEventQueue))
    {
        evt->type = EventTypeNone;
    }
    else
    {
        vm_event_queue_pop_into(&vmEventQueue, (VMEventUnion*)evt);
    }

    return 0;
}

sqInt
ioGetButtonState(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    ioProcessEvents();
    return buttonState | (modifiersState << 3);
}

sqInt
ioGetKeystroke(void)
{
    if(sdl2DisplayHeadless)
        return 0;

	UNIMPLEMENTED
	return 0;
}

sqInt
ioMousePoint(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    ioProcessEvents();
    return (mousePositionX<<16) | mousePositionY;
}

sqInt
ioPeekKeystroke(void)
{
    if(sdl2DisplayHeadless)
        return 0;

	UNIMPLEMENTED
	return 0;
}

sqInt
ioProcessEvents(void)
{
    aioPoll(0);
    if(!sdl2DisplayHeadless)
    {
        handleEvents();
    }

    return 0;
}

double
ioScreenScaleFactor(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    return 1.0;
}

sqInt
ioScreenSize(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    int width;
    int height;
    if(!windowRenderer)
        return getSavedWindowSize();

    SDL_GetRendererOutputSize(windowRenderer, &width, &height);
    return height | (width << 16);
}

sqInt
ioScreenDepth(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    return 32;
}

sqInt clipboardSize(void)
{
    if(sdl2DisplayHeadless)
        return 0;

    if(!SDL_HasClipboardText())
        return 0;

    return strlen(SDL_GetClipboardText());
}

sqInt
clipboardReadIntoAt(sqInt count, sqInt byteArrayIndex, sqInt startIndex)
{
    sqInt clipSize;
    char *clipboardText;

    if(sdl2DisplayHeadless)
        return 0;

    clipboardText = SDL_GetClipboardText();
    if(!clipboardText)
        clipboardText = "";

    clipSize = count;
    if(count < clipSize)
        clipSize = count;

    memcpy(pointerForOop(byteArrayIndex + startIndex), (void *)clipboardText, clipSize);
    return clipSize;
}

sqInt
clipboardWriteFromAt(sqInt count, sqInt byteArrayIndex, sqInt startIndex)
{
    char *buffer;

    if(sdl2DisplayHeadless)
        return 0;

    buffer = (char*)malloc(count + 1);
    memcpy(buffer, pointerForOop(byteArrayIndex + startIndex), count);
    buffer[count] = 0;

    SDL_SetClipboardText(buffer);

    free(buffer);
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
    if(sdl2DisplayHeadless)
        return 0;

	UNIMPLEMENTED
    return 0;
}

sqInt
dropRequestFileHandle(sqInt dropIndex)
{
    if(sdl2DisplayHeadless)
        return 0;

	UNIMPLEMENTED
    return nilObject();
}

sqInt ioSetInputSemaphore(sqInt semaIndex)
{
    if(sdl2DisplayHeadless)
        return 1;

    if (semaIndex == 0)
        success(false);
    else
        sdl2InputEventSemaIndex = semaIndex;
    return true;
}

void ioSignalInputEvent(void)
{
    if (sdl2InputEventSemaIndex > 0)
        signalSemaphoreWithIndex(sdl2InputEventSemaIndex);
}
#endif
