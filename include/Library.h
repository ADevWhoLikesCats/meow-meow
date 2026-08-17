#ifndef LIBRARY_H
#define LIBRARY_H

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

extern "C" {
    DLLEXPORT double putchard(double X);
    DLLEXPORT double printd(double X);
    DLLEXPORT double printstr(const char* Str);
    DLLEXPORT double inputd();
}

#endif
