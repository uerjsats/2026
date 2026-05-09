#include "VisionSystem.h"
#include "SerialBridge.h"
#include <math.h>

// Definição das variáveis globais
Point *contour = NULL;
Point vertices[40];
int contourSize = 0;
int vertexCount = 0;
unsigned long lastImageTime = 0;

void initVisionBuffers() {
    // Alocação na PSRAM para evitar estouro de memória interna
    contour = (Point *)ps_malloc(2000 * sizeof(Point));
}

String processVisionFrame(camera_fb_t * fb) {
    if (!fb) return "ERRO";
    String detectedType = "NENHUM";

    // Executa a busca de contornos no buffer da câmera
    findContour(fb->buf, fb->width, fb->height);
    
    if (contourSize > 300) {
        simplifyContour(18.0); // Algoritmo RDP
        int centroX, centroY;
        calculateCentroid(&centroX, &centroY);

        int erroX = centroX - 160; 
        int erroY = centroY - 120; 

        detectedType = identifyShape(centroX, centroY, erroX, erroY);

        // Desenha o contorno visual no buffer para debug
        if (contourSize > 1) {
            for (int i = 0; i < contourSize - 1; i++) {
                drawLine(fb->buf, fb->width, fb->height, contour[i].x, contour[i].y, contour[i+1].x, contour[i+1].y);
            }
        }
    }
    esp_camera_fb_return(fb);
    return detectedType;
}

// Implementação do Moore-Neighbor 
void findContour(uint8_t* buf, int w, int h) {
    contourSize = 0;
    Point start = {-1, -1};
    
    // Cálculo de brilho adaptativo
    long somaBrilho = 0;
    int amostras = 0;
    for (int i = 0; i < (w * h); i += 20) {
        somaBrilho += buf[i];
        amostras++;
    }
    int brilhoMedio = somaBrilho / amostras;
    int thrBusca = (brilhoMedio * 0.5); 
    int thrSeguimento = (brilhoMedio * 0.7);

    // Busca o primeiro ponto escuro (objeto)
    for (int y = h/4; y < (3*h)/4 && start.x == -1; y++) {
        for (int x = w/4; x < (3*w)/4; x++) {
            if (buf[y * w + x] < thrBusca) { 
                start = {x, y}; 
                break; 
            }
        }
    }
    
    if (start.x == -1) return;

    Point curr = start;
    Point prev = {start.x - 1, start.y};
    int dirs[8][2] = {{-1,-1}, {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}};

    for (int i = 0; i < 1500; i++) {
        if (contourSize >= 1990) break;
        contour[contourSize++] = curr;
        
        int sDir = 0;
        for(int d=0; d<8; d++) {
            if(curr.x + dirs[d][0] == prev.x && curr.y + dirs[d][1] == prev.y) { sDir = d; break; }
        }

        bool found = false;
        for (int d = 1; d <= 8; d++) {
            int t = (sDir + d) % 8;
            int nx = curr.x + dirs[t][0], ny = curr.y + dirs[t][1];
            if (nx >= 1 && nx < w-1 && ny >= 1 && ny < h-1) {
                if (buf[ny * w + nx] < thrSeguimento) { 
                    prev = curr; curr = {nx, ny}; found = true; break; 
                }
            }
        }
        if (!found || (abs(curr.x - start.x) <= 1 && abs(curr.y - start.y) <= 1 && i > 10)) break;
    }
}
// --- 1. FUNÇÃO DRAWLINE (Algoritmo de Bresenham) ---
void drawLine(uint8_t* buf, int w, int h, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    
    while (true) {
        if(x0 >= 0 && x0 < w - 1 && y0 >= 0 && y0 < h) {
            buf[y0 * w + x0] = 255; // Pinta o pixel de branco
            // Engrossa a linha para facilitar a visualização no Python
            if (x0 + 1 < w) {
                buf[y0 * w + (x0 + 1)] = 255; 
            }
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// --- 2. DISTÂNCIA PERPENDICULAR ---
// Calcula a distância de um ponto P a uma reta definida por S (Início) e E (Fim).
float perpendicularDistance(Point p, Point s, Point e) {
    float dx = e.x - s.x;
    float dy = e.y - s.y;
    float mag = sqrt(dx * dx + dy * dy);
    if (mag == 0) return sqrt(pow(p.x - s.x, 2) + pow(p.y - s.y, 2));
    return abs(dx * (s.y - p.y) - dy * (s.x - p.x)) / mag;
}

// --- 3. SIMPLIFYCONTOUR (Algoritmo RDP) ---
void simplifyContour(float epsilon) {
    if (contourSize < 3) return;

    // Alocação segura na PSRAM para evitar estouro de stack
    bool* keep = (bool*)ps_malloc(contourSize * sizeof(bool));
    if (keep == NULL) return; 

    memset(keep, false, contourSize);

    // Pilha manual para evitar recursão profunda no ESP32
    int* rdpStack = (int*)ps_malloc(100 * sizeof(int));
    if (rdpStack == NULL) {
        free(keep);
        return;
    }

    int top = 0;
    rdpStack[top++] = 0;
    rdpStack[top++] = contourSize - 1;
    keep[0] = true;
    keep[contourSize - 1] = true;

    while (top > 0) {
        int end = rdpStack[--top];
        int start = rdpStack[--top];

        float maxDist = 0;
        int index = start;

        for (int i = start + 1; i < end; i++) {
            float d = perpendicularDistance(contour[i], contour[start], contour[end]);
            if (d > maxDist) {
                index = i;
                maxDist = d;
            }
        }

        if (maxDist > epsilon) {
            keep[index] = true;
            if (top + 4 < 100) {
                rdpStack[top++] = start;
                rdpStack[top++] = index;
                rdpStack[top++] = index;
                rdpStack[top++] = end;
            }
        }
    }

    vertexCount = 0;
    for (int i = 0; i < contourSize; i++) {
        if (keep[i] && vertexCount < 39) { 
            vertices[vertexCount++] = contour[i];
        }
    }
    
    free(keep);
    free(rdpStack);
}

// --- 4. CÁLCULO DE CENTROIDE ---
// Localiza o centro geométrico da forma para fins de cálculo de erro de navegação.
void calculateCentroid(int *cX, int *cY) {
    if (contourSize <= 0 || contour == NULL) {
        *cX = 0; *cY = 0;
        return;
    }

    long sumX = 0, sumY = 0;
    for(int i = 0; i < contourSize; i++) {
        sumX += contour[i].x;
        sumY += contour[i].y;
    }
    *cX = sumX / contourSize;
    *cY = sumY / contourSize;
}

// --- 5. IDENTIFICAÇÃO DE FORMA ---
// Classifica o objeto como TRIANGULO ou QUADRADO com base nos ângulos
String identifyShape(int cX, int cY, int errX, int errY) {
    if (vertexCount < 3) return "DESCONHECIDO";

    // 1. Encontrar a aresta da base 
    // Procura a aresta com o maior valor médio de Y (mais abaixo na imagem)
    int base1 = 0, base2 = 1;
    double maxAvgY = -1e9;

    for (int i = 0; i < vertexCount; i++) {
        int j = (i + 1) % vertexCount;
        double avgY = (vertices[i].y + vertices[j].y) / 2.0;
        if (avgY > maxAvgY) {
            maxAvgY = avgY;
            base1 = i;
            base2 = j;
        }
    }

    // 2. Identificar vértices adjacentes aos pontos da base para criar os vetores
    int prev_idx = (base1 - 1 + vertexCount) % vertexCount;
    int next_idx = (base2 + 1) % vertexCount;

    // Vetores originados no vértice base1
    double v1x = vertices[base2].x - vertices[base1].x;
    double v1y = vertices[base2].y - vertices[base1].y;
    double v2x = vertices[prev_idx].x - vertices[base1].x;
    double v2y = vertices[prev_idx].y - vertices[base1].y;

    // Vetores originados no vértice base2
    double v3x = vertices[base1].x - vertices[base2].x;
    double v3y = vertices[base1].y - vertices[base2].y;
    double v4x = vertices[next_idx].x - vertices[base2].x;
    double v4y = vertices[next_idx].y - vertices[base2].y;

    // 3. Lambda para cálculo do ângulo interno em graus via Produto Escalar
    auto calcAngle = [](double xA, double yA, double xB, double yB) {
        double dot = xA * xB + yA * yB;
        double magA = sqrt(xA * xA + yA * yA);
        double magB = sqrt(xB * xB + yB * yB);
        if (magA == 0 || magB == 0) return 0.0;
        
        double cosTheta = dot / (magA * magB);
        // Clamp para evitar NaN devido a flutuações de precisão do float
        if (cosTheta < -1.0) cosTheta = -1.0;
        if (cosTheta > 1.0) cosTheta = 1.0;
        
        return acos(cosTheta) * (180.0 / acos(-1.0));
    };

    double angle1 = calcAngle(v1x, v1y, v2x, v2y);
    double angle2 = calcAngle(v3x, v3y, v4x, v4y);

    // 4. Classificação com tolerância para distorção de perspectiva/ruído
    if (angle1 >= 80.0 && angle1 <= 100.0 && angle2 >= 80.0 && angle2 <= 100.0) {
        return "QUADRADO";
    } else if (angle1 < 80.0 && angle2 < 80.0) {
        return "TRIANGULO";
    }

    return "DESCONHECIDO";
}