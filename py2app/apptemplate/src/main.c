#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>

/*
    Typedefs
*/

typedef int PyObject;
typedef void (*Py_SetProgramNamePtr)(const char *);
typedef void (*Py_InitializePtr)(void); 
typedef int (*PyRun_SimpleFilePtr)(FILE *, const char *);
typedef void (*Py_FinalizePtr)(void);
typedef PyObject *(*PySys_GetObjectPtr)(const char *);
typedef int *(*PySys_SetArgvPtr)(int argc, char **argv);
typedef PyObject *(*PyObject_GetAttrStringPtr)(PyObject *, const char *);

typedef CFTypeRef id;
typedef const char * SEL;
typedef signed char BOOL;
#define NSAlertAlternateReturn 0

/*
    Forward declarations
*/
static int report_error(const char *);
static CFTypeRef py2app_getKey(const char *key);

/*
    Strings
*/
static const char *ERR_REALLYBADTITLE = "The application could not be launched.";
static const char *ERR_TITLEFORMAT = "%@ has encountered a fatal error, and will now terminate.";
static const char *ERR_NONAME = "The Info.plist file must have values for the CFBundleName or CFBundleExecutable strings.";
static const char *ERR_PYRUNTIMELOCATIONS = "The Info.plist file must have a PyRuntimeLocations array containing string values for preferred Python runtime locations.  These strings should be \"otool -L\" style mach ids; \"@executable_stub\" and \"~\" prefixes will be translated accordingly.";
static const char *ERR_NOPYTHONRUNTIME = "A Python runtime not could be located.  You may need to install a framework build of Python, or edit the PyRuntimeLocations array in this application's Info.plist file.";
static const char *ERR_NOPYTHONSCRIPT = "A main script could not be located in the Resources folder.;";
static const char *ERR_LINKERRFMT = "An internal error occurred while attempting to link with:\r\r%s\r\rSee the Console for a detailed dyld error message";
static const char *ERR_PYTHONEXCEPTION = "An uncaught exception was raised during execution of the main script.\r\rThis may mean that an unexpected error has occurred, or that you do not have all of the dependencies for this application.\r\rSee the Console for a detailed traceback.";
static const char *ERR_COLONPATH = "Python applications can not currently run from paths containing a '/' (or ':' from the Terminal).";
static const char *ERR_DEFAULTURLTITLE = "Visit Website";
static const char *ERR_CONSOLEAPP = "Console.app";
static const char *ERR_CONSOLEAPPTITLE = "Open Console";
static const char *ERR_TERMINATE = "Terminate";

/*
    Globals
*/
static CFMutableArrayRef py2app_pool;

#define USES(NAME) static __typeof__(&NAME) py2app_ ## NAME
/* ApplicationServices */
USES(LSOpenFSRef);
USES(LSFindApplicationForInfo);
USES(GetCurrentProcess);
USES(SetFrontProcess);
/* CoreFoundation */
USES(CFArrayRemoveValueAtIndex);
USES(CFStringCreateFromExternalRepresentation);
USES(CFStringAppendCString);
USES(CFStringCreateMutable);
USES(kCFTypeArrayCallBacks);
USES(CFArrayCreateMutable);
USES(CFRetain);
USES(CFRelease);
USES(CFBundleGetMainBundle);
USES(CFBundleGetValueForInfoDictionaryKey);
USES(CFArrayGetCount);
USES(CFStringCreateWithCString);
USES(CFArrayGetValueAtIndex);
USES(CFArrayAppendValue);
USES(CFStringFind);
USES(CFBundleCopyPrivateFrameworksURL);
USES(CFURLCreateWithFileSystemPathRelativeToBase);
USES(CFStringCreateWithSubstring);
USES(CFStringGetLength);
USES(CFURLGetFileSystemRepresentation);
USES(CFURLCreateWithFileSystemPath);
USES(CFShow);
USES(CFBundleCopyResourcesDirectoryURL);
USES(CFURLCreateFromFileSystemRepresentation);
USES(CFURLCreateFromFileSystemRepresentationRelativeToBase);
USES(CFStringGetCharacterAtIndex);
USES(CFURLCreateWithString);
USES(CFStringGetCString);
USES(CFStringCreateByCombiningStrings);
USES(CFDictionaryGetValue);
USES(CFBooleanGetValue);
USES(CFStringCreateArrayBySeparatingStrings);
USES(CFArrayAppendArray);
USES(CFStringCreateByCombiningStrings);
USES(CFStringCreateWithFormat);
USES(CFBundleCopyResourceURL);
USES(CFBundleCopyAuxiliaryExecutableURL);
USES(CFURLCreateCopyDeletingLastPathComponent);
USES(CFURLCreateCopyAppendingPathComponent);
USES(CFURLCopyLastPathComponent);
USES(CFStringGetMaximumSizeForEncoding);
#undef USES

/*
    objc
*/

static id (*py2app_objc_getClass)(const char *name);
static SEL (*py2app_sel_getUid)(const char *str);
static id (*py2app_objc_msgSend)(id self, SEL op, ...);

/*
    Cocoa
*/
static void (*py2app_NSLog)(CFStringRef format, ...);
static BOOL (*py2app_NSApplicationLoad)(void);
static int (*py2app_NSRunAlertPanel)(CFStringRef title, CFStringRef msg, CFStringRef defaultButton, CFStringRef alternateButton, CFStringRef otherButton, ...);

/*
    Functions
*/

static int bind_objc_Cocoa_ApplicationServices(void) {
    static Boolean bound = false;
    if (bound) return 0;
    bound = true;
    void* cf_dylib;
    cf_dylib = dlopen("/usr/lib/libobjc.dylib", RTLD_LAZY);
    if (!cf_dylib) return -1;

#define LOOKUP(NAME) do { \
    py2app_ ## NAME = (__typeof__(py2app_ ## NAME))dlsym( \
        cf_dylib, #NAME); \
        if (!py2app_ ## NAME) return -1; \
    } while (0)

    LOOKUP(objc_getClass);
    LOOKUP(sel_getUid);
    LOOKUP(objc_msgSend);

    cf_dylib = dlopen(
        "/System/Library/Frameworks/Cocoa.framework/Cocoa",
        RTLD_LAZY);
    if (!cf_dylib) return -1;
    LOOKUP(NSLog);
    LOOKUP(NSApplicationLoad);
    LOOKUP(NSRunAlertPanel);

    cf_dylib = dlopen(
        "/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices",
        RTLD_LAZY);
    if (!cf_dylib) return -1;

    LOOKUP(GetCurrentProcess);
    LOOKUP(SetFrontProcess);
    LOOKUP(LSOpenFSRef);
    LOOKUP(LSFindApplicationForInfo);
#undef LOOKUP
    return 0;
}
    
static int bind_CoreFoundation(void) {
    static Boolean bound = false;
    void *cf_dylib;
    if (bound) return 0;
    bound = true;
    cf_dylib = dlopen(
        "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",
        RTLD_LAZY);
    if (!cf_dylib) return -1;

#define LOOKUP(NAME) do { \
    py2app_ ## NAME = (__typeof__(py2app_ ## NAME))dlsym( \
        cf_dylib, #NAME); \
        if (!py2app_ ## NAME) return -1; \
    } while (0)

    LOOKUP(CFArrayRemoveValueAtIndex);
    LOOKUP(CFStringCreateFromExternalRepresentation);
    LOOKUP(CFStringAppendCString);
    LOOKUP(CFStringCreateMutable);
    LOOKUP(kCFTypeArrayCallBacks);
    LOOKUP(CFArrayCreateMutable);
    LOOKUP(CFRetain);
    LOOKUP(CFRelease);
    LOOKUP(CFBundleGetMainBundle);
    LOOKUP(CFBundleGetValueForInfoDictionaryKey);
    LOOKUP(CFArrayGetCount);
    LOOKUP(CFStringCreateWithCString);
    LOOKUP(CFArrayGetValueAtIndex);
    LOOKUP(CFArrayAppendValue);
    LOOKUP(CFStringFind);
    LOOKUP(CFBundleCopyPrivateFrameworksURL);
    LOOKUP(CFURLCreateWithFileSystemPathRelativeToBase);
    LOOKUP(CFStringCreateWithSubstring);
    LOOKUP(CFStringGetLength);
    LOOKUP(CFURLGetFileSystemRepresentation);
    LOOKUP(CFURLCreateWithFileSystemPath);
    LOOKUP(CFShow);
    LOOKUP(CFBundleCopyResourcesDirectoryURL);
    LOOKUP(CFURLCreateFromFileSystemRepresentation);
    LOOKUP(CFURLCreateFromFileSystemRepresentationRelativeToBase);
    LOOKUP(CFStringGetCharacterAtIndex);
    LOOKUP(CFURLCreateWithString);
    LOOKUP(CFStringGetCString);
    LOOKUP(CFStringCreateByCombiningStrings);
    LOOKUP(CFDictionaryGetValue);
    LOOKUP(CFBooleanGetValue);
    LOOKUP(CFStringCreateArrayBySeparatingStrings);
    LOOKUP(CFArrayAppendArray);
    LOOKUP(CFStringCreateByCombiningStrings);
    LOOKUP(CFStringCreateWithFormat);
    LOOKUP(CFBundleCopyResourceURL);
    LOOKUP(CFBundleCopyAuxiliaryExecutableURL);
    LOOKUP(CFURLCreateCopyDeletingLastPathComponent);
    LOOKUP(CFURLCreateCopyAppendingPathComponent);
    LOOKUP(CFURLCopyLastPathComponent);
    LOOKUP(CFStringGetMaximumSizeForEncoding);

#undef LOOKUP

    return 0;
}

#define AUTORELEASE(obj) ((obj == NULL) ? NULL : ( \
    py2app_CFArrayAppendValue(py2app_pool, (const void *)obj), \
    py2app_CFRelease(obj), \
    obj))
    
#define py2app_CFSTR(s) AUTORELEASE( \
    py2app_CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8))

static int py2app_openConsole(void) {
    OSStatus err;
    FSRef consoleRef;
    err = py2app_LSFindApplicationForInfo(
        kLSUnknownCreator,
        NULL,
        py2app_CFSTR(ERR_CONSOLEAPP),
        &consoleRef,
        NULL);
    if (err != noErr) return err;
    return py2app_LSOpenFSRef((const FSRef *)&consoleRef, NULL);
}

static CFTypeRef py2app_getKey(const char *key) {
    CFTypeRef rval;
    CFStringRef cfKey = py2app_CFStringCreateWithCString(NULL,
        key, kCFStringEncodingUTF8);
    if (!cfKey) return NULL;
    rval = py2app_CFBundleGetValueForInfoDictionaryKey(
        py2app_CFBundleGetMainBundle(),
        cfKey);
    py2app_CFRelease(cfKey);
    return rval;
}

static CFStringRef py2app_getApplicationName(void) {
    static CFStringRef name = NULL;
    if (name) return name;
    name = (CFStringRef)py2app_getKey("CFBundleName");
    if (!name) name = (CFStringRef)py2app_getKey("CFBundleExecutable");
    if (!name) name = py2app_CFSTR("py2app stub executable");
    return AUTORELEASE(name);
}


static CFStringRef py2app_getErrorTitle(CFStringRef applicationName) {
    CFStringRef res;
    if (!applicationName) return py2app_CFSTR(ERR_REALLYBADTITLE);
    res = py2app_CFStringCreateWithFormat(
        NULL, NULL, py2app_CFSTR(ERR_TITLEFORMAT), applicationName);
    AUTORELEASE(res);
    return res;
}

static void ensureGUI(void) {
    ProcessSerialNumber psn;
    id app = ((id(*)(id, SEL))py2app_objc_msgSend)(py2app_objc_getClass("NSApplication"), py2app_sel_getUid("sharedApplication"));
    py2app_NSApplicationLoad();
    ((void(*)(id, SEL, BOOL))py2app_objc_msgSend)(app, py2app_sel_getUid("activateIgnoringOtherApps:"), 1);
    if (py2app_GetCurrentProcess(&psn) == noErr) {
        py2app_SetFrontProcess(&psn);
    }
}

static int report_error(const char *error) {
    int choice;
    id releasePool;

    if (bind_objc_Cocoa_ApplicationServices()) {
        fprintf(stderr, "%s\n", error);
        return -1;
    }
    releasePool = ((id(*)(id, SEL))py2app_objc_msgSend)(
		    ((id(*)(id, SEL))py2app_objc_msgSend)(
			    py2app_objc_getClass("NSAutoreleasePool"), 
			    py2app_sel_getUid("alloc")), 
		    py2app_sel_getUid("init"));
    py2app_NSLog(py2app_CFSTR("%@"), py2app_CFSTR(error));

    printf("%p\n", py2app_NSApplicationLoad);

    if (!py2app_NSApplicationLoad()) {
        py2app_NSLog(py2app_CFSTR("NSApplicationLoad() failed"));
    } else {
        ensureGUI();
        choice = py2app_NSRunAlertPanel(
            py2app_getErrorTitle(py2app_getApplicationName()),
            py2app_CFSTR("%@"),
            py2app_CFSTR(ERR_TERMINATE),
            py2app_CFSTR(ERR_CONSOLEAPPTITLE),
            NULL,
            py2app_CFSTR(error));
        if (choice == NSAlertAlternateReturn) py2app_openConsole();
    }
    ((void(*)(id, SEL))py2app_objc_msgSend)(releasePool, py2app_sel_getUid("release"));
    return -1;
}

static CFStringRef pathFromURL(CFURLRef anURL) {
    UInt8 buf[PATH_MAX];
    py2app_CFURLGetFileSystemRepresentation(anURL, true, buf, sizeof(buf));
    return py2app_CFStringCreateWithCString(NULL, (char *)buf, kCFStringEncodingUTF8);
}

static CFStringRef pyStandardizePath(CFStringRef pyLocation) {
    CFRange foundRange;
    CFURLRef fmwkURL;
    CFURLRef locURL;
    CFStringRef subpath;
    static CFStringRef prefix = NULL;
    if (!prefix) prefix = py2app_CFSTR("@executable_path/");
    foundRange = py2app_CFStringFind(pyLocation, prefix, 0);
    if (foundRange.location == kCFNotFound || foundRange.length == 0) {
        return NULL;
    }
    fmwkURL = py2app_CFBundleCopyPrivateFrameworksURL(py2app_CFBundleGetMainBundle());
    foundRange.location = foundRange.length;
    foundRange.length = py2app_CFStringGetLength(pyLocation) - foundRange.length;
    subpath = py2app_CFStringCreateWithSubstring(NULL, pyLocation, foundRange);
    locURL = py2app_CFURLCreateWithFileSystemPathRelativeToBase(
        NULL,
        subpath,
        kCFURLPOSIXPathStyle,
        false,
        fmwkURL);
    py2app_CFRelease(subpath);
    py2app_CFRelease(fmwkURL);
    subpath = pathFromURL(locURL);
    py2app_CFRelease(locURL);
    return subpath;
}

static Boolean doesPathExist(CFStringRef path) {
    struct stat st;
    CFURLRef locURL;
    UInt8 buf[PATH_MAX];
    locURL = py2app_CFURLCreateWithFileSystemPath(
        NULL, path, kCFURLPOSIXPathStyle, false);
    py2app_CFURLGetFileSystemRepresentation(locURL, true, buf, sizeof(buf));
    py2app_CFRelease(locURL);
    return (stat((const char *)buf, &st) == -1 ? false : true);
}

static CFStringRef py2app_findPyLocation(CFArrayRef pyLocations) {
    CFIndex i;
    CFIndex cnt = py2app_CFArrayGetCount(pyLocations);
    for (i = 0; i < cnt; i++) {
        CFStringRef newLoc;
        CFStringRef pyLocation = py2app_CFArrayGetValueAtIndex(pyLocations, i);
        newLoc = pyStandardizePath(pyLocation);
        if (!newLoc) {
		newLoc = pyLocation;
		py2app_CFRetain(newLoc);
	}
        if (doesPathExist(newLoc)) {
            return newLoc;
        }
        if (newLoc) py2app_CFRelease(newLoc);
    }
    return NULL;
}

static CFStringRef tildeExpand(CFStringRef path) {
    CFURLRef pathURL;
    char buf[PATH_MAX];
    CFURLRef fullPathURL;
    struct passwd *pwnam;
    char tmp;
    char *dir = NULL;

    
    py2app_CFStringGetCString(path, buf, sizeof(buf), kCFStringEncodingUTF8);

    int i;
    if (buf[0] != '~') {
        return py2app_CFStringCreateWithCString(
            NULL, buf, kCFStringEncodingUTF8);
    }
    /* user in path */
    i = 1;
    while (buf[i] != '\0' && buf[i] != '/') {
        i++;
    }
    if (i == 1) {
        dir = getenv("HOME");
    } else {
        tmp = buf[i];
        buf[i] = '\0';
        pwnam = getpwnam((const char *)&buf[1]);
        if (pwnam) dir = pwnam->pw_dir;
        buf[i] = tmp;
    }
    if (!dir) {
        return py2app_CFStringCreateWithCString(NULL, buf, kCFStringEncodingUTF8);
    }
    pathURL = py2app_CFURLCreateFromFileSystemRepresentation(
        NULL, (const UInt8*)dir, strlen(dir), false);
    fullPathURL = py2app_CFURLCreateFromFileSystemRepresentationRelativeToBase(
        NULL, (const UInt8*)&buf[i + 1], strlen(&buf[i + 1]), false, pathURL);
    py2app_CFRelease(pathURL);
    path = pathFromURL(fullPathURL);
    py2app_CFRelease(fullPathURL);
    return path;
}

static void setcfenv(char *name, CFStringRef value) {
    char buf[PATH_MAX];
    py2app_CFStringGetCString(value, buf, sizeof(buf), kCFStringEncodingUTF8);
    setenv(name, buf, 1);
}

static void py2app_setPythonPath(void) {
    CFMutableArrayRef paths;
    CFURLRef resDir;
    CFStringRef resPath;
    CFArrayRef resPackages;
    CFDictionaryRef options;

    paths = py2app_CFArrayCreateMutable(NULL, 0, py2app_kCFTypeArrayCallBacks);

    resDir = py2app_CFBundleCopyResourcesDirectoryURL(py2app_CFBundleGetMainBundle());
    resPath = pathFromURL(resDir);
    py2app_CFArrayAppendValue(paths, resPath);
    py2app_CFRelease(resPath);

    resPackages = py2app_getKey("PyResourcePackages");
    if (resPackages) {
        int i;
        int cnt = py2app_CFArrayGetCount(resPackages);
        for (i = 0; i < cnt; i++) {
            resPath = tildeExpand(py2app_CFArrayGetValueAtIndex(resPackages, i));
            if (py2app_CFStringGetLength(resPath)) {
                if (py2app_CFStringGetCharacterAtIndex(resPath, 0) != '/') {
                    CFURLRef absURL = py2app_CFURLCreateWithString(
                        NULL, resPath, resDir);
                    py2app_CFRelease(resPath);
                    resPath = pathFromURL(absURL);
                    py2app_CFRelease(absURL);
                }
                py2app_CFArrayAppendValue(paths, resPath);
            }
            py2app_CFRelease(resPath);
        }
    }

    py2app_CFRelease(resDir);

    options = py2app_getKey("PyOptions");
    if (options) {
        CFBooleanRef use_pythonpath;
        use_pythonpath = py2app_CFDictionaryGetValue(
            options, py2app_CFSTR("use_pythonpath"));
        if (use_pythonpath && py2app_CFBooleanGetValue(use_pythonpath)) {
            char *ppath = getenv("PYTHONPATH");
            if (ppath) {
                CFArrayRef oldPath;
                oldPath = py2app_CFStringCreateArrayBySeparatingStrings(
                    NULL, py2app_CFSTR(ppath), py2app_CFSTR(":"));
                if (oldPath) {
                    CFRange rng;
                    rng.location = 0;
                    rng.length = py2app_CFArrayGetCount(oldPath);
                    py2app_CFArrayAppendArray(paths, oldPath, rng);
                    py2app_CFRelease(oldPath);
                }
            }
        }
    }

    if (py2app_CFArrayGetCount(paths)) {
        resPath = py2app_CFStringCreateByCombiningStrings(NULL, paths, py2app_CFSTR(":"));
        setcfenv("PYTHONPATH", resPath);
        py2app_CFRelease(resPath);
    }
    py2app_CFRelease(paths);
}



static void setResourcePath(void) {
    CFURLRef resDir;
    CFStringRef resPath;
    resDir = py2app_CFBundleCopyResourcesDirectoryURL(py2app_CFBundleGetMainBundle());
    resPath = pathFromURL(resDir);
    py2app_CFRelease(resDir);
    setcfenv("RESOURCEPATH", resPath);
    py2app_CFRelease(resPath);
}

static void setExecutablePath(void) {
    char executable_path[PATH_MAX];
    uint32_t bufsize = PATH_MAX;
    if (!_NSGetExecutablePath(executable_path, &bufsize)) {
        executable_path[bufsize] = '\0';
        setenv("EXECUTABLEPATH", executable_path, 1);
    }
}

static CFStringRef getMainScript(void) {
    CFMutableArrayRef possibleMains;
    CFBundleRef bndl;
    CFStringRef e_py, e_pyc, e_pyo, path;
    int i, cnt;
    possibleMains = py2app_CFArrayCreateMutable(NULL, 0, py2app_kCFTypeArrayCallBacks);
    CFArrayRef firstMains = py2app_getKey("PyMainFileNames");
    if (firstMains) {
        CFRange rng;
        rng.location = 0;
        rng.length = py2app_CFArrayGetCount(firstMains);
        py2app_CFArrayAppendArray(possibleMains, firstMains, rng);
    }
    py2app_CFArrayAppendValue(possibleMains, py2app_CFSTR("__main__"));
    py2app_CFArrayAppendValue(possibleMains, py2app_CFSTR("__realmain__"));
    py2app_CFArrayAppendValue(possibleMains, py2app_CFSTR("Main"));

    e_py = py2app_CFSTR("py");
    e_pyc = py2app_CFSTR("pyc");
    e_pyo = py2app_CFSTR("pyo");

    cnt = py2app_CFArrayGetCount(possibleMains);
    bndl = py2app_CFBundleGetMainBundle();
    path = NULL;
    for (i = 0; i < cnt; i++) {
        CFStringRef base;
        CFURLRef resURL;
        base = py2app_CFArrayGetValueAtIndex(possibleMains, i);
        resURL = py2app_CFBundleCopyResourceURL(bndl, base, e_py, NULL);
        if (resURL == NULL) {
            resURL = py2app_CFBundleCopyResourceURL(bndl, base, e_pyc, NULL);
        }
        if (resURL == NULL) {
            resURL = py2app_CFBundleCopyResourceURL(bndl, base, e_pyo, NULL);
        }
        if (resURL != NULL) {
            path = pathFromURL(resURL);
            py2app_CFRelease(resURL);
            break;
        }
    }
    py2app_CFRelease(possibleMains);
    return path;
}

static int report_linkEdit_error(void) {
    CFStringRef errString;
    const char *errorString;
    char* buf;
    errorString = dlerror();
    fputs(errorString, stderr);
    errString = py2app_CFStringCreateWithFormat(
        NULL, NULL, py2app_CFSTR(ERR_LINKERRFMT), errorString);
    buf = alloca(py2app_CFStringGetMaximumSizeForEncoding(
            py2app_CFStringGetLength(errString), kCFStringEncodingUTF8));
    py2app_CFStringGetCString(errString, buf, sizeof(buf), kCFStringEncodingUTF8);
    py2app_CFRelease(errString);
    return report_error(buf);
}

static CFStringRef getPythonInterpreter(CFStringRef pyLocation) {
    CFBundleRef bndl;
    CFStringRef auxName;
    CFURLRef auxURL;
    CFStringRef path;

    auxName = py2app_getKey("PyExecutableName");
    if (!auxName) auxName = py2app_CFSTR("python");
    bndl = py2app_CFBundleGetMainBundle();
    auxURL = py2app_CFBundleCopyAuxiliaryExecutableURL(bndl, auxName);
    if (auxURL) {
        path = pathFromURL(auxURL);
        py2app_CFRelease(auxURL);
        return path;
    }
    return NULL;
}

static CFStringRef getErrorScript(void) {
    CFMutableArrayRef errorScripts;
    CFBundleRef bndl;
    CFStringRef path;
    int i, cnt;
    errorScripts = py2app_CFArrayCreateMutable(NULL, 0, py2app_kCFTypeArrayCallBacks);
    CFArrayRef firstErrorScripts = py2app_getKey("PyErrorScripts");
    if (firstErrorScripts) {
        CFRange rng;
        rng.location = 0;
        rng.length = py2app_CFArrayGetCount(firstErrorScripts);
        py2app_CFArrayAppendArray(errorScripts, firstErrorScripts, rng);
    }
    py2app_CFArrayAppendValue(errorScripts, py2app_CFSTR("__error__"));
    py2app_CFArrayAppendValue(errorScripts, py2app_CFSTR("__error__.py"));
    py2app_CFArrayAppendValue(errorScripts, py2app_CFSTR("__error__.pyc"));
    py2app_CFArrayAppendValue(errorScripts, py2app_CFSTR("__error__.pyo"));
    py2app_CFArrayAppendValue(errorScripts, py2app_CFSTR("__error__.sh"));

    cnt = py2app_CFArrayGetCount(errorScripts);
    bndl = py2app_CFBundleGetMainBundle();
    path = NULL;
    for (i = 0; i < cnt; i++) {
        CFStringRef base;
        CFURLRef resURL;
        base = py2app_CFArrayGetValueAtIndex(errorScripts, i);
        resURL = py2app_CFBundleCopyResourceURL(bndl, base, NULL, NULL);
        if (resURL) {
            path = pathFromURL(resURL);
            py2app_CFRelease(resURL);
            break;
        }
    }
    py2app_CFRelease(errorScripts);
    return path;
 
}

static CFMutableArrayRef get_trimmed_lines(CFStringRef output) {
    CFMutableArrayRef lines;
    CFArrayRef tmp;
    CFRange rng;
    lines = py2app_CFArrayCreateMutable(NULL, 0, py2app_kCFTypeArrayCallBacks);
    tmp = py2app_CFStringCreateArrayBySeparatingStrings(
        NULL, output, py2app_CFSTR("\n"));
    rng.location = 0;
    rng.length = py2app_CFArrayGetCount(tmp);
    py2app_CFArrayAppendArray(lines, tmp, rng);
    while (true) {
        CFIndex cnt = py2app_CFArrayGetCount(lines);
        CFStringRef last;
        /* Nothing on stdout means pass silently */
        if (cnt <= 0) {
            py2app_CFRelease(lines);
            return NULL;
        }
        last = py2app_CFArrayGetValueAtIndex(lines, cnt - 1);
        if (py2app_CFStringGetLength(last) > 0) break;
        py2app_CFArrayRemoveValueAtIndex(lines, cnt - 1);
    }
    return lines;
}

static int report_script_error(const char *msg) {
    CFStringRef errorScript;
    CFMutableArrayRef lines;
    CFRange foundRange;
    CFStringRef lastLine;
    CFStringRef output = NULL;
    CFIndex lineCount;
    CFURLRef buttonURL = NULL;
    CFStringRef buttonString = NULL;
    CFStringRef title = NULL;
    CFStringRef errmsg = NULL;
    id releasePool;
    int errBinding;
    int status = 0;

    errorScript = getErrorScript();
    if (!errorScript) return report_error(msg);

    errBinding = bind_objc_Cocoa_ApplicationServices();
    if (!errBinding) {
        id task, stdoutPipe, taskData;
        CFMutableArrayRef argv;
        releasePool = ((id(*)(id, SEL))py2app_objc_msgSend)(
		    ((id(*)(id, SEL))py2app_objc_msgSend)(
			    py2app_objc_getClass("NSAutoreleasePool"), 
			    py2app_sel_getUid("alloc")), 
		    py2app_sel_getUid("init"));
        task = ((id(*)(id, SEL))py2app_objc_msgSend)(
		    ((id(*)(id, SEL))py2app_objc_msgSend)(
			    py2app_objc_getClass("NSTask"), 
			    py2app_sel_getUid("alloc")), 
		    py2app_sel_getUid("init"));
        stdoutPipe = ((id(*)(id, SEL))py2app_objc_msgSend)(py2app_objc_getClass("NSPipe"), py2app_sel_getUid("pipe"));
        ((void(*)(id, SEL, id))py2app_objc_msgSend)(task, py2app_sel_getUid("setLaunchPath:"), py2app_CFSTR("/bin/sh"));
        ((void(*)(id, SEL, id))py2app_objc_msgSend)(task, py2app_sel_getUid("setStandardOutput:"), stdoutPipe);
        argv = py2app_CFArrayCreateMutable(NULL, 0, py2app_kCFTypeArrayCallBacks);
        py2app_CFArrayAppendValue(argv, errorScript);
        py2app_CFArrayAppendValue(argv, py2app_getApplicationName());
        ((void(*)(id, SEL, id))py2app_objc_msgSend)(task, py2app_sel_getUid("setArguments:"), argv);
        /* This could throw, in theory, but /bin/sh should prevent that */
        ((void(*)(id, SEL))py2app_objc_msgSend)(task, py2app_sel_getUid("launch"));
        ((void(*)(id, SEL))py2app_objc_msgSend)(task, py2app_sel_getUid("waitUntilExit"));
        taskData = ((id(*)(id, SEL))py2app_objc_msgSend)(
            ((id(*)(id, SEL))py2app_objc_msgSend)(stdoutPipe, py2app_sel_getUid("fileHandleForReading")),
            py2app_sel_getUid("readDataToEndOfFile"));
        py2app_CFRelease(argv);

        status = ((int(*)(id, SEL))py2app_objc_msgSend)(task, py2app_sel_getUid("terminationStatus"));
        py2app_CFRelease(task);
        if (!status && taskData) {
            output = py2app_CFStringCreateFromExternalRepresentation(
                NULL, taskData, kCFStringEncodingUTF8);
        }

        ((void(*)(id, SEL))py2app_objc_msgSend)(releasePool, py2app_sel_getUid("release"));
    }

    py2app_CFRelease(errorScript);
    if (status || !output) return report_error(msg);

    lines = get_trimmed_lines(output);
    py2app_CFRelease(output);
    /* Nothing on stdout means pass silently */
    if (!lines) return -1;
    lineCount = py2app_CFArrayGetCount(lines);
    lastLine = py2app_CFArrayGetValueAtIndex(lines, lineCount - 1);
    foundRange = py2app_CFStringFind(lastLine, py2app_CFSTR("ERRORURL: "), 0);
    if (foundRange.location != kCFNotFound && foundRange.length != 0) {
        CFMutableArrayRef buttonArr;
        CFArrayRef tmp;
        CFRange rng;
        buttonArr = py2app_CFArrayCreateMutable(NULL, 0, py2app_kCFTypeArrayCallBacks);
        tmp = py2app_CFStringCreateArrayBySeparatingStrings(
            NULL, lastLine, py2app_CFSTR(" "));
        lineCount -= 1;
        py2app_CFArrayRemoveValueAtIndex(lines, lineCount);
        rng.location = 1;
        rng.length = py2app_CFArrayGetCount(tmp) - 1;
        py2app_CFArrayAppendArray(buttonArr, tmp, rng);
        py2app_CFRelease(tmp);
        while (true) {
            CFStringRef tmpstr;
            if (py2app_CFArrayGetCount(buttonArr) <= 0) break;
            tmpstr = py2app_CFArrayGetValueAtIndex(buttonArr, 0);
            if (py2app_CFStringGetLength(tmpstr) == 0) {
                py2app_CFArrayRemoveValueAtIndex(buttonArr, 0);
            } else {
                break;
            }
        }

        buttonURL = py2app_CFURLCreateWithString(
            NULL, py2app_CFArrayGetValueAtIndex(buttonArr, 0), NULL);
        if (buttonURL) {
            py2app_CFArrayRemoveValueAtIndex(buttonArr, 0);
            while (true) {
                CFStringRef tmpstr;
                if (py2app_CFArrayGetCount(buttonArr) <= 0) break;
                tmpstr = py2app_CFArrayGetValueAtIndex(buttonArr, 0);
                if (py2app_CFStringGetLength(tmpstr) == 0) {
                    py2app_CFArrayRemoveValueAtIndex(buttonArr, 0);
                } else {
                    break;
                }
            }
            if (py2app_CFArrayGetCount(buttonArr) > 0) {
                buttonString = py2app_CFStringCreateByCombiningStrings(
                    NULL, buttonArr, py2app_CFSTR(" "));
            }
            if (!buttonString) buttonString = py2app_CFSTR(ERR_DEFAULTURLTITLE);
        }
        py2app_CFRelease(buttonArr);
        
    }
    if (lineCount <= 0 || errBinding) {
        py2app_CFRelease(lines);
        return report_error(msg);
    }

    releasePool = ((id(*)(id, SEL))py2app_objc_msgSend)(
		    ((id(*)(id, SEL))py2app_objc_msgSend)(
			    py2app_objc_getClass("NSAutoreleasePool"), 
			    py2app_sel_getUid("alloc")), 
		    py2app_sel_getUid("init"));

    title = py2app_CFArrayGetValueAtIndex(lines, 0);
    py2app_CFRetain(title);
    AUTORELEASE(title);
    lineCount -= 1;
    py2app_CFArrayRemoveValueAtIndex(lines, lineCount);
    py2app_NSLog(py2app_CFSTR("%@"), title);
    if (lineCount > 0) {
        CFStringRef showerr;
        errmsg = py2app_CFStringCreateByCombiningStrings(
            NULL, lines, py2app_CFSTR("\r"));
        AUTORELEASE(errmsg);
        showerr = ((id(*)(id, SEL, id))py2app_objc_msgSend)(
            ((id(*)(id, SEL, id))py2app_objc_msgSend)(errmsg, py2app_sel_getUid("componentsSeparatedByString:"), py2app_CFSTR("\r")),
            py2app_sel_getUid("componentsJoinedByString:"), py2app_CFSTR("\n"));
        py2app_NSLog(py2app_CFSTR("%@"), showerr);
    } else {
        errmsg = py2app_CFSTR("");
    }

    ensureGUI();
    if (!buttonURL) {
        int choice = py2app_NSRunAlertPanel(
            title, py2app_CFSTR("%@"), py2app_CFSTR(ERR_TERMINATE),
            py2app_CFSTR(ERR_CONSOLEAPPTITLE), NULL, errmsg);
        if (choice == NSAlertAlternateReturn) py2app_openConsole();
    } else {
        int choice = py2app_NSRunAlertPanel(
            title, py2app_CFSTR("%@"), py2app_CFSTR(ERR_TERMINATE),
            buttonString, NULL, errmsg);
        if (choice == NSAlertAlternateReturn) {
            id ws = ((id(*)(id, SEL))py2app_objc_msgSend)(py2app_objc_getClass("NSWorkspace"), py2app_sel_getUid("sharedWorkspace"));
            ((void(*)(id, SEL, id))py2app_objc_msgSend)(ws, py2app_sel_getUid("openURL:"), buttonURL);
        }
    }
    ((void(*)(id, SEL))py2app_objc_msgSend)(releasePool, py2app_sel_getUid("release"));
    py2app_CFRelease(lines);
    return -1;
}

static int py2app_main(int argc, char * const *argv, char * const *envp) {
    CFArrayRef pyLocations;
    CFStringRef pyLocation;
    CFStringRef mainScript;
    CFStringRef pythonInterpreter;
    char *resource_path;
    char buf[PATH_MAX];
    char c_pythonInterpreter[PATH_MAX];
    char c_mainScript[PATH_MAX];
    char **argv_new;
    struct stat sb;
    void *py_dylib;
    int rval;
    FILE *mainScriptFile;


    if (!py2app_getApplicationName()) return report_error(ERR_NONAME);
    pyLocations = (CFArrayRef)py2app_getKey("PyRuntimeLocations");
    if (!pyLocations) return report_error(ERR_PYRUNTIMELOCATIONS);
    pyLocation = py2app_findPyLocation(pyLocations);
    if (!pyLocation) return report_error(ERR_NOPYTHONRUNTIME);

    setExecutablePath();
    setResourcePath();
    /* check for ':' in path, not compatible with Python due to Py_GetPath */
    /* XXX: Could work-around by creating something in /tmp I guess */
    resource_path = getenv("RESOURCEPATH");
    if ((resource_path == NULL) || (strchr(resource_path, ':') != NULL)) {
        return report_error(ERR_COLONPATH);
    }
    py2app_setPythonPath();
    setenv("ARGVZERO", argv[0], 1);

    mainScript = getMainScript();
    if (!mainScript) return report_error(ERR_NOPYTHONSCRIPT);

    pythonInterpreter = getPythonInterpreter(pyLocation);
    py2app_CFStringGetCString(
        pythonInterpreter, c_pythonInterpreter,
        sizeof(c_pythonInterpreter), kCFStringEncodingUTF8);
    py2app_CFRelease(pythonInterpreter);
    if (lstat(c_pythonInterpreter, &sb) == 0) {
        if (!((sb.st_mode & S_IFLNK) == S_IFLNK)) {
            setenv("PYTHONHOME", resource_path, 1);
        }
    }

    py2app_CFStringGetCString(pyLocation, buf, sizeof(buf), kCFStringEncodingUTF8);
    py_dylib = dlopen(buf, RTLD_LAZY);
    if (!py_dylib) return report_linkEdit_error();

#define LOOKUP(NAME) \
	    NAME ## Ptr py2app_ ## NAME = (NAME ## Ptr)dlsym(py_dylib, #NAME); \
	    if (!py2app_ ## NAME) { \
		return report_linkEdit_error(); \
	    } 
    
    LOOKUP(Py_SetProgramName);
    LOOKUP(Py_Initialize);
    LOOKUP(PyRun_SimpleFile);
    LOOKUP(Py_Finalize);
    LOOKUP(PySys_GetObject);
    LOOKUP(PySys_SetArgv);
    LOOKUP(PyObject_GetAttrString);

#undef LOOKUP

    py2app_Py_SetProgramName(c_pythonInterpreter);

    py2app_Py_Initialize();

    py2app_CFStringGetCString(
        mainScript, c_mainScript,
        sizeof(c_mainScript), kCFStringEncodingUTF8);
    py2app_CFRelease(mainScript);

    argv_new = alloca((argc + 1) * sizeof(char *));
    argv_new[argc] = NULL;
    argv_new[0] = c_mainScript;
    memcpy(&argv_new[1], &argv[1], (argc - 1) * sizeof(char *));
    py2app_PySys_SetArgv(argc, argv_new);

    mainScriptFile = fopen(c_mainScript, "r");
    rval = py2app_PyRun_SimpleFile(mainScriptFile, c_mainScript);
    fclose(mainScriptFile);
    
    if (rval) {
        rval = report_script_error(ERR_PYTHONEXCEPTION);
    }

    py2app_Py_Finalize();

    return rval;
}

int main(int argc, char * const *argv, char * const *envp)
{
    int rval;
    if (bind_CoreFoundation()) {
        fprintf(stderr, "CoreFoundation not found or functions missing\n");
        return -1;
    }
    if (!py2app_CFBundleGetMainBundle()) {
        fprintf(stderr, "Not bundled, exiting\n");
        return -1;
    }
    py2app_pool = py2app_CFArrayCreateMutable(NULL, 0, py2app_kCFTypeArrayCallBacks);
    if (!py2app_pool) {
        fprintf(stderr, "Couldn't create global pool\n");
        return -1;
    }
    rval = py2app_main(argc, argv, envp);
    py2app_CFRelease(py2app_pool);
    return rval;
}
