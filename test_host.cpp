#include "vst2.h"
#include <windows.h>
#include <stdio.h>
#include <math.h>

#define NPAIRS 16
#define NCH 32
#define WINDOW 32768

static float g_buf[NCH][65536];
static float *g_in[NCH], *g_out[NCH];
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
    printf("%-46s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static void run(AEffect *e, int total) {
    for (int off = 0; off < total; off += 128) {
        for (int c = 0; c < NCH; c++) { g_in[c] = g_buf[c] + off; g_out[c] = g_buf[c] + 32768 + off; }
        e->processReplacing(e, g_in, g_out, 128);
    }
}

static void zeroAll() {
    for (int c = 0; c < NCH; c++) memset(g_buf[c], 0, sizeof(g_buf[0]));
}

static void impulseAtPair(int pair) {
    zeroAll();
    g_buf[pair*2][0] = 1.0f;
    g_buf[pair*2+1][0] = 1.0f;
}

static int singleImpulse(int ch, int pos) {
    for (int i = 0; i < WINDOW; i++) {
        float *o = g_buf[ch] + 32768;
        if (i == pos) { if (o[i] != 1.0f) return 0; }
        else if (o[i] != 0.0f) return 0;
    }
    return 1;
}

int main() {
    HMODULE h = LoadLibraryA("NegativeDelay16.dll");
    if (!h) { printf("LoadLibrary failed\n"); return 1; }
    typedef AEffect *(*MainProc)(audioMasterCallback);
    MainProc mainProc = (MainProc)(void *)GetProcAddress(h, "VSTPluginMain");
    if (!mainProc) mainProc = (MainProc)(void *)GetProcAddress(h, "main");
    if (!mainProc) { printf("no entry point\n"); return 1; }

    AEffect *e = mainProc(hostProc);
    check("effect created", e && e->magic == kEffectMagic);
    check("32ch in/out", e->numInputs == NCH && e->numOutputs == NCH);
    check("16 parameters", e->numParams == NPAIRS);

    e->dispatcher(e, effOpen, 0, 0, NULL, 0);
    e->dispatcher(e, effSetSampleRate, 0, 0, NULL, 44100.0f);
    e->dispatcher(e, effSetBlockSize, 0, 128, NULL, 0);

    char name[64] = {0};
    e->dispatcher(e, effGetEffectName, 0, 0, name, 0);
    check("effect name NegativeDelay16", strcmp(name, "NegativeDelay16") == 0);
    char pname[64] = {0};
    e->dispatcher(e, effGetParamName, 0, 0, pname, 0);
    check("param 0 named P1", strcmp(pname, "P1") == 0);

    e->dispatcher(e, effMainsChanged, 0, 1, NULL, 0);
    check("all-zero defaults -> initialDelay 0", e->initialDelay == 0);

    for (int i = 0; i < 256; i++) { g_buf[0][i] = (float)(i + 3); g_buf[1][i] = (float)(i + 5); }
    run(e, 256);
    int ok = 1;
    for (int i = 0; i < 256; i++) if (g_buf[0][32768 + i] != g_buf[0][i] || g_buf[1][32768 + i] != g_buf[1][i]) ok = 0;
    check("all-zero passthrough identity", ok);

    g_ioChanged = 0;
    e->setParameter(e, 1, normParam(-500.0f));
    e->dispatcher(e, effMainsChanged, 0, 1, NULL, 0);
    check("P2=-500ms -> initialDelay 22050", e->initialDelay == 22050);
    check("IOChanged signalled on baseline change", g_ioChanged >= 1);

    zeroAll();
    run(e, WINDOW);
    impulseAtPair(0);
    run(e, WINDOW);
    check("P1=0ms impulse at 22050", singleImpulse(0, 22050) && singleImpulse(1, 22050));

    for (int i = 0; i < 256; i++) { g_buf[2][i] = (float)(i + 3); g_buf[3][i] = (float)(i + 5); }
    run(e, 256);
    ok = 1;
    for (int i = 0; i < 256; i++) if (g_buf[2][32768 + i] != g_buf[2][i] || g_buf[3][32768 + i] != g_buf[3][i]) ok = 0;
    check("P2=-500ms passthrough identity", ok);

    e->setParameter(e, 2, normParam(-100.0f));
    e->dispatcher(e, effMainsChanged, 0, 1, NULL, 0);
    zeroAll();
    run(e, WINDOW);
    impulseAtPair(2);
    run(e, WINDOW);
    check("P3=-100ms impulse at 17640", singleImpulse(4, 17640) && singleImpulse(5, 17640));

    e->setParameter(e, 15, normParam(100.0f));
    e->dispatcher(e, effMainsChanged, 0, 1, NULL, 0);
    zeroAll();
    run(e, WINDOW);
    impulseAtPair(15);
    run(e, WINDOW);
    check("P16=+100ms impulse at 26460", singleImpulse(30, 26460) && singleImpulse(31, 26460));

    check("initialDelay still 22050", e->initialDelay == 22050);

    float saved = e->getParameter(e, 15);
    check("getParameter roundtrip", fabsf(saved - normParam(100.0f)) < 1e-6f);

    char disp[64] = {0};
    e->dispatcher(e, effGetParamDisplay, 15, 0, disp, 0);
    printf("  param 16 display at +100ms: '%s'\n", disp);
    e->dispatcher(e, effMainsChanged, 0, 0, NULL, 0);
    e->dispatcher(e, effClose, 0, 0, NULL, 0);
    FreeLibrary(h);
    printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
