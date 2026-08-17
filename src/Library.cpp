#include "Library.h"
#include <cstdio>

extern "C" DLLEXPORT double putchard(double X) {
    fputc((char)X, stderr);
    return 0;
}

extern "C" DLLEXPORT double printd(double X) {
    fprintf(stderr, "%f\n", X);
    return 0;
}

extern "C" DLLEXPORT double printstr(const char* Str) {
    fprintf(stderr, "%s", Str);
    return 0;
}

extern "C" DLLEXPORT double inputd() {
    double X;
    scanf("%lf", &X);
    return X;
}