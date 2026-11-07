// Compressor of the ESP (PSN-Presepe-WiFi/comprimi.h): compresses each file given on the
// command line into <file>.gz; run_tests.sh checks the result with Python's gzip module.
#include "../PSN-Presepe-WiFi/comprimi.h"
#include <cstdio>
#include <string>
#include <vector>
int main(int argc, char **argv) {
  for (int a = 1; a < argc; a++) {
    FILE *f = std::fopen(argv[a], "rb"); if (!f) return 1;
    std::vector<uint8_t> d; int c; while ((c = std::fgetc(f)) >= 0) d.push_back((uint8_t)c); std::fclose(f);
    std::vector<uint8_t> o(d.size() + 1024);
    size_t r = gzipComprimi(d.data(), d.size(), o.data(), o.size());
    std::string n = std::string(argv[a]) + ".gz";
    FILE *g = std::fopen(n.c_str(), "wb"); std::fwrite(o.data(), 1, r, g); std::fclose(g);
  }
  return 0;
}
