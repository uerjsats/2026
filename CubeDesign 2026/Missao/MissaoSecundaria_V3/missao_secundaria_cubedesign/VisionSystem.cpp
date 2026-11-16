/**************************************************************************************************
 __     ___    _   _ _____          _             _   _ _____ ____     _           _
 \ \   / / \  | \ | |_   _|__  __ _| |_          | | | | ____|  _ \   | |___  __ _| |_ ___
  \ \ / / _ \ |  \| | | |/ __|/ _` | __|  _____  | | | |  _| | |_) |  | / __|/ _` | __/ __|
   \ V / ___ \| |\  | | |\__ \ (_| | |_  |_____| | |_| | |___|  _ < |_| \__ \ (_| | |_\__ \
    \_/_/   \_\_| \_| |_||___/\__,_|\__|          \___/|_____|_| \_\___/|___/\__,_|\__|___/

 **************************************************************************************************
 * PROJETO: VANTsat - Equipe UERJsats: Missão Atlas LASC 2026
 * DESENVOLVEDORES: Carlos Leal e Vitor Forny
 * GITHUB: https://github.com/uerjsats https://github.com/Caduleal https://github.com/vitorforny04
 * OBJETIVO:  Sistema de reconhecimento computacional de imagens focado na detecção
 *          e classificação autônoma de triângulos e silhuetas de aeronaves.
 * CONTEXTO: Motor de processamento de imagens. Esta versão reconhece VÁRIOS tipos de
 *           aeronave (vistas de cima), em QUALQUER rotação, comparando o perfil de
 *           largura da silhueta com 7 modelos e com as misturas entre eles.
 **************************************************************************************************/

#include "VisionSystem.h"
#include "AircraftTemplates.h"
#include <math.h>

// =====================================================================================
// LIMITES DO CLASSIFICADOR — ajuste aqui. O painel /live lê estes mesmos valores.
// =====================================================================================
#define AC_CONTRAST_MIN   40       // diferença mínima entre média do "papel" e da "tinta"
#define AC_AREA_MIN_FRAC  0.02f    // mancha ocupa pelo menos 2% do quadro
#define AC_AREA_MAX_FRAC  0.60f    // ... e no máximo 60%
#define AC_SIZE_MIN       30       // maior lado da caixa envolvente (px)
#define AC_ASYM_MAX       0.060f   // assimetria máxima em relação ao eixo de simetria
#define AC_DIST_MAX       0.130f   // distância máxima ao modelo (ou mistura) mais parecido
#define AC_ASPECT_WEIGHT  0.15f    // peso da diferença de envergadura/comprimento na distância

// Parâmetros internos da busca (normalmente não precisa mexer)
#define AC_MAX_SAMPLES    6000     // pixels amostrados da mancha (limita o tempo de CPU)
#define AC_SYM_BINS       32       // fatias usadas para medir simetria
#define AC_COARSE_STEP    4        // passo (graus) da busca grossa do eixo
#define AC_BLEND_STEPS    20       // resolução da mistura entre dois modelos (0..1)

// Definição das variáveis globais
Point *contour = NULL;
int contourSize = 0;

void initVisionBuffers() {
    // Alocação na PSRAM para evitar estouro de memória interna
    // (função original do VANTsat_TX_V3 — reaproveitada sem alteração)
    contour = (Point *)ps_malloc(2000 * sizeof(Point));
}

// Função original do VANTsat_TX_V3 (parte do algoritmo RDP) — reaproveitada
// sem alteração (o detector atual não a usa, mas ela fica disponível).
// NÃO MUDE NADA AQUI!
float perpendicularDistance(Point p, Point s, Point e) {
    float dx = e.x - s.x;
    float dy = e.y - s.y;
    float mag = sqrt(dx * dx + dy * dy);
    if (mag == 0) return sqrt(pow(p.x - s.x, 2) + pow(p.y - s.y, 2));
    return abs(dx * (s.y - p.y) - dy * (s.x - p.x)) / mag;
}

// =====================================================================================
// RECONHECIMENTO DE AERONAVES — vários tipos, qualquer rotação
//
//  1) Otsu separa "tinta" (escuro) de "papel" (claro).
//  2) Escolhe a MAIOR mancha escura que não encosta na borda do quadro.
//  3) Procura o EIXO DE SIMETRIA da mancha (todo avião visto de cima é simétrico):
//     testa ângulos de 0° a 180° e fica com o que deixa os dois lados mais iguais.
//  4) Ao longo desse eixo, mede o PERFIL DE LARGURA (24 fatias do nariz à cauda),
//     nos dois sentidos (não se sabe de antemão qual ponta é o nariz).
//  5) Compara o perfil com os 7 modelos de AircraftTemplates.h e com todas as
//     MISTURAS entre pares de modelos (ex.: 70% caça + 30% delta). Isso cobre os
//     "intermediários": aviões que não são exatamente nenhum dos modelos.
//  6) É AVIAO se for simétrico o bastante e estiver perto o bastante de algum
//     modelo ou mistura.
// Testa as DUAS polaridades: primeiro silhueta ESCURA sobre fundo claro; se não
// passar, inverte as cores e testa silhueta CLARA sobre fundo escuro (avião branco).
// Se qualquer uma passar, é AVIAO.
// =====================================================================================

struct AircraftFeatures {
    int reason;            // 0 = aceito; >0 = motivo da rejeição
    int otsu, dark, light;
    int area, minx, miny, bw, bh;
    float cx, cy;          // centróide da mancha
    float axisDeg;         // direção do eixo nariz->cauda (graus, sentido da imagem)
    float noseX, noseY, tailX, tailY;
    float asym;            // assimetria no melhor eixo
    float aspect;          // envergadura / comprimento
    float dist;            // distância ao melhor modelo/mistura
    int tA, tB;            // modelos da melhor mistura (tB = -1 se for um modelo só)
    float alpha;           // fração de tB na mistura
    int verts;             // vértices do polígono simplificado (só para exibição)
    bool inv;              // true = resultado da imagem invertida (silhueta clara)
    float profile[AC_PROFILE_BINS];   // perfil medido (normalizado)
    float match[AC_PROFILE_BINS];     // perfil do modelo/mistura escolhido
};
static AircraftFeatures acLast;

// ---- Contadores do último quadro analisado (só para a linha de Serial) ----
static uint32_t  dbgFrameId = 0;
static float     dbgMs      = 0;
static bool      dbgAviao   = false;

static uint8_t*  acMask  = NULL;
static uint32_t* acQueue = NULL;
static int       acCap   = 0;

// Pixels amostrados da mancha escolhida, relativos ao centróide
static int16_t*  acPx = NULL;
static int16_t*  acPy = NULL;
static int       acPn = 0;

struct AcBlob {
    int count, minx, maxx, miny, maxy, seedX, seedY;
    long sx, sy;
};


static bool acEnsureBuffers(int n) {
    if (!acPx) {
        acPx = (int16_t*)ps_malloc(AC_MAX_SAMPLES * 2 * sizeof(int16_t));
        acPy = (int16_t*)ps_malloc(AC_MAX_SAMPLES * 2 * sizeof(int16_t));
        if (!acPx || !acPy) { free(acPx); free(acPy); acPx = acPy = NULL; return false; }
    }
    if (acCap >= n) return true;
    free(acMask);
    free(acQueue);
    acMask  = (uint8_t*)ps_malloc(n);
    acQueue = (uint32_t*)ps_malloc((size_t)n * sizeof(uint32_t));
    if (!acMask || !acQueue) {
        free(acMask);
        free(acQueue);
        acMask = NULL;
        acQueue = NULL;
        acCap = 0;
        return false;
    }
    acCap = n;
    return true;
}

static bool acOtsu(const uint8_t* buf, int n, int &thr, int &mDark, int &mLight) {
    uint32_t hist[256];
    memset(hist, 0, sizeof(hist));
    for (int i = 0; i < n; i++) hist[buf[i]]++;

    double sum = 0;
    for (int t = 0; t < 256; t++) sum += (double)t * hist[t];

    double sumB = 0, wB = 0, best = -1;
    thr = 0; mDark = 0; mLight = 0;
    for (int t = 0; t < 256; t++) {
        wB += hist[t];
        if (wB == 0) continue;
        double wF = n - wB;
        if (wF == 0) break;
        sumB += (double)t * hist[t];
        double mB = sumB / wB;
        double mF = (sum - sumB) / wF;
        double between = wB * wF * (mB - mF) * (mB - mF);
        if (between > best) {
            best = between;
            thr = t;
            mDark = (int)mB;
            mLight = (int)mF;
        }
    }
    return best >= 0;
}

// Preenche (8-conectado) a mancha que contém startIdx, trocando fromV por toV
static void acFill(int startIdx, int w, int h, AcBlob &b, uint8_t fromV, uint8_t toV) {
    int head = 0, tail = 0;
    acQueue[tail++] = (uint32_t)startIdx;
    acMask[startIdx] = toV;
    b.count = 0;
    b.minx = w; b.maxx = -1; b.miny = h; b.maxy = -1;
    b.sx = 0; b.sy = 0;
    b.seedX = startIdx % w;
    b.seedY = startIdx / w;

    while (head < tail) {
        int idx = (int)acQueue[head++];
        int x = idx % w, y = idx / w;
        b.count++;
        b.sx += x;
        b.sy += y;
        if (x < b.minx) b.minx = x;
        if (x > b.maxx) b.maxx = x;
        if (y < b.miny) b.miny = y;
        if (y > b.maxy) b.maxy = y;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                int ni = ny * w + nx;
                if (acMask[ni] == fromV) {
                    acMask[ni] = toV;
                    acQueue[tail++] = (uint32_t)ni;
                }
            }
        }
    }
}


// -------------------------------------------------------------------------------------
// Simetria: quão diferentes são os dois lados da mancha em relação a um eixo que passa
// pelo centróide com ângulo 'deg'. Divide a mancha em fatias ao longo do eixo e, em
// cada fatia, mede o desequilíbrio de massa entre esquerda e direita. 0 = espelho perfeito.
// -------------------------------------------------------------------------------------
static float acAsymmetry(float deg, float R) {
    float rad = deg * 0.01745329f;
    float ux = cosf(rad), uy = sinf(rad);
    float sumV[AC_SYM_BINS];
    memset(sumV, 0, sizeof(sumV));
    float scale = AC_SYM_BINS / (2.0f * R);
    for (int i = 0; i < acPn; i++) {
        float x = acPx[i], y = acPy[i];
        float u = x * ux + y * uy;          // ao longo do eixo
        float v = -x * uy + y * ux;         // perpendicular ao eixo
        int k = (int)((u + R) * scale);
        if (k < 0) k = 0;
        if (k >= AC_SYM_BINS) k = AC_SYM_BINS - 1;
        sumV[k] += v;
    }
    float tot = 0;
    for (int k = 0; k < AC_SYM_BINS; k++) tot += fabsf(sumV[k]);
    return tot / (acPn * R);
}

// Perfil de largura ao longo do eixo 'deg' (do lado u mínimo ao u máximo), normalizado
// pela largura máxima. Também devolve o comprimento L, a envergadura W e os extremos.
static void acProfile(float deg, int stride, float* prof, float &L, float &Wsp,
                      float &uMin, float &uMax) {
    float rad = deg * 0.01745329f;
    float ux = cosf(rad), uy = sinf(rad);
    uMin = 1e9f; uMax = -1e9f;
    float vMin = 1e9f, vMax = -1e9f;
    for (int i = 0; i < acPn; i++) {
        float x = acPx[i], y = acPy[i];
        float u = x * ux + y * uy, v = -x * uy + y * ux;
        if (u < uMin) uMin = u;
        if (u > uMax) uMax = u;
        if (v < vMin) vMin = v;
        if (v > vMax) vMax = v;
    }
    L   = uMax - uMin + stride;
    Wsp = vMax - vMin + stride;
    // Cada pixel ocupa um trecho de comprimento 'stride' ao longo do eixo; ele é dividido
    // entre as fatias que cobre, proporcional à sobreposição. Sem isso, quando o eixo fica
    // alinhado com a grade de pixels, algumas fatias ganham uma coluna inteira a mais
    // e o perfil vira uma "escadinha".
    float cnt[AC_PROFILE_BINS];
    memset(cnt, 0, sizeof(cnt));
    float binLen = L / AC_PROFILE_BINS;
    for (int i = 0; i < acPn; i++) {
        float u = acPx[i] * ux + acPy[i] * uy;
        float a = u - uMin;                     // início do trecho do pixel
        float b = a + stride;                   // fim do trecho
        int k = (int)(a / binLen);
        if (k < 0) k = 0;
        while (a < b && k < AC_PROFILE_BINS) {
            float edge = (k + 1) * binLen;
            float seg = ((b < edge) ? b : edge) - a;
            if (seg > 0) cnt[k] += seg / stride;
            a = edge;
            k++;
        }
    }
    float mx = 0;
    for (int k = 0; k < AC_PROFILE_BINS; k++) if (cnt[k] > mx) mx = cnt[k];
    for (int k = 0; k < AC_PROFILE_BINS; k++) prof[k] = (mx > 0) ? cnt[k] / mx : 0;
}

// Distância entre um perfil medido e a mistura (1-a)*A + a*B dos modelos
static float acBlendDist(const float* p, float logAsp, int A, int B, float a) {
    const AircraftTemplate &tA = AC_TEMPLATES[A];
    const AircraftTemplate &tB = AC_TEMPLATES[B];
    float se = 0;
    for (int k = 0; k < AC_PROFILE_BINS; k++) {
        float m = (1 - a) * tA.profile[k] + a * tB.profile[k];
        float d = p[k] - m;
        se += d * d;
    }
    float rms = sqrtf(se / AC_PROFILE_BINS);
    float tAsp = (1 - a) * logf(tA.aspect) + a * logf(tB.aspect);
    return rms + AC_ASPECT_WEIGHT * fabsf(logAsp - tAsp);
}

// Procura o melhor modelo ou mistura de dois modelos para o perfil p
static float acBestMatch(const float* p, float aspect, int &bestA, int &bestB, float &bestAlpha) {
    float logAsp = logf(aspect);
    float best = 1e9f;
    bestA = 0; bestB = -1; bestAlpha = 0;
    for (int A = 0; A < AC_NUM_TEMPLATES; A++) {
        float d = acBlendDist(p, logAsp, A, A, 0);
        if (d < best) { best = d; bestA = A; bestB = -1; bestAlpha = 0; }
        for (int B = A + 1; B < AC_NUM_TEMPLATES; B++) {
            for (int s = 1; s < AC_BLEND_STEPS; s++) {
                float a = (float)s / AC_BLEND_STEPS;
                float dd = acBlendDist(p, logAsp, A, B, a);
                if (dd < best) { best = dd; bestA = A; bestB = B; bestAlpha = a; }
            }
        }
    }
    return best;
}

// inv = false: procura mancha ESCURA; inv = true: cores invertidas, procura mancha CLARA
static String identifyAircraftCore(uint8_t* buf, int w, int h, bool inv) {
    memset(&acLast, 0, sizeof(acLast));
    acLast.tB = -1;
    acLast.inv = inv;
    acPn = 0;
    if (!buf || !contour || w < 32 || h < 32) { acLast.reason = 1; return "NENHUM"; }
    int n = w * h;
    if (!acEnsureBuffers(n)) { acLast.reason = 1; return "NENHUM"; }

    // 1) Limiar de Otsu: exige contraste real entre "tinta" e "papel"
    int thr, mDark, mLight;
    if (!acOtsu(buf, n, thr, mDark, mLight)) { acLast.reason = 2; return "NENHUM"; }
    acLast.otsu = thr; acLast.dark = mDark; acLast.light = mLight;
    if (mLight - mDark < AC_CONTRAST_MIN) { acLast.reason = 2; return "NENHUM"; }

    if (!inv) for (int i = 0; i < n; i++) acMask[i] = (buf[i] <= thr) ? 1 : 0;
    else      for (int i = 0; i < n; i++) acMask[i] = (buf[i] >  thr) ? 1 : 0;

    // 2) Maior mancha escura que NÃO encosta na borda do quadro
    AcBlob best;
    best.count = 0;
    for (int i = 0; i < n; i++) {
        if (acMask[i] != 1) continue;
        AcBlob b;
        acFill(i, w, h, b, 1, 2);
        if (b.minx <= 1 || b.miny <= 1 || b.maxx >= w - 2 || b.maxy >= h - 2) continue;
        if (b.count > best.count) best = b;
    }
    if (best.count == 0) { acLast.reason = 3; return "NENHUM"; }

    int bw = best.maxx - best.minx + 1;
    int bh = best.maxy - best.miny + 1;
    acLast.area = best.count; acLast.bw = bw; acLast.bh = bh;
    acLast.minx = best.minx; acLast.miny = best.miny;
    if (best.count < n * AC_AREA_MIN_FRAC || best.count > n * AC_AREA_MAX_FRAC ||
        (bw < AC_SIZE_MIN && bh < AC_SIZE_MIN)) { acLast.reason = 4; return "NENHUM"; }

    // Remarca só a mancha escolhida com 3 (as outras ficam com 2)
    AcBlob tmp;
    acFill(best.seedY * w + best.seedX, w, h, tmp, 2, 3);

    float cx = (float)best.sx / best.count, cy = (float)best.sy / best.count;
    acLast.cx = cx; acLast.cy = cy;


    // 3) Amostra os pixels da mancha (no máximo AC_MAX_SAMPLES) relativos ao centróide
    int stride = 1;
    while ((best.count / (stride * stride)) > AC_MAX_SAMPLES) stride++;
    float R = 1.0f;
    for (int y = best.miny; y <= best.maxy; y += stride) {
        for (int x = best.minx; x <= best.maxx; x += stride) {
            if (acMask[y * w + x] != 3) continue;
            if (acPn >= AC_MAX_SAMPLES * 2) break;
            float dx = x - cx, dy = y - cy;
            acPx[acPn] = (int16_t)lroundf(dx);
            acPy[acPn] = (int16_t)lroundf(dy);
            acPn++;
            float r = sqrtf(dx * dx + dy * dy);
            if (r > R) R = r;
        }
    }
    if (acPn < 50) { acLast.reason = 4; return "NENHUM"; }
    R += 1.0f;

    // 4) Eixo de simetria: busca grossa em 0..180°, depois refina os 2 melhores mínimos
    const int NC = 180 / AC_COARSE_STEP;
    float coarse[NC];
    for (int i = 0; i < NC; i++) coarse[i] = acAsymmetry(i * AC_COARSE_STEP, R);

    int c1 = 0;
    for (int i = 1; i < NC; i++) if (coarse[i] < coarse[c1]) c1 = i;
    int c2 = -1;
    for (int i = 0; i < NC; i++) {
        int dd = abs(i - c1); if (dd > NC / 2) dd = NC - dd;
        if (dd * AC_COARSE_STEP < 24) continue;                  // longe do 1º mínimo
        float prev = coarse[(i + NC - 1) % NC], next = coarse[(i + 1) % NC];
        if (coarse[i] > prev || coarse[i] > next) continue;      // tem que ser mínimo local
        if (c2 < 0 || coarse[i] < coarse[c2]) c2 = i;
    }

    float candDeg[2]; float candAsym[2]; int nc = 0;
    int seeds[2] = {c1, c2};
    for (int s = 0; s < 2; s++) {
        if (seeds[s] < 0) continue;
        float bestD = seeds[s] * AC_COARSE_STEP, bestA = coarse[seeds[s]];
        for (int d = -AC_COARSE_STEP + 1; d < AC_COARSE_STEP; d++) {
            float deg = seeds[s] * AC_COARSE_STEP + d;
            float a = acAsymmetry(deg, R);
            if (a < bestA) { bestA = a; bestD = deg; }
        }
        candDeg[nc] = bestD; candAsym[nc] = bestA; nc++;
    }

    // 5) Para cada eixo candidato e cada sentido, perfil de largura x modelos/misturas
    float bestDist = 1e9f;
    bool any = false;
    for (int c = 0; c < nc; c++) {
        float prof[AC_PROFILE_BINS], L, Wsp, uMin, uMax;
        acProfile(candDeg[c], stride, prof, L, Wsp, uMin, uMax);
        float aspect = Wsp / L;
        for (int dir = 0; dir < 2; dir++) {
            float p[AC_PROFILE_BINS];
            for (int k = 0; k < AC_PROFILE_BINS; k++) p[k] = dir ? prof[AC_PROFILE_BINS - 1 - k] : prof[k];
            int A, B; float al;
            float d = acBestMatch(p, aspect, A, B, al);
            // Eixo pouco simétrico só ganha se não houver candidato simétrico
            bool symOk = candAsym[c] <= AC_ASYM_MAX;
            bool bestSymOk = any && acLast.asym <= AC_ASYM_MAX;
            if (!any || (symOk && !bestSymOk) || (symOk == bestSymOk && d < bestDist)) {
                any = true;
                bestDist = d;
                acLast.asym = candAsym[c];
                acLast.aspect = aspect;
                acLast.dist = d;
                acLast.tA = A; acLast.tB = B; acLast.alpha = al;
                memcpy(acLast.profile, p, sizeof(p));
                for (int k = 0; k < AC_PROFILE_BINS; k++) {
                    acLast.match[k] = (B < 0) ? AC_TEMPLATES[A].profile[k]
                        : (1 - al) * AC_TEMPLATES[A].profile[k] + al * AC_TEMPLATES[B].profile[k];
                }
                // Direção nariz -> cauda na imagem
                float rad = candDeg[c] * 0.01745329f;
                float ux = cosf(rad), uy = sinf(rad);
                float uN = dir ? uMax : uMin, uT = dir ? uMin : uMax;
                acLast.noseX = cx + uN * ux; acLast.noseY = cy + uN * uy;
                acLast.tailX = cx + uT * ux; acLast.tailY = cy + uT * uy;
                float deg = candDeg[c] + (dir ? 180.0f : 0.0f);
                acLast.axisDeg = fmodf(deg, 360.0f);
            }
        }
    }

    // 6) Decisão
    if (acLast.asym > AC_ASYM_MAX) { acLast.reason = 6; return "NENHUM"; }
    if (acLast.dist > AC_DIST_MAX) { acLast.reason = 7; return "NENHUM"; }
    return "AVIAO";
}

// =====================================================================================
// Ponto de entrada público: guarda uma cópia do quadro (para o /live), roda o
// classificador e mede o tempo de processamento.
// =====================================================================================
// Resumo da OUTRA polaridade (a que não está sendo mostrada no /live), para o
// operador ver as duas tentativas quando nenhuma passou.
struct AcAltSummary {
    bool valid;
    bool inv;
    int reason;
    float dist, asym;
    String model;
};
static AcAltSummary acAlt;

static String acMatchText();   // definida mais abaixo

static void acFillAlt(const AircraftFeatures &f) {
    acAlt.valid = true;
    acAlt.inv = f.inv;
    acAlt.reason = f.reason;
    acAlt.dist = f.dist;
    acAlt.asym = f.asym;
    acAlt.model = (f.reason >= 6) ? acMatchText() : String("");
}


String identifyAircraft(uint8_t* buf, int w, int h) {
    unsigned long t0 = micros();
    acAlt.valid = false;

    // 1ª tentativa: silhueta escura
    String result = identifyAircraftCore(buf, w, h, false);

    // 2ª tentativa: cores invertidas (avião branco/claro sobre fundo escuro).
    // Se o problema foi contraste ou erro interno, inverter não muda nada: pula.
    if (result != "AVIAO" && acLast.reason > 2) {
        AircraftFeatures darkF = acLast;

        result = identifyAircraftCore(buf, w, h, true);

        // Nenhuma passou: guarda a tentativa ESCURA (a normal), a não ser que ela não
        // tenha achado mancha nenhuma (motivo 3). A outra vai como resumo em acAlt.
        if (result != "AVIAO") {
            if (darkF.reason != 3) {
                acFillAlt(acLast);              // guarda o resumo da clara...
                acLast = darkF;                 // ...e volta para a escura
            } else {
                AircraftFeatures lightF = acLast;
                acLast = darkF;
                acFillAlt(acLast);              // resumo da escura (motivo 3)
                acLast = lightF;                // fica a clara
            }
        }
    }
    dbgMs = (micros() - t0) / 1000.0f;
    dbgFrameId++;
    dbgAviao = (result == "AVIAO");
    return result;
}

// =====================================================================================
// DEPURAÇÃO: textos, linha de Serial, JSON e JPEG para o painel /live
// =====================================================================================
const char* visionReasonText(int reason) {
    switch (reason) {
        case 0:  return "aviao reconhecido";
        case 1:  return "erro interno (PSRAM/buffer ou quadro invalido)";
        case 2:  return "pouco contraste entre silhueta e fundo";
        case 3:  return "nenhuma mancha escura isolada (tudo encosta na borda?)";
        case 4:  return "mancha muito pequena ou muito grande";
        case 6:  return "silhueta nao e simetrica";
        case 7:  return "forma nao parece com nenhum modelo de aviao";
    }
    return "?";
}

const char* visionTemplateName(int i) {
    return (i >= 0 && i < AC_NUM_TEMPLATES) ? AC_TEMPLATES[i].name : "-";
}

// Ex.: "Caca 70% + Delta 30%"  ou  "Comercial"
static String acMatchText() {
    const AircraftFeatures &f = acLast;
    if (f.tB < 0 || f.alpha <= 0.0f) return String(visionTemplateName(f.tA));
    int pa = (int)lroundf((1 - f.alpha) * 100), pb = 100 - pa;
    // Mistura com até 10% de outro modelo: mostra só o modelo dominante
    if (pa >= 90) return String(visionTemplateName(f.tA));
    if (pb >= 90) return String(visionTemplateName(f.tB));
    String a = String(visionTemplateName(f.tA)) + " " + String(pa) + "%";
    String b = String(visionTemplateName(f.tB)) + " " + String(pb) + "%";
    return (pa >= pb) ? a + " + " + b : b + " + " + a;
}

String visionLastModel() {
    return acMatchText();
}

String visionDebugLine() {
    const AircraftFeatures &f = acLast;
    char det[96] = "";
    switch (f.reason) {
        case 2: snprintf(det, sizeof(det), "contraste %d, minimo %d", f.light - f.dark, AC_CONTRAST_MIN); break;
        case 4: snprintf(det, sizeof(det), "area %d px, caixa %dx%d", f.area, f.bw, f.bh); break;
        case 6: snprintf(det, sizeof(det), "assimetria %.3f, maximo %.3f", f.asym, AC_ASYM_MAX); break;
        case 7: snprintf(det, sizeof(det), "distancia %.3f, maximo %.3f; mais perto: %s",
                         f.dist, AC_DIST_MAX, acMatchText().c_str()); break;
    }

    char line[240];
    if (f.reason == 0) {
        snprintf(line, sizeof(line), "[VISAO #%lu] AVIAO detectado (silhueta %s): %s | dist %.3f, eixo %.0f graus | %.0f ms",
                 (unsigned long)dbgFrameId, f.inv ? "clara" : "escura", acMatchText().c_str(), f.dist, f.axisDeg, dbgMs);
    } else {
        char alt[96] = "";
        if (acAlt.valid) snprintf(alt, sizeof(alt), " | %s: motivo %d", acAlt.inv ? "invertida" : "escura", acAlt.reason);
        snprintf(line, sizeof(line), "[VISAO #%lu] NENHUM | %s: motivo %d: %s%s%s%s%s | %.0f ms",
                 (unsigned long)dbgFrameId, f.inv ? "invertida" : "escura", f.reason, visionReasonText(f.reason),
                 det[0] ? " (" : "", det, det[0] ? ")" : "", alt, dbgMs);
    }
    return String(line);
}
