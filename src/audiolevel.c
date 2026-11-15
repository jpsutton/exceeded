/* audiolevel.c - loudness measurement (ITU-R BS.1770-4), gains and slider law (spec chapter 15) */
#include "audiolevel.h"
#include <math.h>
#include <stdlib.h>

typedef struct Bq { double b0, b1, b2, a1, a2, x1, x2, y1, y2; } Bq;

static double bq_run(Bq *q, double x) {
    double y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x; q->y2 = q->y1; q->y1 = y;
    return y;
}

static void kweights(Bq *s1, Bq *s2, double fs) {
    {   /* stage 1: shelf */
        double G = 3.999843853973347, Q = 0.7071752369554196, fc = 1681.974450955533;
        double K = tan(M_PI * fc / fs), Vh = pow(10.0, G / 20.0), Vb = pow(Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        *s1 = (Bq){(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0, 0, 0, 0, 0};
    }
    {   /* stage 2: high-pass */
        double Q = 0.5003270373253953, fc = 38.13547087613982;
        double K = tan(M_PI * fc / fs);
        double a0 = 1.0 + K / Q + K * K;
        *s2 = (Bq){1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0, 0, 0, 0, 0};
    }
}

void loud_measure(const float *x, size_t n, int fs, Loud *out) {
    out->silent = 1;
    out->has_peak = 0;
    out->lufs = -200.0;
    out->peak_db = -200.0;
    if (!n || fs <= 0) return;
    double peak = 0;
    for (size_t i = 0; i < n; i++) { double a = fabs((double)x[i]); if (a > peak) peak = a; }
    if (peak > 0) { out->has_peak = 1; out->peak_db = 20.0 * log10(peak); }
    Bq s1, s2;
    kweights(&s1, &s2, (double)fs);
    double *y2 = malloc(sizeof(double) * n);
    if (!y2) return;
    for (size_t i = 0; i < n; i++) { double y = bq_run(&s2, bq_run(&s1, (double)x[i])); y2[i] = y * y; }
    size_t B = (size_t)floor(0.4 * fs + 0.5), S = (size_t)floor(0.1 * fs + 0.5);
    if (B < 1) B = 1;
    if (S < 1) S = 1;
    /* prefix sums for the block means */
    double *ps = malloc(sizeof(double) * (n + 1));
    if (!ps) { free(y2); return; }
    ps[0] = 0;
    for (size_t i = 0; i < n; i++) ps[i + 1] = ps[i] + y2[i];
    size_t nb = 0;
    double *z = malloc(sizeof(double) * (n / S + 2));
    if (n < B) z[nb++] = ps[n] / (double)n;
    else for (size_t st = 0; st + B <= n; st += S) z[nb++] = (ps[st + B] - ps[st]) / (double)B;
    /* absolute gate */
    double sum = 0;
    size_t cnt = 0;
    for (size_t j = 0; j < nb; j++) {
        double l = -0.691 + 10.0 * log10(z[j] > 0 ? z[j] : 1e-300);
        if (l > -70.0) { sum += z[j]; cnt++; }
    }
    if (cnt) {
        double gamma = -0.691 + 10.0 * log10(sum / (double)cnt) - 10.0;
        double sum2 = 0;
        size_t cnt2 = 0;
        for (size_t j = 0; j < nb; j++) {
            double l = -0.691 + 10.0 * log10(z[j] > 0 ? z[j] : 1e-300);
            if (l > -70.0 && l > gamma) { sum2 += z[j]; cnt2++; }
        }
        if (cnt2) { out->silent = 0; out->lufs = -0.691 + 10.0 * log10(sum2 / (double)cnt2); }
    }
    free(z); free(ps); free(y2);
}

double level_target(int bus) { return bus == LB_MUSIC ? -20.0 : bus == LB_EFFECTS ? -16.0 : -15.0; }

double level_style_gain(int style, int bus) {
    static const double t[3][3] = {{12.6, -2.2, -0.9}, {-3.5, 4.8, 0}, {-11.2, -6.8, 0}};
    return t[style][bus];
}

double level_asset_gain(int style, int bus, const Loud *l, int is_wave, int even_out) {
    double G = level_style_gain(style, bus);
    double g = G;
    if (even_out && !l->silent) {
        double R = bus == LB_MUSIC ? 6.0 : 12.0;
        g = level_target(bus) - l->lufs;
        if (g < G - R) g = G - R;
        if (g > G + R) g = G + R;
        if (is_wave && l->has_peak && g > 3.0 - l->peak_db) g = 3.0 - l->peak_db;
    }
    return floor(g * 10.0 + 0.5) / 10.0;
}

double level_slider_db(int v, int *muted) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (muted) *muted = v == 0;
    if (v == 0) return -200.0;
    return 40.0 * log10(v / 100.0);
}

double level_db_to_lin(double db) { return pow(10.0, db / 20.0); }

float level_softclip(float x, float knee, float ceiling) {
    float a = fabsf(x);
    if (a < knee) return x;
    float y = knee + (ceiling - knee) * tanhf((a - knee) / (ceiling - knee));
    return x < 0 ? -y : y;
}
