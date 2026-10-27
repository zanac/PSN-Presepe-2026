/*
  PSN-Presepe WiFi - firmware per l'ESP8266 della scheda "Mega + WiFi R3"
  (ATmega2560 + ESP8266 sulla stessa basetta, es. WeMos/RobotDyn).

  L'ESP8266 fa solo da ponte verso la rete: tutta la logica del presepe resta
  nel Mega (firmware/PSN-Presepe/PSN-Presepe.ino). I due chip parlano sulla
  seriale interna (ESP UART0 <-> Mega Serial3, 115200 baud) quando i DIP switch
  1 e 2 sono su ON e il selettore e' su RXD3/TXD3.

  - SSID, password WiFi, nome in rete, utente e password della pagina arrivano
    dal Mega, che li legge dalle sezioni [WIFI] e [AUTENTICAZIONE] di PRESEPE.INI.
  - Pagina "/"        : pannello con stato del ciclo, comandi e test da telefono.
  - Pagina "/config"  : file PRESEPE.INI con anteprima del ciclo; salva e riavvia.
  - http://<nome>.local (mDNS) e aggiornamento del firmware ESP via WiFi (ArduinoOTA,
    password = quella della pagina).

  Protocollo con il Mega: vedi il blocco "LINK WIFI" di PSN-Presepe.ino.
  Comandi ESP -> Mega: "@COMANDO argomenti*HH" (HH = somma dei byte, esadecimale),
  risposta "@OK ..." / "@ERR ...". Senza risposta il comando viene ritrasmesso.

  Compilazione: IDE Arduino con il pacchetto "esp8266" (scheda "Generic ESP8266
  Module", Flash Size "4MB (FS:1MB OTA:~1019KB)"). Caricamento: DIP 5, 6, 7 su ON
  (vedi README). Le pagine web sono in pagine_web.h, generato da
  firmware/tools/build_web.py.
*/
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include "pagine_web.h"

#define VERSIONE "039"

ESP8266WebServer server(80);

// ------------------------------------------------------------------ configurazione dal Mega
String cfgSsid, cfgWpwd, cfgUtente, cfgApwd, cfgHost = "presepe";   // in uso
String nSsid, nWpwd, nUtente, nApwd, nHost;                          // in arrivo (applicati a @GO)
String nomiRele[16];
bool configOk = false;          // ricevuto almeno un @GO completo
uint8_t cfgCampi = 0;           // campi ricevuti dall'ultimo @SSID (1 SSID, 2 WPWD, 4 USER, 8 APWD, 16 HOST)
uint16_t idComando = 1;         // numero dei comandi non ripetibili (RUN, NEXT, COMMIT, RESTORE)
bool reteAvviata = false;       // WiFi.begin() eseguito
bool servizi = false;           // mDNS e OTA avviati (dopo la prima connessione)
unsigned long helloMs = 0;

// ------------------------------------------------------------------ stato dal Mega
long stato[22];                 // campi di "@ST" (vedi lnkInviaStato del Mega)
bool statoValido = false;
unsigned long statoMs = 0;

// ------------------------------------------------------------------ collegamento seriale
String rx;
String risposta;                // ultima riga @OK / @ERR (o @END per GET)
bool rispostaPronta = false;
bool inGet = false;
String getTesto, getNome;
String ipInviato = "";          // ultimo IP confermato dal Mega
unsigned long ipMs = 0;
bool ackVisto = false;          // "@ACK" (o "@FILE" per GET): il Mega ha ricevuto il comando e lavora

uint8_t somma(const String &s) {
  uint8_t x = 0;
  for (size_t i = 0; i < s.length(); i++) x = (uint8_t)(x + (uint8_t)s[i]);
  return x;
}

void scriviRiga(const String &l) {          // "@..." + "*HH\n"
  char h[4];
  snprintf(h, sizeof(h), "%02X", somma(l));
  Serial.print(l); Serial.print('*'); Serial.print(h); Serial.print('\n');
}

void applicaConfig() {
  bool rete = nSsid != cfgSsid || nWpwd != cfgWpwd || nHost != cfgHost || nApwd != cfgApwd;
  if (rete && reteAvviata) { delay(50); ESP.restart(); }   // cambio di rete/nome/password: ripartenza pulita
  cfgSsid = nSsid; cfgWpwd = nWpwd; cfgUtente = nUtente; cfgApwd = nApwd; cfgHost = nHost.length() ? nHost : "presepe";
  configOk = true;
  ipInviato = "";                                          // il Mega puo' essere ripartito: rimanda l'IP
  if (!reteAvviata && cfgSsid.length()) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(cfgHost);
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfgSsid.c_str(), cfgWpwd.length() ? cfgWpwd.c_str() : nullptr);
    reteAvviata = true;
  }
}

void rigaDalMega(String l) {
  if (!l.startsWith("@")) return;                          // rumore all'avvio del Mega
  if (l.startsWith("@ST ")) {
    int n = 0, p = 4;
    while (n < 22 && p <= (int)l.length()) {
      int q = l.indexOf(' ', p); if (q < 0) q = l.length();
      stato[n++] = l.substring(p, q).toInt(); p = q + 1;
    }
    statoValido = n == 22; statoMs = millis();
    return;
  }
  // La configurazione arriva sempre in quest'ordine: @SSID apre un nuovo invio, @GO lo applica
  // solo se sono arrivati tutti i campi (altrimenti l'ESP continua a mandare @HELLO).
  if (l.startsWith("@SSID ") || l == "@SSID") { nSsid = l.substring(6); cfgCampi = 1; return; }
  if (l.startsWith("@WPWD ") || l == "@WPWD") { nWpwd = l.substring(6); cfgCampi |= 2; return; }
  if (l.startsWith("@USER ")) { nUtente = l.substring(6); cfgCampi |= 4; return; }
  if (l.startsWith("@APWD ")) { nApwd = l.substring(6); cfgCampi |= 8; return; }
  if (l.startsWith("@HOST ")) { nHost = l.substring(6); cfgCampi |= 16; return; }
  if (l.startsWith("@NREL ")) {
    int sp = l.indexOf(' ', 6);
    int i = (sp < 0 ? l.substring(6) : l.substring(6, sp)).toInt();
    if (i >= 1 && i <= 16) nomiRele[i - 1] = sp < 0 ? "" : l.substring(sp + 1);
    return;
  }
  if (l == "@GO") { if (cfgCampi == 31 && nUtente.length() && nApwd.length()) applicaConfig(); cfgCampi = 0; return; }
  if (l == "@ACK") { ackVisto = true; return; }
  if (l.startsWith("@FILE ")) { getNome = l.substring(6); getTesto = ""; ackVisto = true; return; }
  if (l.startsWith("@L ") || l == "@L") { if (inGet) { getTesto += l.substring(3); getTesto += '\n'; } return; }
  if (l.startsWith("@C ")) { if (inGet) getTesto += l.substring(3); return; }
  if (l.startsWith("@END")) { if (inGet) { risposta = l; rispostaPronta = true; } return; }
  if (l.startsWith("@OK") || l.startsWith("@ERR")) { risposta = l; rispostaPronta = true; return; }
}

void leggiSeriale() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') { String l = rx; rx = ""; rigaDalMega(l); continue; }
    if (rx.length() < 300) rx += ch;
  }
}

// Invia un comando e aspetta la risposta. Ritrasmette se la riga non arriva al Mega
// (puo' perdere caratteri mentre aggiorna le WS2811). Ritorna "" se nessuna risposta.
// lavoroMs: dopo "@ACK" (comando ricevuto, lavoro sulla SD in corso) aspetta fino a
// lavoroMs la risposta finale senza ritrasmettere.
String comando(const String &c, unsigned long attesaMs = 600, uint8_t tentativi = 4, unsigned long lavoroMs = 0) {
  for (uint8_t t = 0; t < tentativi; t++) {
    leggiSeriale();
    rispostaPronta = false; risposta = ""; ackVisto = false;
    // Un '\n' da solo prima della riga: il Mega lo tiene nel registro della UART anche se sta
    // aggiornando le WS2811 a interrupt spenti, e appena lo legge smette di aggiornarle
    // per 150 ms (lnkQuiet), cosi' la riga vera arriva intera.
    Serial.print('\n');
    delay(8);
    scriviRiga("@" + c);
    unsigned long t0 = millis();
    bool lavoro = false;
    while (millis() - t0 < (lavoro ? lavoroMs : attesaMs)) {
      leggiSeriale();
      if (rispostaPronta) return risposta;
      if (ackVisto && lavoroMs && !lavoro) { lavoro = true; t0 = millis(); }
      delay(1);
    }
    if (lavoro) return "";        // il Mega ha ricevuto ma non ha finito: non ripetere
  }
  return "";
}
String nuovoId() { if (++idComando == 0) idComando = 1; return String(idComando); }

// ------------------------------------------------------------------ pagine
bool autorizzato() {
  if (!configOk || !cfgApwd.length()) {
    server.send(503, "text/plain; charset=utf-8",
                "Il presepe non ha ancora inviato la configurazione: [WIFI] abilitato = 1 nel PRESEPE.INI, "
                "DIP 1 e 2 su ON, selettore su RXD3/TXD3.");
    return false;
  }
  if (!server.authenticate(cfgUtente.c_str(), cfgApwd.c_str())) {
    server.requestAuthentication(BASIC_AUTH, "Presepe");
    return false;
  }
  return true;
}

void inviaPagina(const uint8_t *dati, size_t len) {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", (const char *)dati, len);
}

String jsonTesto(const String &s) {
  String o = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
    else if ((uint8_t)ch < 32) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", (uint8_t)ch); o += b; }
    else o += ch;
  }
  return o + "\"";
}

void rispostaJson(bool ok, const String &campo, const String &valore, int codice = 200) {
  String j = String("{\"ok\":") + (ok ? "true" : "false") + ",\"" + campo + "\":" + jsonTesto(valore) + "}";
  server.send(codice, "application/json", j);
}

// "@ERR motivo" -> testo per la pagina
String motivo(const String &r) {
  if (!r.length()) return "il presepe non risponde";
  if (r.startsWith("@ERR ")) {
    String m = r.substring(5);
    if (m == "sd") return "microSD assente o non scrivibile";
    if (m == "modo colore") return "sulla scheda e' attiva la modalita' colore";
    if (m == "test") return "comando non disponibile durante il test";
    if (m == "lunghezza") return "trasferimento incompleto, riprova";
    if (m == "nessun backup") return "non c'e' una copia precedente (PRESEPE.BAK)";
    if (m == "vuoto") return "file vuoto, non salvato";
    if (m == "copia") return "scrittura sulla microSD non riuscita (rimesso il file precedente)";
    if (m == "backup") return "copia di sicurezza PRESEPE.BAK non riuscita, file non salvato";
    if (m == "put") return "salvataggio interrotto, riprova";
    return m;
  }
  return r;
}

void apiStato() {
  if (!autorizzato()) return;
  bool mega = statoValido && millis() - statoMs < 3000;
  String j = "{";
  j += "\"versione\":\"" VERSIONE "\"";
  j += ",\"ip\":" + jsonTesto(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("none"));
  j += ",\"ssid\":" + jsonTesto(cfgSsid);
  j += ",\"host\":" + jsonTesto(cfgHost);
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"mega\":" + String(mega ? "true" : "false");
  j += ",\"eta\":" + String(statoValido ? millis() - statoMs : 999999UL);
  const long *s = stato;
  j += ",\"modo\":" + String(s[0]) + ",\"run\":" + String(s[1]) + ",\"fase\":" + String(s[2]);
  j += ",\"pctFase\":" + String(s[3]) + ",\"pct\":" + String(s[4] / 10.0, 1) + ",\"durata\":" + String(s[5]);
  j += ",\"sd\":" + String(s[6]) + ",\"err\":" + String(s[7]) + ",\"riga\":" + String(s[8]);
  j += ",\"nst\":" + String(s[9]) + ",\"nca\":" + String(s[10]) + ",\"rele\":" + String(s[11]);
  j += ",\"cielo\":[" + String(s[12]) + "," + String(s[13]) + "," + String(s[14]) + "]";
  j += ",\"tramonto\":[" + String(s[15]) + "," + String(s[16]) + "," + String(s[17]) + "]";
  j += ",\"alba\":[" + String(s[18]) + "," + String(s[19]) + "," + String(s[20]) + "]";
  j += ",\"passo\":" + String(s[21]);
  j += ",\"nomi\":[";
  for (int i = 0; i < 16; i++) { if (i) j += ","; j += jsonTesto(nomiRele[i]); }
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// Comandi ammessi dalla pagina (il Mega controlla comunque gli argomenti).
bool comandoAmmesso(const String &c) {
  if (c.length() == 0 || c.length() > 40) return false;
  for (size_t i = 0; i < c.length(); i++) {
    char ch = c[i];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ')) return false;
  }
  int sp = c.indexOf(' ');
  String cmd = sp < 0 ? c : c.substring(0, sp);
  const char *ok[] = { "T", "X", "S", "P", "PA", "PC", "R", "B", "RUN", "NEXT" };
  for (const char *k : ok) if (cmd == k) return true;
  return false;
}

// Le richieste che cambiano qualcosa devono avere l'intestazione X-Presepe (la mettono le
// pagine del presepe): un'altra pagina aperta nello stesso browser non puo' aggiungerla senza
// un permesso CORS che l'ESP non da', quindi non puo' usare le credenziali salvate.
bool daPagina() {
  if (server.header("X-Presepe") == "1") return true;
  server.send(403, "text/plain; charset=utf-8", "richiesta non valida");
  return false;
}

void apiComando() {
  if (!autorizzato() || !daPagina()) return;
  String c = server.arg("plain"); c.trim();
  if (!comandoAmmesso(c)) { rispostaJson(false, "msg", "comando non valido", 400); return; }
  if (c == "RUN" || c == "NEXT") c += " " + nuovoId();     // non ripetibili: numerati
  String r = comando(c);
  bool ok = r.startsWith("@OK");
  rispostaJson(ok, "msg", ok ? String("") : motivo(r));
}

void apiLeggiFile() {
  if (!autorizzato()) return;
  inGet = true; getTesto = ""; getNome = "";
  getTesto.reserve(12000);
  String r = comando("GET", 1500, 4, 15000);
  inGet = false;
  if (!r.startsWith("@END")) {
    server.send(503, "text/plain; charset=utf-8", motivo(r));
    getTesto = ""; return;
  }
  server.sendHeader("X-File", getNome);
  server.sendHeader("Access-Control-Expose-Headers", "X-File");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", getTesto);
  getTesto = "";
}

// Salva il testo sulla microSD: PUT, righe a pezzi da 100 caratteri, COMMIT.
void apiScriviFile() {
  if (!autorizzato() || !daPagina()) return;
  const String &t = server.arg("plain");      // nessuna copia: il file puo' essere lungo
  // X-Len = byte inviati dalla pagina: se la memoria e' finita il testo arriva vuoto o tagliato
  if (!t.length() || String(t.length()) != server.header("X-Len")) {
    rispostaJson(false, "msg", t.length() ? "file arrivato incompleto, riprova" : "file vuoto, non salvato", 400); return;
  }
  if (t.length() > 24000) { rispostaJson(false, "msg", "file troppo lungo (massimo 24000 caratteri)", 413); return; }
  String r = comando("PUT", 3000, 3);
  if (!r.startsWith("@OK")) { rispostaJson(false, "msg", motivo(r), 503); return; }
  uint16_t seq = 0; uint32_t byte = 0;
  size_t i = 0, n = t.length();
  while (i < n) {
    int fine = t.indexOf('\n', i);
    size_t fineRiga = fine < 0 ? n : (size_t)fine;
    // pezzi da 100: tutti "C" tranne l'ultimo della riga, che e' "W" (aggiunge il ritorno a capo)
    size_t p = i;
    do {
      size_t q = fineRiga - p > 100 ? p + 100 : fineRiga;
      bool ultimo = q == fineRiga;
      String pezzo = t.substring(p, q);
      pezzo.replace("\r", "");
      String cmd = String(ultimo ? "W " : "C ") + String(seq) + " " + pezzo;
      r = comando(cmd, 1500, 4);
      if (!r.startsWith("@OK")) { comando("ABORT", 3000, 2); rispostaJson(false, "msg", motivo(r), 503); return; }
      seq++; byte += pezzo.length() + (ultimo ? 1 : 0);
      p = q;
    } while (p < fineRiga);
    i = fineRiga + 1;
    if (fine < 0) break;
  }
  r = comando("COMMIT " + String(byte) + " " + nuovoId(), 1500, 4, 20000);
  if (!r.startsWith("@OK")) { rispostaJson(false, "msg", motivo(r), 503); return; }
  String nome = r.length() > 4 ? r.substring(4) : String("PRESEPE.INI");
  rispostaJson(true, "file", nome);
}

void apiRiavvia() {
  if (!autorizzato() || !daPagina()) return;
  String r = comando("REBOOT", 800, 4);
  bool ok = r.startsWith("@OK");
  if (ok) statoValido = false;
  rispostaJson(ok, "msg", ok ? String("") : motivo(r));
}

void apiRipristina() {
  if (!autorizzato() || !daPagina()) return;
  String r = comando("RESTORE " + nuovoId(), 1500, 4, 20000);
  if (!r.startsWith("@OK")) { rispostaJson(false, "msg", motivo(r), 503); return; }
  rispostaJson(true, "file", r.length() > 4 ? r.substring(4) : String("PRESEPE.INI"));
}

void setup() {
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();            // nessuna rete ricordata: si collega solo con i dati del Mega
  idComando = (uint16_t)(ESP.random() | 1);   // diverso a ogni avvio dell'ESP
  server.collectHeaders("X-Presepe", "X-Len");    // intestazioni lette da daPagina() e apiScriviFile()

  server.on("/", HTTP_GET, []() { if (autorizzato()) inviaPagina(PAGINA_PANNELLO, PAGINA_PANNELLO_LEN); });
  server.on("/config", HTTP_GET, []() { if (autorizzato()) inviaPagina(PAGINA_CONFIG, PAGINA_CONFIG_LEN); });
  server.on("/api/stato", HTTP_GET, apiStato);
  server.on("/api/cmd", HTTP_POST, apiComando);
  server.on("/api/ini", HTTP_GET, apiLeggiFile);
  server.on("/api/ini", HTTP_POST, apiScriviFile);
  server.on("/api/riavvia", HTTP_POST, apiRiavvia);
  server.on("/api/ripristina", HTTP_POST, apiRipristina);
  server.onNotFound([]() { server.send(404, "text/plain", "non trovato"); });
  server.begin();
}

void loop() {
  leggiSeriale();

  // Finche' il Mega non manda la configurazione, chiedila ogni 2 s.
  if (!configOk && millis() - helloMs >= 2000) { helloMs = millis(); scriviRiga("@HELLO"); }

  bool collegato = WiFi.status() == WL_CONNECTED;
  if (collegato && !servizi) {
    MDNS.begin(cfgHost.c_str());
    MDNS.addService("http", "tcp", 80);
    ArduinoOTA.setHostname(cfgHost.c_str());
    ArduinoOTA.setPassword(cfgApwd.c_str());
    ArduinoOTA.begin();
    servizi = true;
  }
  if (servizi) { MDNS.update(); ArduinoOTA.handle(); }

  // Indirizzo al Mega: a ogni cambio, e comunque ogni minuto.
  if (configOk) {
    String ip = collegato ? WiFi.localIP().toString() : String("none");
    if ((ip != ipInviato && millis() - ipMs > 3000) || millis() - ipMs > 60000) {
      ipMs = millis();
      if (comando("IP " + ip, 600, 2).startsWith("@OK")) ipInviato = ip;
    }
  }

  server.handleClient();
}
