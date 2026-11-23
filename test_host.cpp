#include "vst2.h"
#include <windows.h>
#include <stdio.h>
#include <math.h>

static int g_ioChanged = 0;

static VstIntPtr hostProc(AEffect *e, VstInt32 op, VstInt32, VstIntPtr, void *, float) {
    if (op == audioMasterVersion) return kVstVersion;
    if (op == audioMasterIOChanged) { g_ioChanged++; return 1; }
    (void)e;
    return 0;
}

static float normParam(float ms) { return (ms + 500.0f) / 600.0f; }

static int failures = 0;
static void check(const char *name, int ok) {
    printf("%-42s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static void run(AEffect *e, float *in0, float *in1, float *out0, float *out1, int total) {
    for (int off = 0; off < total; off += 128) {
        float *pin[2] = { in0 + off, in1 + off };
        float *pout[2] = { out0 + off, out1 + off };
        e->processReplacing(e, pin, pout, 128);
    }
}

int main() {
    HMODULE h = LoadLibraryA("NegativeDelay.dll");
    if (!h) { printf("LoadLibrary failed\n"); return 1; }
    typedef AEffect *(*MainProc)(audioMasterCallback);
    MainProc mainProc = (MainProc)(void *)GetProcAddress(h, "VSTPluginMain");
    if (!mainProc) mainProc = (MainProc)(void *)GetProcAddress(h, "main");
    if (!mainProc) { printf("no entry point\n"); return 1; }

    AEffect *e = mainProc(hostProc);
    check("effect created", e && e->magic == kEffectMagic);
    check("stereo 2in/2out", e->numInputs == 2 && e->numOutputs == 2);
    check("one parameter", e->numParams == 1);

    e->dispatcher(e, effOpen, 0, 0, NULL, 0);
    e->dispatcher(e, effSetSampleRate, 0, 0, NULL, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 128, NULL, 0);

    char name[64] = {0};
    e->dispatcher(e, effGetEffectName, 0, 0, name, 0);
    check("effect name", strcmp(name, "NegativeDelay") == 0);

    g_ioChanged = 0;
    e->setParameter(e, 0, normParam(0.0f));
    e->dispatcher(e, effMainsChanged, 0, 1, NULL, 0);
    check("0ms -> initialDelay 0", e->initialDelay == 0);
    check("IOChanged signalled on param set", g_ioChanged >= 1);

    float in[2][512], out[2][512];
    for (int i = 0; i < 256; i++) { in[0][i] = (float)i; in[1][i] = (float)(1000 + i); }
    run(e, in[0], in[1], out[0], out[1], 256);
    int     ok = 1;
    for (int i = 0; i < 256; i++) if (out[0][i] != in[0][i] || out[1][i] != in[1][i]) ok = 0;
    check("0ms passthrough identity (blocks)", ok);

    e->setParameter(e, 0, normParam(10.0f));
    e->dispatcher(e, effMainsChanged, 0, 0, NULL, 0);
    e->dispatcher(e, effMainsChanged, 0, 1, NULL, 0);
    check("10ms -> initialDelay 441", e->initialDelay == 441);

    for (int i = 0; i < 512; i++) { in[0][i] = 0.0f; in[1][i] = 0.0f; }
    in[0][0] = in[1][0] = 1.0f;
    memset(out, 0, sizeof(out));
    run(e, in[0], in[1], out[0], out[1], 512);
    check("10ms impulse lands at sample 441", out[0][441] == 1.0f && out[1][441] == 1.0f);
    int clean = 1;
    for (int i = 0; i < 512; i++) if (i != 441 && (out[0][i] != 0.0f || out[1][i] != 0.0f)) clean = 0;
    check("10ms no other output samples", clean);

    e->setParameter(e, 0, normParam(-500.0f));
    e->dispatcher(e, effMainsChanged, 0, 1, NULL, 0);
    check("-500ms -> initialDelay -22050", e->initialDelay == -22050);

    for (int i = 0; i < 256; i++) { in[0][i] = (float)(i + 7); in[1][i] = (float)(i + 9); }
    run(e, in[0], in[1], out[0], out[1], 256);
    ok = 1;
    for (int i = 0; i < 256; i++) if (out[0][i] != in[0][i] || out[1][i] != in[1][i]) ok = 0;
    check("-500ms passthrough identity", ok);

    e->setParameter(e, 0, normParam(100.0f));
    e->dispatcher(e, effMainsChanged, 0, 1, NULL, 0);
    check("100ms -> initialDelay 4410", e->initialDelay == 4410);

    float saved = e->getParameter(e, 0);
    check("getParameter roundtrip", fabsf(saved - normParam(100.0f)) < 1e-6f);

    char disp[64] = {0};
    e->dispatcher(e, effGetParamDisplay, 0, 0, disp, 0);
    printf("  param display at 100ms: '%s'\n", disp);
    e->dispatcher(e, effMainsChanged, 0, 0, NULL, 0);
    e->dispatcher(e, effClose, 0, 0, NULL, 0);
    FreeLibrary(h);
    printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
