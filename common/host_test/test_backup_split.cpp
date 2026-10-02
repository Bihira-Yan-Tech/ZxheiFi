// Host tests for firmware/backup_split.h - the streaming restore splitter.
// Run: python tools/run_host_tests.py
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "../../firmware/backup_split.h"

using zxb::BackupSplitter;

static int fails = 0;

#define CHECK(name, cond)                                   \
  do {                                                      \
    bool ok_ = (cond);                                      \
    if (!ok_) fails++;                                      \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name);         \
  } while (0)

struct Collect : BackupSplitter::Sink {
  std::map<std::string, std::string> files;
  std::vector<std::string> order;
  std::string current;
  std::vector<std::string> reject;
  bool fileStart(const char* name) override {
    for (auto& r : reject) if (r == name) return false;
    current = name;
    order.push_back(name);
    files[current] = "";
    return true;
  }
  void fileData(const char* p, size_t n) override { files[current].append(p, n); }
  bool fileEnd() override { return true; }
};

struct Result {
  BackupSplitter::State state;
  Collect sink;
  long version;
  std::string board, fw;
};

static Result run(const std::string& text, size_t chunk = 0, std::vector<std::string> reject = {}) {
  Result r;
  r.sink.reject = reject;
  BackupSplitter s(r.sink);
  if (!chunk) chunk = text.size() ? text.size() : 1;
  for (size_t i = 0; i < text.size(); i += chunk) {
    s.feed(text.data() + i, std::min(chunk, text.size() - i));
  }
  r.state = s.finish();
  r.version = s.version();
  r.board = s.board();
  r.fw = s.fw();
  return r;
}

static const char* CONFIG = "{\"brandName\":\"Shop {1}\",\"note\":\"say \\\"hi\\\" }]\",\"n\":[1,2,{\"a\":[]}]}";
static const char* VOUCHERS = "[{\"code\":\"ZX-1\",\"used\":false},{\"code\":\"ZX-2\",\"used\":true}]";

static std::string sample() {
  return std::string("{\n  \"zxheifiBackup\": 1,\n  \"fw\": \"2.0.0-dev\",\n  \"board\": \"esp8266\",\n"
                     "  \"createdAt\": 1790000000,\n  \"files\": {\n    \"config.json\": ") + CONFIG +
         ",\n    \"vouchers.json\": " + VOUCHERS + ",\n    \"today.json\": {}\n  }\n}\n";
}

int main() {
  {
    Result r = run(sample());
    CHECK("sample backup -> Ok", r.state == BackupSplitter::Ok);
    CHECK("header read", r.version == 1 && r.board == "esp8266" && r.fw == "2.0.0-dev");
    CHECK("three files in order", r.sink.order.size() == 3 && r.sink.order[0] == "config.json" &&
          r.sink.order[1] == "vouchers.json" && r.sink.order[2] == "today.json");
    CHECK("config bytes exact (escapes + braces in strings)", r.sink.files["config.json"] == CONFIG);
    CHECK("vouchers bytes exact", r.sink.files["vouchers.json"] == VOUCHERS);
    CHECK("empty object file", r.sink.files["today.json"] == "{}");
  }
  {
    std::string text = sample();
    bool allSame = true;
    for (size_t chunk = 1; chunk <= text.size(); chunk++) {
      Result r = run(text, chunk);
      if (r.state != BackupSplitter::Ok || r.sink.files["config.json"] != CONFIG ||
          r.sink.files["vouchers.json"] != VOUCHERS || r.sink.order.size() != 3) {
        allSame = false;
        printf("  differs at chunk size %u\n", (unsigned)chunk);
        break;
      }
    }
    CHECK("every chunk size gives the same result", allSame);
  }
  {
    Result r = run("{\"files\":{\"a.json\":[1]},\"extra\":{\"x\":[\"}\",{}]},\"s\":\"q\\\"\",\"num\":-12.5e3,"
                   "\"t\":true,\"zxheifiBackup\":1}");
    CHECK("unknown keys skipped, header after files accepted", r.state == BackupSplitter::Ok &&
          r.sink.files["a.json"] == "[1]");
  }
  {
    Result r = run("{\"files\":{\"a.json\":[1]}}");
    CHECK("no zxheifiBackup -> NotABackup", r.state == BackupSplitter::NotABackup);
  }
  {
    Result r = run("{\"zxheifiBackup\":2,\"files\":{}}");
    CHECK("version 2 -> NotABackup", r.state == BackupSplitter::NotABackup);
  }
  {
    std::string text = sample();
    Result r = run(text.substr(0, text.size() / 2));
    CHECK("truncated -> Malformed", r.state == BackupSplitter::Malformed);
  }
  {
    Result r = run("{\"zxheifiBackup\":1,\"files\":{\"a.json\":\"just a string\"}}");
    CHECK("file value that's a string -> Malformed", r.state == BackupSplitter::Malformed);
  }
  {
    Result r = run(sample(), 0, {"vouchers.json"});
    CHECK("sink rejects a name -> Malformed", r.state == BackupSplitter::Malformed);
  }
  {
    Result r = run("{\"zxheifiBackup\":1,\"files\":{}}");
    CHECK("empty files -> Ok with 0 files", r.state == BackupSplitter::Ok && r.sink.order.empty());
  }
  {
    Result r = run("hello there");
    CHECK("garbage -> Malformed", r.state == BackupSplitter::Malformed);
    Result a = run("[1,2,3]");
    CHECK("an array -> Malformed", a.state == BackupSplitter::Malformed);
    Result e = run("");
    CHECK("empty input -> Malformed", e.state == BackupSplitter::Malformed);
  }
  {
    std::string longName(40, 'a');
    Result r = run("{\"zxheifiBackup\":1,\"files\":{\"" + longName + "\":{}}}");
    CHECK("file name over 32 chars -> Malformed", r.state == BackupSplitter::Malformed);
  }
  {
    Result r = run("{\"zxheifiBackup\":1,\"files\":{\"a.json\":{}}} trailing");
    CHECK("junk after the backup -> Malformed", r.state == BackupSplitter::Malformed);
  }

  printf("BACKUP SPLIT %s (%d failures)\n", fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
