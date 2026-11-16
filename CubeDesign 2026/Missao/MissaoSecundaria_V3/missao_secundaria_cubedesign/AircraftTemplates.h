// GERADO AUTOMATICAMENTE por make_templates.py - perfis de largura dos modelos
// Cada perfil: largura da silhueta em 24 fatias, do nariz (0) a cauda (23),
// normalizada pela largura maxima. 'aspecto' = envergadura / comprimento.
#ifndef AIRCRAFT_TEMPLATES_H
#define AIRCRAFT_TEMPLATES_H

#define AC_PROFILE_BINS 24
#define AC_NUM_TEMPLATES 7

struct AircraftTemplate { const char* name; float aspect; float profile[AC_PROFILE_BINS]; };

static const AircraftTemplate AC_TEMPLATES[AC_NUM_TEMPLATES] = {
    {"Comercial", 0.940f, {0.085f, 0.156f, 0.171f, 0.173f, 0.175f, 0.178f, 0.180f, 0.183f, 0.199f, 0.370f, 0.585f, 0.800f, 1.000f, 0.908f, 0.706f, 0.422f, 0.178f, 0.174f, 0.172f, 0.170f, 0.263f, 0.426f, 0.586f, 0.212f}},
    {"Caca", 0.640f, {0.025f, 0.073f, 0.105f, 0.123f, 0.141f, 0.159f, 0.178f, 0.198f, 0.219f, 0.239f, 0.262f, 0.351f, 0.473f, 0.596f, 0.718f, 0.840f, 0.960f, 1.000f, 0.450f, 0.266f, 0.344f, 0.480f, 0.613f, 0.527f}},
    {"Delta", 0.660f, {0.023f, 0.067f, 0.106f, 0.122f, 0.134f, 0.147f, 0.181f, 0.236f, 0.291f, 0.345f, 0.400f, 0.455f, 0.510f, 0.565f, 0.620f, 0.675f, 0.730f, 0.785f, 0.840f, 0.894f, 0.949f, 1.000f, 0.640f, 0.134f}},
    {"AsaVoadora", 2.500f, {0.030f, 0.089f, 0.148f, 0.208f, 0.267f, 0.327f, 0.386f, 0.446f, 0.505f, 0.564f, 0.624f, 0.683f, 0.743f, 0.802f, 0.861f, 0.921f, 0.980f, 1.000f, 0.846f, 0.644f, 0.381f, 0.127f, 0.043f, 0.006f}},
    {"UCAV", 1.620f, {0.018f, 0.053f, 0.088f, 0.123f, 0.158f, 0.193f, 0.228f, 0.263f, 0.298f, 0.333f, 0.368f, 0.403f, 0.438f, 0.494f, 0.594f, 0.696f, 0.797f, 0.899f, 1.000f, 0.926f, 0.465f, 0.214f, 0.074f, 0.023f}},
    {"Helice", 1.320f, {0.059f, 0.092f, 0.105f, 0.106f, 0.106f, 0.106f, 1.000f, 1.000f, 1.000f, 1.000f, 0.184f, 0.098f, 0.092f, 0.086f, 0.080f, 0.074f, 0.068f, 0.062f, 0.056f, 0.050f, 0.244f, 0.379f, 0.379f, 0.126f}},
    {"Triangulo", 1.000f, {0.021f, 0.064f, 0.106f, 0.149f, 0.191f, 0.234f, 0.277f, 0.319f, 0.362f, 0.404f, 0.447f, 0.489f, 0.532f, 0.574f, 0.617f, 0.660f, 0.702f, 0.745f, 0.787f, 0.830f, 0.872f, 0.915f, 0.957f, 1.000f}},
};

#endif
