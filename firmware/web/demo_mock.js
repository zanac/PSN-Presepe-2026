// Demo board for the pages in demo/: answers the /api/* requests of the panel and of the
// configuration editor in the browser, without a presepe. Built into demo/*.html by
// firmware/tools/build_web.py together with:
//   DEMO_INI    the repo's PRESEPE.INI (the file "on the microSD")
//   DEMO_ENGINE the parser and light engine of simulation_web/simulator.template.html
// so the demo follows the firmware rules and the default file automatically.
// The microSD (PRESEPE.INI and PRESEPE.BAK) is kept in localStorage, shared by the two pages.
(() => {
  "use strict";
  const E = DEMO_ENGINE();
  const KEY = "presepe-demo-sd";
  const load = () => { try { return JSON.parse(localStorage.getItem(KEY)) || null; } catch (e) { return null; } };
  const sd = load() || { ini: DEMO_INI, bak: null };
  const keep = () => { try { localStorage.setItem(KEY, JSON.stringify(sd)); } catch (e) {} };

  // ------------------------------------------------------------------ board state
  let cfg, errori, prima, p, run, modo, bootFino = 0, last = performance.now();
  let test = { cielo: [0, 0, 0], tramonto: [0, 0, 0], alba: [0, 0, 0], rele: 0 };
  const ids = new Map();                 // numbered commands already done -> reply
  function avvio() {                     // power-on / "Riavvia il presepe"
    const r = E.parse(sd.ini);
    cfg = r.c; errori = r.errors.length; prima = errori ? (r.errors[0].n || 65535) : 0;
    p = 0; modo = 0; run = !/^\s*partenza\s*=\s*pausa/im.test(sd.ini);
  }
  avvio();
  const durata = () => cfg.durate[0];    // potentiometer at the first position
  function avanza() {
    const now = performance.now(), dt = (now - last) / 1000; last = now;
    if (run && modo === 0 && now > bootFino) p = (p + dt * 100 / durata()) % 100;
  }
  function stato() {
    avanza();
    const mega = performance.now() > bootFino;
    const fa = E.faseDi(cfg, p), a = E.inizio(cfg, fa), b = E.fine(cfg, fa);
    const ciclo = modo === 0;
    const col = s => modo === 4 ? [0, 0, 0] : ciclo ? E.colore(cfg, s, p) : test[E.STRISCE[s]];
    const releCiclo = E.rele(cfg, p).reduce((m, on, i) => on ? m | (1 << i) : m, 0);
    return {
      versione: "demo", ip: "192.168.1.50", ssid: "Casa", host: "presepe", rssi: -58,
      mega, eta: mega ? 200 : 999999, modo, run: run ? 1 : 0, fase: fa,
      pctFase: Math.round(b > a ? (p - a) * 100 / (b - a) : 0), pct: Math.round(p * 10) / 10, durata: durata(),
      sd: errori ? 4 : 3, err: errori, riga: prima, nst: cfg.numStelle, nca: cfg.numCasette,
      rele: modo === 4 ? 0 : ciclo ? releCiclo : test.rele, cielo: col(0), tramonto: col(1), alba: col(2), passo: 0,
      nomi: cfg.nomi.slice(),
    };
  }

  // ------------------------------------------------------------------ commands (same rules as the Mega)
  let audio = null;
  function bip() {
    try {
      audio = audio || new (window.AudioContext || window.webkitAudioContext)();
      const o = audio.createOscillator(), g = audio.createGain();
      o.frequency.value = 2700; g.gain.value = 0.05; o.connect(g); g.connect(audio.destination);
      o.start(); o.stop(audio.currentTime + 0.12);
    } catch (e) {}
  }
  function entraTest() { if (modo !== 2) { modo = 2; test = { cielo: [0, 0, 0], tramonto: [0, 0, 0], alba: [0, 0, 0], rele: 0 }; } }
  function comando(c) {
    const f = c.trim().split(/\s+/), k = f[0], n = f.slice(1).map(Number);
    const ok = { ok: true, msg: "" }, err = m => ({ ok: false, msg: m });
    const range = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
    const stop = err("presepe in STOP: premi Play");
    if (modo === 4 && ["T", "S", "R", "P", "PA", "PC", "RUN", "NEXT"].includes(k)) return stop;
    switch (k) {
      case "STOP": modo = 4; run = false; return ok;           // tutto spento
      case "PLAY": if (modo === 4) { modo = 0; run = true; p = 0; } return ok;   // dall'inizio del GIORNO
      case "T": entraTest(); return ok;
      case "X": if (modo === 2) { modo = 0; run = true; } return ok;
      case "S":
        if (n.length !== 4 || !range(n[0], 0, 2) || !n.slice(1).every(v => range(v, 0, 255))) return err("arg");
        entraTest(); test[E.STRISCE[n[0]]] = n.slice(1); return ok;
      case "R":
        if (n.length !== 2 || !range(n[0], 1, 16) || !range(n[1], 0, 1)) return err("arg");
        entraTest(); test.rele = n[1] ? test.rele | (1 << (n[0] - 1)) : test.rele & ~(1 << (n[0] - 1)); return ok;
      case "P": case "PA": case "PC": entraTest(); return ok;   // the panel draws the pixels itself
      case "B": bip(); return ok;
      case "RUN": case "NEXT": {
        const id = f[1];
        if (id && ids.has(id)) return ids.get(id);
        let r = ok;
        if (modo !== 0) r = err("comando non disponibile durante il test");
        else if (k === "RUN") run = !run;
        else { avanza(); const fa = E.faseDi(cfg, p); p = fa < 4 ? E.fine(cfg, fa) : 0; }
        if (id) ids.set(id, r);
        return r;
      }
    }
    return err("comando non valido");
  }

  // ------------------------------------------------------------------ fetch of /api/*
  const json = (o, code = 200) => new Response(JSON.stringify(o), { status: code, headers: { "Content-Type": "application/json" } });
  const lento = ms => new Promise(r => setTimeout(r, ms));          // the real board takes a moment
  const offline = () => json({ ok: false, msg: "il presepe non risponde" }, 503);
  const vero = window.fetch.bind(window);
  window.fetch = async (url, opt = {}) => {
    const u = typeof url === "string" ? url : url.url;
    if (!u.startsWith("/api/")) return vero(url, opt);
    const metodo = (opt.method || "GET").toUpperCase(), corpo = opt.body || "";
    const mega = performance.now() > bootFino;
    if (u === "/api/stato") return json(stato());
    if (u === "/api/cmd" && metodo === "POST") { await lento(60); return mega ? json(comando(corpo)) : offline(); }
    if (u === "/api/ini" && metodo === "GET") {
      await lento(400);
      if (!mega) return new Response("il presepe non risponde", { status: 503 });
      return new Response(sd.ini.replace(/\r/g, "").replace(/([^\n])$/, "$1\n"), { headers: { "Content-Type": "text/plain; charset=utf-8", "X-File": "PRESEPE.INI" } });
    }
    if (u === "/api/ini" && metodo === "POST") {
      await lento(900);
      if (!mega) return offline();
      if (!corpo) return json({ ok: false, msg: "file vuoto, non salvato" }, 400);
      sd.bak = sd.ini; sd.ini = corpo; keep();
      return json({ ok: true, file: "PRESEPE.INI" });
    }
    if (u === "/api/ripristina" && metodo === "POST") {
      await lento(700);
      if (!mega) return offline();
      if (sd.bak === null) return json({ ok: false, msg: "non c'e' una copia precedente (PRESEPE.BAK)" }, 503);
      [sd.ini, sd.bak] = [sd.bak, sd.ini]; keep();
      return json({ ok: true, file: "PRESEPE.INI" });
    }
    if (u === "/api/riavvia" && metodo === "POST") {
      if (!mega) return offline();
      bootFino = performance.now() + 4000;                           // splash 3 s + SD
      setTimeout(avvio, 3900);
      return json({ ok: true, msg: "" });
    }
    return new Response("non trovato", { status: 404 });
  };

  // ------------------------------------------------------------------ banner
  window.demoRipristinaSD = () => { try { localStorage.removeItem(KEY); } catch (e) {} location.reload(); };
  document.addEventListener("DOMContentLoaded", () => {
    const b = document.createElement("div");
    b.className = "demo-banner";
    b.innerHTML = 'DEMO: nessuna scheda collegata, il presepe è simulato nel browser. ' +
      '<a href="index.html">Pannello</a> · <a href="config.html">Configurazione</a> · ' +
      '<a href="#" onclick="demoRipristinaSD(); return false">rimetti la microSD di esempio</a>';
    document.body.prepend(b);
  });
})();
