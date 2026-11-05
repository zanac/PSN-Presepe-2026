// Compressore gzip minimo (deflate: LZ77 + un blocco Huffman dinamico).
// Usato dall'ESP8266 per mandare il file INI compresso: il browser lo decomprime da solo
// (Content-Encoding: gzip). Memoria: tabelle LZ77 (GZ_HASH + GZ_FINESTRA voci da 2 byte) e
// frequenze, tutte allocate qui dentro e liberate alla fine.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef GZ_BIT_HASH
#define GZ_BIT_HASH 10           // 1024 voci (2 kB)
#endif
#ifndef GZ_BIT_FINESTRA
#define GZ_BIT_FINESTRA 12       // catene sugli ultimi 4096 byte (8 kB)
#endif
#ifndef GZ_CATENA
#define GZ_CATENA 48             // candidati provati per posizione
#endif

namespace gz {

struct Uscita {
  uint8_t *p; size_t cap, n; uint32_t acc; uint8_t nb; bool pieno;
  void scrivi(uint32_t v, uint8_t k) {      // k bit, il meno significativo per primo
    acc |= v << nb; nb += k;
    while (nb >= 8) { if (n < cap) p[n++] = (uint8_t)acc; else pieno = true; acc >>= 8; nb -= 8; }
  }
  void ottetto(uint8_t b) { scrivi(b, 8); }
  void chiudi() { if (nb) { if (n < cap) p[n++] = (uint8_t)acc; else pieno = true; acc = 0; nb = 0; } }
};

static const uint16_t BASE_L[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t EXTRA_L[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t BASE_D[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t EXTRA_D[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static const uint8_t ORDINE[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

static uint8_t codiceL(uint16_t l) { uint8_t c = 0; while (c < 28 && BASE_L[c + 1] <= l) c++; return c; }
static uint8_t codiceD(uint16_t d) { uint8_t c = 0; while (c < 29 && BASE_D[c + 1] <= d) c++; return c; }

// Lunghezze di Huffman (al massimo maxLen bit) dalle frequenze. Se l'albero viene troppo
// profondo dimezza le frequenze e riprova (semplice; per questi file basta).
static bool lunghezze(const uint32_t *freqIn, int n, uint8_t maxLen, uint8_t *len) {
  uint32_t *f = (uint32_t *)malloc(n * sizeof(uint32_t));
  int *nodo = (int *)malloc(2 * n * sizeof(int));      // padre di ogni nodo
  uint32_t *peso = (uint32_t *)malloc(2 * n * sizeof(uint32_t));
  int *coda = (int *)malloc(2 * n * sizeof(int));
  if (!f || !nodo || !peso || !coda) { free(f); free(nodo); free(peso); free(coda); return false; }
  memcpy(f, freqIn, n * sizeof(uint32_t));
  bool ok = false;
  for (int giro = 0; giro < 20 && !ok; giro++) {
    memset(len, 0, n);
    int m = 0;
    for (int i = 0; i < n; i++) if (f[i]) { peso[i] = f[i]; coda[m++] = i; }
    if (m == 0) { ok = true; break; }
    if (m == 1) { len[coda[0]] = 1; ok = true; break; }
    int libero = n, vivi = m;
    for (int i = 0; i < 2 * n; i++) nodo[i] = -1;
    while (vivi > 1) {                          // O(m^2): m <= 286, una volta per file
      int a = -1, b = -1;
      for (int i = 0; i < vivi; i++) {
        int x = coda[i];
        if (a < 0 || peso[x] < peso[coda[a]]) { b = a; a = i; }
        else if (b < 0 || peso[x] < peso[coda[b]]) b = i;
      }
      int na = coda[a], nb = coda[b];
      peso[libero] = peso[na] + peso[nb]; nodo[na] = libero; nodo[nb] = libero;
      if (a > b) { int t = a; a = b; b = t; }
      coda[a] = libero; coda[b] = coda[--vivi];
      libero++;
    }
    ok = true;
    for (int i = 0; i < n; i++) if (f[i]) {
      int d = 0; for (int x = i; nodo[x] >= 0; x = nodo[x]) d++;
      if (d > maxLen) { ok = false; break; }
      len[i] = (uint8_t)d;
    }
    if (!ok) for (int i = 0; i < n; i++) if (f[i]) f[i] = (f[i] + 1) / 2;
  }
  free(f); free(nodo); free(peso); free(coda);
  return ok;
}

// Codici canonici (gia' con i bit invertiti, come li vuole deflate)
static void codici(const uint8_t *len, int n, uint16_t *cod) {
  uint16_t conta[16] = {0}, prossimo[16];
  for (int i = 0; i < n; i++) conta[len[i]]++;
  conta[0] = 0; uint16_t c = 0;
  for (int b = 1; b < 16; b++) { c = (c + conta[b - 1]) << 1; prossimo[b] = c; }
  for (int i = 0; i < n; i++) if (len[i]) {
    uint16_t v = prossimo[len[i]]++, r = 0;
    for (int k = 0; k < len[i]; k++) { r = (r << 1) | (v & 1); v >>= 1; }
    cod[i] = r;
  }
}

struct Lz {
  const uint8_t *in; size_t n;
  uint16_t *testa, *prec;               // testa: hash -> posizione+1; prec: catena (finestra circolare)
  static uint16_t hash(const uint8_t *p) { return (uint16_t)(((p[0] << 10) ^ (p[1] << 5) ^ p[2]) & ((1 << GZ_BIT_HASH) - 1)); }
  void inserisci(size_t i) {
    if (i + 2 >= n) return;
    uint16_t h = hash(in + i);
    prec[i & ((1 << GZ_BIT_FINESTRA) - 1)] = testa[h];
    testa[h] = (uint16_t)(i + 1);
  }
  // match piu' lungo che inizia in i (0 se meno di 3)
  uint16_t cerca(size_t i, uint16_t &dist) {
    if (i + 2 >= n) return 0;
    uint16_t migliore = 0, h = hash(in + i);
    size_t lmax = n - i < 258 ? n - i : 258;
    uint32_t c = testa[h];
    for (int k = 0; k < GZ_CATENA && c; k++) {
      size_t j = c - 1;
      if (i - j > (1u << GZ_BIT_FINESTRA) - 1 || j >= i) break;
      if (in[j + migliore] == in[i + migliore]) {
        size_t l = 0; while (l < lmax && in[j + l] == in[i + l]) l++;
        if (l > migliore) { migliore = (uint16_t)l; dist = (uint16_t)(i - j); if (l == lmax) break; }
      }
      uint32_t p = prec[j & ((1 << GZ_BIT_FINESTRA) - 1)];
      if (p >= c) break;                  // voce sovrascritta dalla finestra
      c = p;
    }
    return migliore >= 3 ? migliore : 0;
  }
  // Percorre il testo e chiama f(lunghezza, distanza) (lunghezza 0 = letterale in[i])
  template <class F> void giro(F f) {
    memset(testa, 0, sizeof(uint16_t) << GZ_BIT_HASH);
    size_t i = 0;
    while (i < n) {
      uint16_t d = 0, l = cerca(i, d);
      if (l && l < 32 && i + 1 < n) {      // valutazione pigra: conviene partire un byte dopo?
        uint16_t d2 = 0; inserisci(i);
        uint16_t l2 = cerca(i + 1, d2);
        if (l2 > l) { f(0, 0, i); i++; l = l2; d = d2; }
        else { f(l, d, i); for (size_t k = 1; k < l; k++) inserisci(i + k); i += l; continue; }
        f(l, d, i); for (size_t k = 0; k < l; k++) inserisci(i + k); i += l; continue;
      }
      if (l) { f(l, d, i); for (size_t k = 0; k < l; k++) inserisci(i + k); i += l; }
      else { f(0, 0, i); inserisci(i); i++; }
    }
  }
};

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
  return ~c;
}

} // namespace gz

// Comprime in formato gzip. Ritorna i byte scritti, 0 se non c'e' memoria o non sta in cap.
static size_t gzipComprimi(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
  using namespace gz;
  if (!n) return 0;
  Lz lz; lz.in = in; lz.n = n;
  lz.testa = (uint16_t *)malloc(sizeof(uint16_t) << GZ_BIT_HASH);
  lz.prec = (uint16_t *)malloc(sizeof(uint16_t) << GZ_BIT_FINESTRA);
  uint32_t *fL = (uint32_t *)calloc(286 + 30 + 19, sizeof(uint32_t));
  uint8_t *len = (uint8_t *)calloc(286 + 30 + 19 + 316, 1);
  uint16_t *cod = (uint16_t *)calloc(286 + 30 + 19, sizeof(uint16_t));
  size_t r = 0;
  if (lz.testa && lz.prec && fL && len && cod) {
    uint32_t *fD = fL + 286, *fC = fD + 30;
    uint8_t *lL = len, *lD = len + 286, *lC = lD + 30, *seq = lC + 19;   // seq: lunghezze L+D di fila
    uint16_t *cL = cod, *cD = cod + 286, *cC = cD + 30;
    // 1. frequenze
    lz.giro([&](uint16_t l, uint16_t d, size_t i) { if (l) { fL[257 + codiceL(l)]++; fD[codiceD(d)]++; } else fL[in[i]]++; });
    fL[256] = 1;
    int nd = 0; for (int i = 0; i < 30; i++) if (fD[i]) nd++;
    if (nd < 2) { if (!fD[0]) fD[0] = 1; if (!fD[1]) fD[1] = 1; }     // due codici distanza: albero completo
    bool ok = lunghezze(fL, 286, 15, lL) && lunghezze(fD, 30, 15, lD);
    int hlit = 286; while (hlit > 257 && !lL[hlit - 1]) hlit--;
    int hdist = 30; while (hdist > 1 && !lD[hdist - 1]) hdist--;
    // 2. lunghezze L+D in codici 0..18 (16 = ripeti la precedente, 17/18 = zeri)
    int ns = 0; for (int i = 0; i < hlit; i++) seq[ns++] = lL[i];
    for (int i = 0; i < hdist; i++) seq[ns++] = lD[i];
    struct Rle { uint8_t s, x; } *rle = (Rle *)malloc(ns * sizeof(Rle));
    int nr = 0;
    if (rle) {
      for (int i = 0; i < ns;) {
        int k = 1; while (i + k < ns && seq[i + k] == seq[i]) k++;
        if (seq[i] == 0 && k >= 3) {
          int q = k > 138 ? 138 : k;
          if (q >= 11) rle[nr++] = {18, (uint8_t)(q - 11)}; else rle[nr++] = {17, (uint8_t)(q - 3)};
          i += q; continue;
        }
        rle[nr++] = {seq[i], 0}; i++; k--;
        while (seq[i - 1] != 0 && k >= 3) { int q = k > 6 ? 6 : k; rle[nr++] = {16, (uint8_t)(q - 3)}; i += q; k -= q; }
      }
      for (int i = 0; i < nr; i++) fC[rle[i].s]++;
      ok = ok && lunghezze(fC, 19, 7, lC);
    } else ok = false;
    if (ok) {
      int hclen = 19; while (hclen > 4 && !lC[ORDINE[hclen - 1]]) hclen--;
      codici(lL, 286, cL); codici(lD, 30, cD); codici(lC, 19, cC);
      Uscita o = {out, cap, 0, 0, 0, false};
      static const uint8_t TESTA[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
      for (int i = 0; i < 10; i++) o.ottetto(TESTA[i]);
      o.scrivi(1, 1); o.scrivi(2, 2);                       // ultimo blocco, Huffman dinamico
      o.scrivi(hlit - 257, 5); o.scrivi(hdist - 1, 5); o.scrivi(hclen - 4, 4);
      for (int i = 0; i < hclen; i++) o.scrivi(lC[ORDINE[i]], 3);
      for (int i = 0; i < nr; i++) {
        o.scrivi(cC[rle[i].s], lC[rle[i].s]);
        if (rle[i].s == 16) o.scrivi(rle[i].x, 2); else if (rle[i].s == 17) o.scrivi(rle[i].x, 3); else if (rle[i].s == 18) o.scrivi(rle[i].x, 7);
      }
      lz.giro([&](uint16_t l, uint16_t d, size_t i) {
        if (o.pieno) return;
        if (!l) { o.scrivi(cL[in[i]], lL[in[i]]); return; }
        uint8_t a = codiceL(l), b = codiceD(d);
        o.scrivi(cL[257 + a], lL[257 + a]); if (EXTRA_L[a]) o.scrivi(l - BASE_L[a], EXTRA_L[a]);
        o.scrivi(cD[b], lD[b]); if (EXTRA_D[b]) o.scrivi(d - BASE_D[b], EXTRA_D[b]);
      });
      o.scrivi(cL[256], lL[256]);
      o.chiudi();
      uint32_t crc = crc32(in, n), dim = (uint32_t)n;
      for (int k = 0; k < 4; k++) o.ottetto((uint8_t)(crc >> (8 * k)));
      for (int k = 0; k < 4; k++) o.ottetto((uint8_t)(dim >> (8 * k)));
      if (!o.pieno) r = o.n;
    }
    free(rle);
  }
  free(lz.testa); free(lz.prec); free(fL); free(len); free(cod);
  return r;
}
