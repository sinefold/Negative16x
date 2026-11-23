#include "vst2.h"
#include "presets.h"
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
    check("17 parameters", e->numParams == NPAIRS + 1);

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

    e->setParameter(e, 16, 0.0f);

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

    e->setParameter(e, 16, 1.0f);
    check("merge re-enabled reads 1", e->getParameter(e, 16) == 1.0f);

    zeroAll();
    run(e, WINDOW);
    impulseAtPair(0);
    run(e, WINDOW);
    ok = singleImpulse(0, 22050) && singleImpulse(1, 22050);
    for (int c = 2; c < NCH; c++) if (!singleImpulse(c, -1)) ok = 0;
    check("merge on: pair0 impulse to ch1/2, others silent", ok);

    zeroAll();
    run(e, WINDOW);
    impulseAtPair(0);
    g_buf[4][0] = 1.0f;
    g_buf[5][0] = 1.0f;
    run(e, WINDOW);
    ok = 1;
    for (int i = 0; i < WINDOW; i++) {
        float *o = g_buf[0] + 32768;
        float want = (i == 22050 || i == 17640) ? 1.0f : 0.0f;
        if (o[i] != want) ok = 0;
        float *o1 = g_buf[1] + 32768;
        if (o1[i] != want) ok = 0;
    }
    check("merge on: pairs 0+2 sum into ch1/2", ok);

    e->setParameter(e, 16, 0.0f);
    zeroAll();
    run(e, WINDOW);
    impulseAtPair(2);
    run(e, WINDOW);
    ok = singleImpulse(4, 17640) && singleImpulse(5, 17640) && singleImpulse(0, -1) && singleImpulse(1, -1);
    check("merge off: per-pair outputs restored", ok);

    check("merge param reads 0", e->getParameter(e, 16) == 0.0f);

    check("program count", e->numPrograms == kNDPresetCount + 1);

    char progname[64] = {0};
    e->dispatcher(e, effGetProgramNameIndexed, 1, 0, progname, 0);
    check("program 1 name from preset table", strcmp(progname, kNDPresets[0].name) == 0);

    e->dispatcher(e, effSetProgram, 0, 1, NULL, 0);
    ok = 1;
    for (int i = 0; i < NPAIRS; i++) {
        float want = (kNDPresets[0].ms[i] + 500.0f) / 600.0f;
        if (fabsf(e->getParameter(e, i) - want) > 1e-4f) ok = 0;
    }
    check("program 1 applies all 16 pair values", ok);

    int worstMs = 0;
    for (int i = 0; i < NPAIRS; i++) { int v = (int)kNDPresets[0].ms[i]; if (v < worstMs) worstMs = v; }
    check("program 1 sets baseline PDC", e->initialDelay == (int)(-worstMs * 44100.0f / 1000.0f + 0.5f));

    char artname[64] = {0};
    e->dispatcher(e, effGetParamName, 0, 0, artname, 0);
    check("slider 1 shows articulation name", kNDPresets[0].names[0] && strcmp(artname, kNDPresets[0].names[0]) == 0);

    e->dispatcher(e, effSetProgram, 0, 0, NULL, 0);
    ok = 1;
    for (int i = 0; i < NPAIRS; i++) if (fabsf(e->getParameter(e, i) - 0.833333f) > 1e-4f) ok = 0;
    check("program 0 resets to zero delay", ok);

    char pname2[64] = {0};
    e->dispatcher(e, effGetParamName, 0, 0, pname2, 0);
    check("program 0 sliders revert to P1..P16", strcmp(pname2, "P1") == 0);

    float saved = e->getParameter(e, 15);
    check("getParameter roundtrip", fabsf(saved - (kNDPresets[0].ms[15] + 500.0f) / 600.0f) < 1e-4f);

    char disp[64] = {0};
    e->dispatcher(e, effGetParamDisplay, 15, 0, disp, 0);
    printf("  param 16 display: '%s'\n", disp);
    e->dispatcher(e, effMainsChanged, 0, 0, NULL, 0);
    e->dispatcher(e, effClose, 0, 0, NULL, 0);
    FreeLibrary(h);
    printf(failures ? "\n%d FAILURES\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
