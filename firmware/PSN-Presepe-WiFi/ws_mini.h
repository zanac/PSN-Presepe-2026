// WebSocket minimo per il pannello (RFC 6455, solo messaggi di testo brevi).
//
// Il pannello apre una connessione che resta aperta: la scheda gli spinge lo stato appena il
// Mega lo manda (circa ogni secondo) e i comandi viaggiano sulla stessa connessione, senza
// aprire ogni volta una connessione HTTP nuova (con un segnale debole e' quella che costa).
//
// Porta separata (81): il server HTTP del core serve una richiesta alla volta e non sa tenere
// connessioni aperte. Un'altra porta e' un'altra "origine" per il browser, che non manda le
// credenziali del pannello: per questo la connessione deve portare una chiave (?k=...), scelta
// a caso a ogni avvio dell'ESP e data solo a chi e' autorizzato (/api/stato).
//
// Messaggi (testo):
//   pagina -> scheda   "c <id> <comando>"   comando (come POST /api/cmd)
//                      "p <n>"              ping: la scheda risponde "q <n>"
//   scheda -> pagina   "r <id> 1"           comando eseguito
//                      "r <id> 0 <motivo>"  comando non eseguito
//                      "s <json>"           stato (come /api/stato, senza i nomi dei rele')
//                      "q <n>"              risposta al ping
// Una pagina che non manda niente per WS_SILENZIO ms viene chiusa (il pannello fa un ping ogni
// 5 s): cosi' un telefono che va in standby non tiene occupato un posto.
#pragma once
#include <ESP8266WiFi.h>
#include <functional>

#ifndef WS_CLIENTI
#define WS_CLIENTI 3
#endif
#define WS_SILENZIO 15000UL
#define WS_MAX_MSG 200          // messaggi dalla pagina: i comandi sono di poche decine di byte

namespace wsm {

// SHA-1 (per Sec-WebSocket-Accept): 20 byte di h da n byte di d
inline void sha1(const uint8_t *d, size_t n, uint8_t h[20]) {
  uint32_t s[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  auto rol = [](uint32_t v, int k) { return (v << k) | (v >> (32 - k)); };
  uint64_t bit = (uint64_t)n * 8;
  size_t tot = ((n + 8) / 64 + 1) * 64;
  uint8_t b[64];
  for (size_t off = 0; off < tot; off += 64) {
    for (int i = 0; i < 64; i++) {
      size_t k = off + i;
      b[i] = k < n ? d[k] : k == n ? 0x80 : k >= tot - 8 ? (uint8_t)(bit >> (8 * (tot - 1 - k))) : 0;
    }
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s[0], bb = s[1], c = s[2], dd = s[3], e = s[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (bb & c) | (~bb & dd); k = 0x5A827999u; }
      else if (i < 40) { f = bb ^ c ^ dd; k = 0x6ED9EBA1u; }
      else if (i < 60) { f = (bb & c) | (bb & dd) | (c & dd); k = 0x8F1BBCDCu; }
      else { f = bb ^ c ^ dd; k = 0xCA62C1D6u; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = dd; dd = c; c = rol(bb, 30); bb = a; a = t;
    }
    s[0] += a; s[1] += bb; s[2] += c; s[3] += dd; s[4] += e;
  }
  for (int i = 0; i < 20; i++) h[i] = (uint8_t)(s[i / 4] >> (24 - 8 * (i % 4)));
}

inline String base64(const uint8_t *d, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String o;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    o += A[(v >> 18) & 63]; o += A[(v >> 12) & 63];
    o += i + 1 < n ? A[(v >> 6) & 63] : '=';
    o += i + 2 < n ? A[v & 63] : '=';
  }
  return o;
}

// Sec-WebSocket-Accept per la chiave della pagina
inline String accetta(const String &chiave) {
  String t = chiave + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t h[20];
  sha1((const uint8_t *)t.c_str(), t.length(), h);
  return base64(h, 20);
}

// Intestazione di un messaggio di testo dalla scheda (senza maschera); ritorna i byte scritti
inline size_t testa(uint8_t *o, size_t n) {
  o[0] = 0x81;
  if (n < 126) { o[1] = (uint8_t)n; return 2; }
  o[1] = 126; o[2] = (uint8_t)(n >> 8); o[3] = (uint8_t)n; return 4;
}

struct Cliente {
  WiFiClient c;
  bool aperto = false;          // dopo la stretta di mano
  String richiesta;             // intestazioni HTTP in arrivo
  uint8_t buf[WS_MAX_MSG + 14];
  size_t n = 0;
  unsigned long visto = 0;      // ultimo byte ricevuto
};

class Server {
 public:
  explicit Server(uint16_t porta) : srv(porta) {}
  void begin() { srv.begin(); }
  void chiave(const String &k) { segreto = k; }
  int clienti() const { int k = 0; for (const Cliente &x : cl) if (x.aperto) k++; return k; }

  // Da chiamare spesso (loop): nuove connessioni, strette di mano, messaggi in arrivo.
  void loop(const std::function<void(Cliente &, const String &)> &suMessaggio) {
    WiFiClient nuovo = srv.accept();
    if (nuovo) {
      Cliente *posto = nullptr;
      for (Cliente &x : cl) if (!x.c.connected()) { posto = &x; break; }
      if (!posto) {                                  // pieno: lascio il posto al nuovo, via il piu' silenzioso
        posto = &cl[0];
        for (Cliente &x : cl) if (x.visto < posto->visto) posto = &x;
        posto->c.stop();
      }
      posto->c = nuovo; posto->c.setNoDelay(true);
      posto->aperto = false; posto->richiesta = ""; posto->n = 0; posto->visto = millis();
    }
    for (Cliente &x : cl) {
      if (!x.c.connected()) { x.aperto = false; continue; }
      // byte in attesa = la pagina c'e' (anche se l'ESP e' stato fermo a lungo, per es. 20 s di salvataggio)
      if (x.c.available()) x.visto = millis();
      if (millis() - x.visto > (x.aperto ? WS_SILENZIO : 3000UL)) { chiudi(x); continue; }
      if (!x.aperto) stretta(x); else leggi(x, suMessaggio);
    }
  }

  bool invia(Cliente &x, const String &t) {
    if (!x.aperto) return false;
    uint8_t h[4]; size_t k = testa(h, t.length());
    if (x.c.availableForWrite() < k + t.length()) return false;    // pagina lenta: salta, lo stato e' periodico
    x.c.write(h, k); x.c.write((const uint8_t *)t.c_str(), t.length());
    return true;
  }
  void inviaTutti(const String &t) { for (Cliente &x : cl) invia(x, t); }

 private:
  WiFiServer srv;
  String segreto;
  Cliente cl[WS_CLIENTI];

  void chiudi(Cliente &x) {
    if (x.aperto) { uint8_t f[2] = {0x88, 0}; x.c.write(f, 2); }
    x.c.stop(); x.aperto = false;
  }
  static String valore(const String &r, const char *nome) {       // intestazione (nome in minuscolo)
    String m = r; m.toLowerCase();
    int p = m.indexOf(String("\n") + nome + ":");
    if (p < 0) return "";
    p += strlen(nome) + 2;
    int e = r.indexOf('\r', p); if (e < 0) e = r.indexOf('\n', p);
    String v = r.substring(p, e < 0 ? r.length() : e); v.trim();
    return v;
  }
  void stretta(Cliente &x) {
    while (x.c.available()) {
      char ch = (char)x.c.read();
      x.visto = millis();
      if (x.richiesta.length() > 1500) { x.c.stop(); return; }
      x.richiesta += ch;
      if (!x.richiesta.endsWith("\r\n\r\n")) continue;
      // "GET /?k=<chiave> HTTP/1.1"
      int a = x.richiesta.indexOf("k="), b = x.richiesta.indexOf(' ', a);
      String k = a < 0 || b < 0 ? String("") : x.richiesta.substring(a + 2, b);
      String chiaveWs = valore(x.richiesta, "sec-websocket-key");
      if (!x.richiesta.startsWith("GET ") || !segreto.length() || k != segreto || !chiaveWs.length()) {
        const char *no = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        x.c.write((const uint8_t *)no, strlen(no)); x.c.stop(); return;
      }
      String si = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                  accetta(chiaveWs) + "\r\n\r\n";
      x.c.write((const uint8_t *)si.c_str(), si.length());
      x.aperto = true; x.richiesta = ""; x.n = 0;
      return;
    }
  }
  void leggi(Cliente &x, const std::function<void(Cliente &, const String &)> &suMessaggio) {
    while (x.c.available() && x.n < sizeof(x.buf)) { x.buf[x.n++] = (uint8_t)x.c.read(); x.visto = millis(); }
    while (x.n >= 2) {
      uint8_t op = x.buf[0] & 15;
      bool fin = x.buf[0] & 0x80, maschera = x.buf[1] & 0x80;
      size_t len = x.buf[1] & 127, h = 2;
      if (len == 126) { if (x.n < 4) return; len = (size_t)x.buf[2] << 8 | x.buf[3]; h = 4; }
      // messaggi spezzati, senza maschera o lunghi: la pagina non li manda
      if (len == 127 || !fin || !maschera || len > WS_MAX_MSG) { chiudi(x); return; }
      if (x.n < h + 4 + len) return;                           // messaggio non ancora intero
      uint8_t *m = x.buf + h, *p = m + 4;
      for (size_t i = 0; i < len; i++) p[i] ^= m[i & 3];
      String testo; testo.reserve(len);
      for (size_t i = 0; i < len; i++) testo += (char)p[i];
      size_t usati = h + 4 + len;
      memmove(x.buf, x.buf + usati, x.n - usati); x.n -= usati;
      if (op == 8) { chiudi(x); return; }                      // chiusura
      if (op == 9) {                                           // ping del browser: pong
        uint8_t f[2] = {0x8A, 0}; x.c.write(f, 2); continue;
      }
      if (op == 1) suMessaggio(x, testo);
      if (!x.aperto) return;
    }
  }
};

} // namespace wsm
