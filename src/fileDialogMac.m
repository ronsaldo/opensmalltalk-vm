#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include "pharovm/fileDialog.h"
#include "pharovm/stringUtilities.h"
#include "pharovm/pathUtilities.h"
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

/**
 * Gatekeeper in OS X runs application downloaded from unsigned zips in a
 * randomized read-only DMG. See https://objective-see.com/blog/blog_0x15.html
 * for an explanation of this fix.
 */
static NSURL *untranslocatePath(NSURL *originalPath)
{
	void *handle = NULL;

	//open security framework
	handle = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
	if(!handle)
		return originalPath;

	Boolean (*secTranslocateIsTranslocatedURL)(CFURLRef path, bool *isTranslocated, CFErrorRef * __nullable error);
	secTranslocateIsTranslocatedURL = dlsym(handle, "SecTranslocateIsTranslocatedURL");

	bool isTranslocated = false;
	secTranslocateIsTranslocatedURL((__bridge CFURLRef)originalPath, &isTranslocated, NULL);

	NSURL *untranslocatedPath = nil;
	if(isTranslocated)
	{
		CFURLRef __nullable (*secTranslocateCreateOriginalPathForURL)(CFURLRef translocatedPath, CFErrorRef * __nullable error);
		secTranslocateCreateOriginalPathForURL = dlsym(handle, "SecTranslocateCreateOriginalPathForURL");
		// HACK: Call this function with the full bundle path. It does not seem to work with just the bundle parent folder.
		untranslocatedPath = (__bridge NSURL*)secTranslocateCreateOriginalPathForURL((__bridge CFURLRef)[NSURL fileURLWithPath:[[NSBundle mainBundle] bundlePath]], NULL);
		if(untranslocatedPath != nil)
		{
			untranslocatedPath = [untranslocatedPath URLByDeletingLastPathComponent];
			NSString *pathString = untranslocatedPath.path;
			if(pathString != nil)
			{
				const char *pathUTF8 = [pathString UTF8String];
				if(pathUTF8)
					chdir(pathUTF8);
			}
		}
	}

	dlclose(handle);
	return untranslocatedPath != nil ? untranslocatedPath : originalPath;
}

bool
vm_file_dialog_is_nop(void)
{
	return false;
}

VMErrorCode
vm_file_dialog_run_modal_open(VMFileDialog *dialog)
{
    NSString *allowedExtension = nil;
    if(dialog->filterExtension && dialog->filterExtension[0] == '.')
    {
        allowedExtension = [NSString stringWithUTF8String: dialog->filterExtension + 1];
    }

    // No image file is specified. Display the file open dialog.
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = [NSString stringWithUTF8String: dialog->title];
    panel.message = [NSString stringWithUTF8String: dialog->message];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;

    if(allowedExtension != nil)
    {
        panel.allowedFileTypes = [NSArray arrayWithObjects: allowedExtension, nil];
    }
	
	if(dialog->defaultFileNameAndPath)
	{
		char *defaultDirectory = (char*)calloc(1, FILENAME_MAX+1);
		vm_path_extract_dirname_into(defaultDirectory, FILENAME_MAX+1, dialog->defaultFileNameAndPath);
		panel.directoryURL = untranslocatePath([NSURL fileURLWithPath: [NSString stringWithUTF8String: defaultDirectory]]);
		free(defaultDirectory);
	}

    dialog->succeeded = false;
    dialog->selectedFileName = NULL;

    NSInteger clickedButton = [panel runModal];
    if(clickedButton == NSModalResponseOK)
    {
        for (NSURL *url in [panel URLs])
        {
            if([url isFileURL])
            {
                dialog->succeeded = true;
                dialog->selectedFileName = strdup([url.path UTF8String]);
                break;
            }
        }
    }

    return VM_SUCCESS;
}
