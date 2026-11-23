#include "vst2.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DELAY_MIN_MS   (-500.0f)
#define DELAY_MAX_MS   100.0f

typedef struct {
    audioMasterCallback host;
    float sampleRate;
    int   blockSize;
    float param;
    int   delaySamples;
    float *buf[2];
    int   bufCap;
    int   wPos;
} Plug;

static int calcDelay(const Plug *p) {
    float ms = DELAY_MIN_MS + p->param * (DELAY_MAX_MS - DELAY_MIN_MS);
    int d = (int)(ms * p->sampleRate * 0.001f + (ms >= 0 ? 0.5f : -0.5f));
    return d;
}

static void freeBuf(Plug *p) {
    free(p->buf[0]); free(p->buf[1]);
    p->buf[0] = p->buf[1] = NULL;
    p->bufCap = p->wPos = 0;
}

static int ensureBuf(Plug *p) {
    int maxPos = (int)(DELAY_MAX_MS * p->sampleRate * 0.001f) + 1;
    int need = maxPos + p->blockSize;
    if (p->bufCap >= need && p->buf[0]) return 1;
    float *l = (float *)realloc(p->buf[0], (size_t)need * sizeof(float));
    float *r = (float *)realloc(p->buf[1], (size_t)need * sizeof(float));
    if (!l || !r) {
        if (l && !r) { free(l); l = NULL; }
        if (r && !l) { free(r); r = NULL; }
        if (l) { p->buf[0] = l; }
        if (r) { p->buf[1] = r; }
        if (!l || !r) { freeBuf(p); return 0; }
    }
    p->buf[0] = l; p->buf[1] = r;
    p->bufCap = need;
    return 1;
}

static void process(Plug *p, float **in, float **out, VstInt32 n) {
    int d = p->delaySamples;
    if (d <= 0 || !p->buf[0]) {
        if (out[0] != in[0]) memcpy(out[0], in[0], (size_t)n * sizeof(float));
        if (out[1] != in[1]) memcpy(out[1], in[1], (size_t)n * sizeof(float));
        return;
    }
    if (d > p->bufCap - (int)n) d = p->bufCap - (int)n;
    if (d < 0) d = 0;
    for (int c = 0; c < 2; c++) {
        float *b = p->buf[c];
        int cap = p->bufCap;
        int w = p->wPos;
        int i = 0;
        while (i < n) {
            int m = n - i;
            if (m > cap - w) m = cap - w;
            memcpy(b + w, in[c] + i, (size_t)m * sizeof(float));
            int r = w - d;
            if (r < 0) r += cap;
            if (r + m <= cap) {
                memcpy(out[c] + i, b + r, (size_t)m * sizeof(float));
            } else {
                int first = cap - r;
                memcpy(out[c] + i, b + r, (size_t)first * sizeof(float));
                memcpy(out[c] + i + first, b, (size_t)(m - first) * sizeof(float));
            }
            w += m; if (w >= cap) w = 0;
            i += m;
        }
    }
    p->wPos += (int)n;
    if (p->wPos >= p->bufCap) p->wPos -= p->bufCap;
}

static VstIntPtr dispatcher(AEffect *e, VstInt32 op, VstInt32 idx, VstIntPtr val, void *ptr, float opt) {
    (void)idx;
    Plug *p = (Plug *)e->object;
    switch (op) {
    case effOpen:
        p->sampleRate = 44100.0f;
        p->blockSize = 512;
        e->initialDelay = calcDelay(p);
        return 1;
    case effClose:
        freeBuf(p);
        free(p);
        e->object = NULL;
        return 1;
    case effSetSampleRate:
        p->sampleRate = opt > 0 ? opt : 44100.0f;
        p->delaySamples = calcDelay(p);
        e->initialDelay = p->delaySamples;
        ensureBuf(p);
        return 1;
    case effSetBlockSize:
        p->blockSize = (int)(val > 0 ? val : 512);
        ensureBuf(p);
        return 1;
    case effMainsChanged:
        if (val) {
            p->delaySamples = calcDelay(p);
            e->initialDelay = p->delaySamples;
            if (!ensureBuf(p)) return 0;
        } else {
            p->wPos = 0;
        }
        return 1;
    case effGetParamLabel:
        if (ptr) strcpy((char *)ptr, "ms");
        return 1;
    case effGetParamDisplay:
        if (ptr) sprintf((char *)ptr, "%.2f", DELAY_MIN_MS + p->param * (DELAY_MAX_MS - DELAY_MIN_MS));
        return 1;
    case effGetParamName:
        if (ptr) strcpy((char *)ptr, "Delay");
        return 1;
    case effGetEffectName:
        if (ptr) strcpy((char *)ptr, "NegativeDelay");
        return 1;
    case effGetVendorString:
        if (ptr) strcpy((char *)ptr, "paul");
        return 1;
    case effGetProductString:
        if (ptr) strcpy((char *)ptr, "NegativeDelay");
        return 1;
    case effGetVendorVersion:
        return 100;
    case effGetVstVersion:
        return kVstVersion;
    case effGetPlugCategory:
        return kPlugCategEffect;
    case effIdentify:
        return kEffectIdentify;
    case effGetTailSize:
        return p->delaySamples > 0 ? p->delaySamples : 0;
    case effCanDo:
        return -1;
    }
    return 0;
}

static void setParameter(AEffect *e, VstInt32 idx, float v) {
    (void)idx;
    Plug *p = (Plug *)e->object;
    p->param = v;
    p->delaySamples = calcDelay(p);
    e->initialDelay = p->delaySamples;
    p->host(e, audioMasterIOChanged, 0, 0, NULL, 0);
}

static float getParameter(AEffect *e, VstInt32 idx) {
    (void)idx;
    return ((Plug *)e->object)->param;
}

static void processReplacing(AEffect *e, float **in, float **out, VstInt32 n) {
    if (n > 0) process((Plug *)e->object, in, out, n);
}

static AEffect *initPlugin(audioMasterCallback host) {
    if (!host) return NULL;
    if (host(NULL, audioMasterVersion, 0, 0, NULL, 0) == 0) return NULL;
    Plug *p = (Plug *)calloc(1, sizeof(Plug));
    if (!p) return NULL;
    p->host = host;
    p->sampleRate = 44100.0f;
    p->blockSize = 512;
    AEffect *e = (AEffect *)calloc(1, sizeof(AEffect));
    if (!e) { free(p); return NULL; }
    e->magic = kEffectMagic;
    e->object = p;
    e->dispatcher = dispatcher;
    e->setParameter = setParameter;
    e->getParameter = getParameter;
    e->processReplacing = processReplacing;
    e->numPrograms = 1;
    e->numParams = 1;
    e->numInputs = 2;
    e->numOutputs = 2;
    e->flags = effFlagsCanReplacing;
    e->uniqueID = CCONST('N', 'g', 'D', '1');
    e->version = 100;
    e->initialDelay = 0;
    return e;
}

VST_EXPORT AEffect *VSTPluginMain(audioMasterCallback host) { return initPlugin(host); }
VST_EXPORT AEffect *main_plugin(audioMasterCallback host) { return initPlugin(host); }
