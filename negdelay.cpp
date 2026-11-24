#include "vst2.h"
#include "presets.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define NPAIRS       16
#define NCH          (NPAIRS * 2)
#define DELAY_MIN_MS (-500.0f)
#define DELAY_MAX_MS 100.0f
#define BASELINE_MS  500.0f

typedef struct {
    audioMasterCallback host;
    AEffect *self;
    float sampleRate;
    int   blockSize;
    float param[NPAIRS];
    int   valueSamples[NPAIRS];
    int   baseline;
    int   merge;
    int   armBypass;
    int   bypass;
    int   armed;
    int   reaperReady;
    void  *track;
    const char *(*getSetTrackInfo)(void *, const char *, void *);
    int   pollCountdown;
    int   curProgram;
    int   delaySamples[NPAIRS];
    float *buf[NCH];
    int   bufCap;
    int   wPos;
} Plug;

static float pairMs(const Plug *p, int i) {
    return DELAY_MIN_MS + p->param[i] * (DELAY_MAX_MS - DELAY_MIN_MS);
}

static void recompute(Plug *p) {
    int B = 0;
    int zero = (p->armBypass && p->armed) || p->bypass;
    for (int i = 0; i < NPAIRS; i++) {
        float ms = pairMs(p, i);
        int v = zero ? 0 : (int)(ms * p->sampleRate * 0.001f + (ms >= 0 ? 0.5f : -0.5f));
        p->valueSamples[i] = v;
        if (-v > B) B = -v;
    }
    p->baseline = B;
    for (int i = 0; i < NPAIRS; i++) {
        int d = B + p->valueSamples[i];
        p->delaySamples[i] = d > 0 ? d : 0;
    }
}

static void freeBuf(Plug *p) {
    for (int c = 0; c < NCH; c++) { free(p->buf[c]); p->buf[c] = NULL; }
    p->bufCap = p->wPos = 0;
}

static int ensureBuf(Plug *p) {
    int need = (int)((BASELINE_MS + DELAY_MAX_MS) * p->sampleRate * 0.001f) + 1 + p->blockSize;
    if (p->bufCap >= need && p->buf[0]) return 1;
    int ok = 1;
    for (int c = 0; c < NCH; c++) {
        float *nb = (float *)realloc(p->buf[c], (size_t)need * sizeof(float));
        if (!nb) { ok = 0; break; }
        p->buf[c] = nb;
    }
    if (!ok) { freeBuf(p); return 0; }
    p->bufCap = need;
    return 1;
}

static void pollArmState(Plug *p) {
    if (!p->armBypass) return;
    if (--p->pollCountdown > 0) return;
    p->pollCountdown = 16;
    if (!p->reaperReady) {
        p->reaperReady = 1;
        p->track = (void *)p->host(p->self, 0xdeadbeef, 0xdeadf00e, 1, NULL, 0.0f);
        VstIntPtr fn = p->host(NULL, 0xdeadbeef, 0xdeadf00d, 0, (void *)"GetSetMediaTrackInfo_Value", 0.0f);
        if (fn) memcpy(&p->getSetTrackInfo, &fn, sizeof(fn));
    }
    if (!p->track || !p->getSetTrackInfo) return;
    int armed = p->getSetTrackInfo(p->track, "I_RECARM", NULL) != NULL;
    if (armed != p->armed) {
        p->armed = armed;
        recompute(p);
        if (p->self->initialDelay != p->baseline) {
            p->self->initialDelay = p->baseline;
            p->host(p->self, audioMasterIOChanged, 0, 0, NULL, 0);
        }
    }
}

static void process(Plug *p, float **in, float **out, VstInt32 n) {
    pollArmState(p);
    int cap = p->bufCap;
    for (int c = 0; c < NCH; c++) {
        int d = p->delaySamples[c >> 1];
        if (d <= 0 || !p->buf[0]) {
            if (out[c] != in[c]) memcpy(out[c], in[c], (size_t)n * sizeof(float));
            continue;
        }
        if (d > cap - (int)n) d = cap - (int)n;
        if (d < 0) d = 0;
        float *b = p->buf[c];
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
    if (p->merge) {
        for (VstInt32 i = 0; i < n; i++) {
            float l = out[0][i], r = out[1][i];
            for (int c = 2; c < NCH; c += 2) { l += out[c][i]; r += out[c+1][i]; }
            out[0][i] = l;
            out[1][i] = r;
        }
        for (int c = 2; c < NCH; c++) memset(out[c], 0, (size_t)n * sizeof(float));
    }
}

static float normFromMs(float ms) {
    return (ms - DELAY_MIN_MS) / (DELAY_MAX_MS - DELAY_MIN_MS);
}

static void applyProgram(AEffect *e, int idx) {
    Plug *p = (Plug *)e->object;
    p->curProgram = idx;
    if (idx <= 0 || idx > kNDPresetCount) {
        for (int i = 0; i < NPAIRS; i++) p->param[i] = normFromMs(0.0f);
    } else {
        const NDPreset *pr = &kNDPresets[idx - 1];
        for (int i = 0; i < NPAIRS; i++) p->param[i] = normFromMs(pr->ms[i]);
    }
    recompute(p);
    if (e->initialDelay != p->baseline) {
        e->initialDelay = p->baseline;
        p->host(e, audioMasterIOChanged, 0, 0, NULL, 0);
    }
}

static VstIntPtr dispatcher(AEffect *e, VstInt32 op, VstInt32 idx, VstIntPtr val, void *ptr, float opt) {
    Plug *p = (Plug *)e->object;
    switch (op) {
    case effSetProgram:
        applyProgram(e, (int)val);
        return 1;
    case effGetProgram:
        return p->curProgram;
    case effGetProgramName:
        if (ptr) {
            if (p->curProgram >= 1 && p->curProgram <= kNDPresetCount) strcpy((char *)ptr, kNDPresets[p->curProgram - 1].name);
            else strcpy((char *)ptr, "Init");
        }
        return 1;
    case effGetProgramNameIndexed:
        if (ptr && idx >= 1 && idx <= kNDPresetCount) strcpy((char *)ptr, kNDPresets[idx - 1].name);
        else if (ptr) strcpy((char *)ptr, "Init");
        return 1;
    case effOpen:
        p->sampleRate = 44100.0f;
        p->blockSize = 512;
        recompute(p);
        e->initialDelay = p->baseline;
        return 1;
    case effClose:
        freeBuf(p);
        free(p);
        e->object = NULL;
        return 1;
    case effSetSampleRate:
        p->sampleRate = opt > 0 ? opt : 44100.0f;
        recompute(p);
        e->initialDelay = p->baseline;
        ensureBuf(p);
        return 1;
    case effSetBlockSize:
        p->blockSize = (int)(val > 0 ? val : 512);
        ensureBuf(p);
        return 1;
    case effMainsChanged:
        if (val) {
            recompute(p);
            e->initialDelay = p->baseline;
            if (!ensureBuf(p)) return 0;
        } else {
            p->wPos = 0;
        }
        return 1;
    case effGetParamLabel:
        if (ptr) strcpy((char *)ptr, (idx >= NPAIRS) ? "" : "ms");
        return 1;
    case effGetParamDisplay:
        if (ptr && idx == NPAIRS) { strcpy((char *)ptr, ((Plug *)e->object)->merge ? "on" : "off"); return 1; }
        if (ptr && idx == NPAIRS + 1) { strcpy((char *)ptr, ((Plug *)e->object)->armBypass ? "on" : "off"); return 1; }
        if (ptr && idx == NPAIRS + 2) { strcpy((char *)ptr, ((Plug *)e->object)->bypass ? "on" : "off"); return 1; }
        if (ptr && idx >= 0 && idx < NPAIRS) sprintf((char *)ptr, "%.2f", pairMs(p, idx));
        return 1;
    case effGetParamName:
        if (ptr && idx == NPAIRS) { strcpy((char *)ptr, "Merge"); return 1; }
        if (ptr && idx == NPAIRS + 1) { strcpy((char *)ptr, "Arm bypass"); return 1; }
        if (ptr && idx == NPAIRS + 2) { strcpy((char *)ptr, "Bypass"); return 1; }
        if (ptr && idx >= 0 && idx < NPAIRS) {
            const char *n = NULL;
            if (p->curProgram >= 1 && p->curProgram <= kNDPresetCount) n = kNDPresets[p->curProgram - 1].names[idx];
            if (n) {
                strncpy((char *)ptr, n, 63);
                ((char *)ptr)[63] = '\0';
            } else {
                sprintf((char *)ptr, "P%d", idx + 1);
            }
        }
        return 1;
    case effGetParameterProperties:
        if (idx >= NPAIRS && idx <= NPAIRS + 2 && ptr) {
            VstParameterProperties *pp = (VstParameterProperties *)ptr;
            memset(pp, 0, sizeof(*pp));
            pp->flags = kVstParameterIsSwitch;
            return 1;
        }
        return 0;
    case effGetEffectName:
        if (ptr) strcpy((char *)ptr, "NegativeDelay16");
        return 1;
    case effGetVendorString:
        if (ptr) strcpy((char *)ptr, "paul");
        return 1;
    case effGetProductString:
        if (ptr) strcpy((char *)ptr, "NegativeDelay16");
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
        return p->baseline;
    case effCanDo:
        if (ptr && strcmp((char *)ptr, "hasCockosExtensions") == 0) return (VstIntPtr)0xbeef0000;
        return -1;
    }
    return 0;
}

static void setParameter(AEffect *e, VstInt32 idx, float v) {
    Plug *p = (Plug *)e->object;
    if (idx == NPAIRS) { p->merge = v >= 0.5f ? 1 : 0; return; }
    if (idx == NPAIRS + 1 || idx == NPAIRS + 2) {
        if (idx == NPAIRS + 1) p->armBypass = v >= 0.5f ? 1 : 0;
        else p->bypass = v >= 0.5f ? 1 : 0;
        recompute(p);
        if (e->initialDelay != p->baseline) {
            e->initialDelay = p->baseline;
            p->host(e, audioMasterIOChanged, 0, 0, NULL, 0);
        }
        return;
    }
    if (idx < 0 || idx >= NPAIRS) return;
    p->param[idx] = v;
    recompute(p);
    if (e->initialDelay != p->baseline) {
        e->initialDelay = p->baseline;
        p->host(e, audioMasterIOChanged, 0, 0, NULL, 0);
    }
}

static float getParameter(AEffect *e, VstInt32 idx) {
    Plug *p = (Plug *)e->object;
    if (idx == NPAIRS) return (float)p->merge;
    if (idx == NPAIRS + 1) return (float)p->armBypass;
    if (idx == NPAIRS + 2) return (float)p->bypass;
    if (idx < 0 || idx >= NPAIRS) return 0;
    return p->param[idx];
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
    for (int i = 0; i < NPAIRS; i++) p->param[i] = (0.0f - DELAY_MIN_MS) / (DELAY_MAX_MS - DELAY_MIN_MS);
    p->merge = 1;
    p->armBypass = 1;
    AEffect *e = (AEffect *)calloc(1, sizeof(AEffect));
    if (!e) { free(p); return NULL; }
    e->magic = kEffectMagic;
    e->object = p;
    e->dispatcher = dispatcher;
    e->setParameter = setParameter;
    e->getParameter = getParameter;
    e->processReplacing = processReplacing;
    e->numPrograms = kNDPresetCount + 1;
    e->numParams = NPAIRS + 3;
    e->numInputs = NCH;
    e->numOutputs = NCH;
    e->flags = effFlagsCanReplacing;
    e->uniqueID = CCONST('N', 'g', 'D', '2');
    e->version = 100;
    e->initialDelay = 0;
    p->self = e;
    return e;
}

VST_EXPORT AEffect *VSTPluginMain(audioMasterCallback host) { return initPlugin(host); }
VST_EXPORT AEffect *main_plugin(audioMasterCallback host) { return initPlugin(host); }
