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
 * CONTEXTO: Motor de processamento de imagens focado na detecção geométrica.
 *           Realiza a classificação determinística de triângulos
 *           exclusivamente através do cálculo e da verificação de ângulos.
 **************************************************************************************************/

#include "VisionSystem.h"
#include <math.h>

// Definição das variáveis globais
Point *contour = NULL;
int contourSize = 0;

void initVisionBuffers() {
    // Alocação na PSRAM para evitar estouro de memória interna
    // (função original do VANTsat_TX_V3 — reaproveitada sem alteração)
    contour = (Point *)ps_malloc(2000 * sizeof(Point));
}

// Função original do VANTsat_TX_V3 (parte do algoritmo RDP) — reaproveitada
// sem alteração porque o detector de aviões também usa perpendicularDistance()
// para simplificar o próprio contorno (ver acRdp()/acPolygon() mais abaixo).
// NÃO MUDE NADA AQUI!
float perpendicularDistance(Point p, Point s, Point e) {
    float dx = e.x - s.x;
    float dy = e.y - s.y;
    float mag = sqrt(dx * dx + dy * dy);
    if (mag == 0) return sqrt(pow(p.x - s.x, 2) + pow(p.y - s.y, 2));
    return abs(dx * (s.y - p.y) - dy * (s.x - p.x)) / mag;
}

// =====================================================================================
// RECONHECIMENTO DE AERONAVES (silhueta de asa delta / UCAV)
//
// Esta branch remove o caminho de TRIANGULO/QUADRADO do VANTsat_TX_V3
// (findContour, drawLine, simplifyContour, calculateCentroid, identifyShape —
// ver a branch/main para esse código) e mantém só o que o detector de avião
// realmente usa: o buffer contour[] (initVisionBuffers) e perpendicularDistance()
// acima, ambos herdados sem alteração do VANTsat_TX_V3. Todo o resto abaixo
// (Otsu, blob-fill, acTrace, acPolygon, acHullArea, identifyAircraft) é código
// novo, escrito especificamente para reconhecer aeronaves. Principais diferenças
// em relação ao algoritmo de triângulo que foi removido:
//  - limiar de Otsu (se adapta ao contraste/luz do quadro, em vez de frações fixas da média);
//  - escolhe a MAIOR mancha escura que não encosta na borda (não depende de a mancha
//    passar pelo centro do quadro);
//  - contorno Moore-Neighbor com backtrack em pixel de FUNDO (não degenera em laço de 12
//    pontos quando a borda está desfocada);
//  - classifica pela forma global (nariz agudo, asas largas atrás, quase convexa,
//    simétrica), e não pelos ângulos da aresta mais baixa.
// Assume, como o código original, silhueta ESCURA sobre fundo CLARO e nariz para cima.
// =====================================================================================

struct AircraftFeatures {
    int reason;        // 0 = aceito; >0 = motivo da rejeição (só para depuração)
    int otsu, dark, light;
    int area, bw, bh, verts;
    float fill, solidity, apexDeg, topFrac, widestFrac, cyFrac, asym, aspect;
};
static AircraftFeatures acLast;

static uint8_t*  acMask  = NULL;
static uint32_t* acQueue = NULL;
static int       acCap   = 0;

struct AcBlob {
    int count, minx, maxx, miny, maxy, seedX, seedY;
    long sx, sy;
};

static const int8_t AC_DX[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
static const int8_t AC_DY[8] = {-1, -1, -1, 0, 1, 1, 1, 0};

static bool acEnsureBuffers(int n) {
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

// Preenche (8-conectado) a mancha que contém startIdx, marcando pixels visitados com 2
static void acFill(int startIdx, int w, int h, AcBlob &b) {
    int head = 0, tail = 0;
    acQueue[tail++] = (uint32_t)startIdx;
    acMask[startIdx] = 2;
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
                if (acMask[ni] == 1) {
                    acMask[ni] = 2;
                    acQueue[tail++] = (uint32_t)ni;
                }
            }
        }
    }
}

static int acDirIndex(int dx, int dy) {
    for (int d = 0; d < 8; d++) {
        if (AC_DX[d] == dx && AC_DY[d] == dy) return d;
    }
    return 7;
}

// Contorno externo da mancha que começa em (sx,sy) — 1º pixel dela em ordem raster,
// portanto o vizinho oeste é fundo. Grava em contour[]/contourSize.
static void acTrace(int w, int h, int sx, int sy) {
    contourSize = 0;
    int cx = sx, cy = sy, back = 7;
    for (int step = 0; step < 6000; step++) {
        if (contourSize < 1990) contour[contourSize++] = {cx, cy};
        int found = -1;
        for (int k = 1; k <= 8; k++) {
            int d = (back + k) & 7;
            int nx = cx + AC_DX[d], ny = cy + AC_DY[d];
            if (nx >= 0 && nx < w && ny >= 0 && ny < h && acMask[ny * w + nx]) { found = d; break; }
        }
        if (found < 0) break;
        int pd = (found + 7) & 7;
        int bx = cx + AC_DX[pd], by = cy + AC_DY[pd];
        cx += AC_DX[found];
        cy += AC_DY[found];
        back = acDirIndex(bx - cx, by - cy);
        if (cx == sx && cy == sy) break;
    }
}

static void acRdp(int i0, int i1, float eps, int n, bool* keep) {
    if (i1 - i0 < 2) return;
    Point a = contour[i0 % n], b = contour[i1 % n];
    float maxD = 0;
    int idx = -1;
    for (int i = i0 + 1; i < i1; i++) {
        float d = perpendicularDistance(contour[i % n], a, b);
        if (d > maxD) { maxD = d; idx = i; }
    }
    if (idx >= 0 && maxD > eps) {
        keep[idx % n] = true;
        acRdp(i0, idx, eps, n, keep);
        acRdp(idx, i1, eps, n, keep);
    }
}

// Douglas-Peucker para contorno FECHADO (divide no ponto mais distante da semente)
static int acPolygon(float eps, Point* out, int maxOut) {
    int n = contourSize;
    if (n < 8) return 0;
    static bool keep[2000];
    memset(keep, 0, n);
    int far = 0;
    long best = -1;
    for (int i = 1; i < n; i++) {
        long dx = contour[i].x - contour[0].x, dy = contour[i].y - contour[0].y;
        long d = dx * dx + dy * dy;
        if (d > best) { best = d; far = i; }
    }
    keep[0] = true;
    keep[far] = true;
    acRdp(0, far, eps, n, keep);
    acRdp(far, n, eps, n, keep);
    int m = 0;
    for (int i = 0; i < n && m < maxOut; i++) {
        if (keep[i]) out[m++] = contour[i];
    }
    return m;
}

static long acCross(Point o, Point a, Point b) {
    return (long)(a.x - o.x) * (b.y - o.y) - (long)(a.y - o.y) * (b.x - o.x);
}

static float acHullArea(const Point* p, int m) {
    if (m < 3) return 0;
    Point s[48];
    if (m > 48) m = 48;
    for (int i = 0; i < m; i++) s[i] = p[i];
    for (int i = 1; i < m; i++) {
        Point v = s[i];
        int j = i - 1;
        while (j >= 0 && (s[j].x > v.x || (s[j].x == v.x && s[j].y > v.y))) { s[j + 1] = s[j]; j--; }
        s[j + 1] = v;
    }
    Point hull[100];
    int k = 0;
    for (int i = 0; i < m; i++) {
        while (k >= 2 && acCross(hull[k - 2], hull[k - 1], s[i]) <= 0) k--;
        hull[k++] = s[i];
    }
    for (int i = m - 2, t = k + 1; i >= 0; i--) {
        while (k >= t && acCross(hull[k - 2], hull[k - 1], s[i]) <= 0) k--;
        hull[k++] = s[i];
    }
    k--;
    if (k < 3) return 0;
    double area = 0;
    for (int i = 0; i < k; i++) {
        int j = (i + 1) % k;
        area += (double)hull[i].x * hull[j].y - (double)hull[j].x * hull[i].y;
    }
    return (float)(fabs(area) / 2.0);
}

String identifyAircraft(uint8_t* buf, int w, int h) {
    memset(&acLast, 0, sizeof(acLast));
    if (!buf || !contour || w < 32 || h < 32 || h > 480) { acLast.reason = 1; return "NENHUM"; }
    int n = w * h;
    if (!acEnsureBuffers(n)) { acLast.reason = 1; return "NENHUM"; }

    // 1) Limiar de Otsu: exige contraste real entre "tinta" e "papel"
    int thr, mDark, mLight;
    if (!acOtsu(buf, n, thr, mDark, mLight)) { acLast.reason = 2; return "NENHUM"; }
    acLast.otsu = thr; acLast.dark = mDark; acLast.light = mLight;
    if (mLight - mDark < 40) { acLast.reason = 2; return "NENHUM"; }

    for (int i = 0; i < n; i++) acMask[i] = (buf[i] <= thr) ? 1 : 0;

    // 2) Maior mancha escura que NÃO encosta na borda do quadro
    AcBlob best;
    best.count = 0;
    for (int i = 0; i < n; i++) {
        if (acMask[i] != 1) continue;
        AcBlob b;
        acFill(i, w, h, b);
        if (b.minx <= 1 || b.miny <= 1 || b.maxx >= w - 2 || b.maxy >= h - 2) continue;
        if (b.count > best.count) best = b;
    }
    if (best.count == 0) { acLast.reason = 3; return "NENHUM"; }

    int bw = best.maxx - best.minx + 1;
    int bh = best.maxy - best.miny + 1;
    acLast.area = best.count; acLast.bw = bw; acLast.bh = bh;
    if (best.count < n * 0.02f || best.count > n * 0.60f || bh < 30 || bw < 30) { acLast.reason = 4; return "NENHUM"; }

    // 3) Contorno externo + larguras por linha
    acTrace(w, h, best.seedX, best.seedY);
    if (contourSize < 60 || contourSize >= 1990) { acLast.reason = 5; return "NENHUM"; }

    static int16_t lo[480], hi[480];
    for (int y = best.miny; y <= best.maxy; y++) { lo[y] = 32767; hi[y] = -1; }
    for (int i = 0; i < contourSize; i++) {
        int x = contour[i].x, y = contour[i].y;
        if (x < lo[y]) lo[y] = (int16_t)x;
        if (x > hi[y]) hi[y] = (int16_t)x;
    }
    #define AC_WIDTH(y) ((hi[y] >= 0) ? (hi[y] - lo[y] + 1) : 0)

    int r0 = best.miny + (int)(0.04f * bh);
    int r1 = best.miny + (int)(0.12f * bh);
    int r2 = best.miny + (int)(0.32f * bh);
    float g = (float)(AC_WIDTH(r2) - AC_WIDTH(r1)) / (float)(r2 - r1);
    acLast.apexDeg = 2.0f * atan(g / 2.0f) * 57.29578f;
    acLast.topFrac = (float)AC_WIDTH(r0) / bw;

    int maxW = 0;
    for (int y = best.miny; y <= best.maxy; y++) if (AC_WIDTH(y) > maxW) maxW = AC_WIDTH(y);
    int yWide = best.miny;
    for (int y = best.miny; y <= best.maxy; y++) { if (AC_WIDTH(y) >= 0.97f * maxW) { yWide = y; break; } }
    acLast.widestFrac = (float)(yWide - best.miny) / bh;

    float cxm = (float)best.sx / best.count, cym = (float)best.sy / best.count;
    acLast.cyFrac = (cym - best.miny) / bh;
    acLast.asym = fabsf(cxm - (best.minx + best.maxx) / 2.0f) / bw;
    acLast.aspect = (float)bw / bh;
    acLast.fill = (float)best.count / ((float)bw * bh);

    // 4) Polígono simplificado + solidez (área / casco convexo)
    float longSide = (float)(bw > bh ? bw : bh);
    float eps = 0.03f * longSide;
    if (eps < 3.0f) eps = 3.0f;
    Point poly[40];
    int m = acPolygon(eps, poly, 40);
    acLast.verts = m;
    float hullArea = acHullArea(poly, m);
    acLast.solidity = (hullArea > 0) ? (float)best.count / hullArea : 0;
    if (acLast.solidity > 1.0f) acLast.solidity = 1.0f;
    #undef AC_WIDTH

    // 5) Decisão: nariz agudo e fino, asas largas na parte de trás, massa concentrada atrás,
    //    quase convexa, simétrica e com "recortes" (>=5 vértices — um triângulo puro fica de fora)
    if (acLast.aspect < 0.60f || acLast.aspect > 2.40f)            { acLast.reason = 6;  return "NENHUM"; }
    if (acLast.fill < 0.30f || acLast.fill > 0.75f)                { acLast.reason = 7;  return "NENHUM"; }
    if (acLast.solidity < 0.65f)                                   { acLast.reason = 8;  return "NENHUM"; }
    if (acLast.apexDeg < 30.0f || acLast.apexDeg > 100.0f)         { acLast.reason = 9;  return "NENHUM"; }
    if (acLast.topFrac > 0.15f)                                    { acLast.reason = 10; return "NENHUM"; }
    if (acLast.widestFrac < 0.55f)                                 { acLast.reason = 11; return "NENHUM"; }
    if (acLast.cyFrac < 0.50f || acLast.cyFrac > 0.80f)            { acLast.reason = 12; return "NENHUM"; }
    if (acLast.asym > 0.10f)                                       { acLast.reason = 13; return "NENHUM"; }
    if (m < 5 || m > 16)                                           { acLast.reason = 14; return "NENHUM"; }
    return "AVIAO";
}