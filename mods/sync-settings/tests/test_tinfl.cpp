// Decodes raw DEFLATE with the vendored tinfl exactly as sync-settings' inflatePayload does: test_tinfl <packed> <expected>.
#include <helpers/sync_settings/miniz/miniz_tinfl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static std::vector<unsigned char> slurp(const char* path) {
  std::vector<unsigned char> data;
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return data;
  int c;
  while ((c = fgetc(f)) != EOF) data.push_back((unsigned char)c);
  fclose(f);
  return data;
}

// Mirrors inflatePayload: heap decoder state, one call, non-wrapping output buffer of out_cap bytes.
static bool inflate(const std::vector<unsigned char>& in, unsigned char* out, size_t out_cap, size_t& out_len) {
  tinfl_decompressor* decoder = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
  if (decoder == nullptr) return false;
  tinfl_init(decoder);
  size_t consumed = in.size();
  size_t produced = out_cap;
  tinfl_status status = tinfl_decompress(decoder, in.data(), &consumed, out, out, &produced,
                                         TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  free(decoder);
  if (status != TINFL_STATUS_DONE || produced == 0 || produced > out_cap) return false;
  out_len = produced;
  return true;
}

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::vector<unsigned char> packed = slurp(argv[1]);
  std::vector<unsigned char> expected = slurp(argv[2]);
  if (packed.empty() || expected.empty()) return 2;

  std::vector<unsigned char> out(expected.size());
  size_t out_len = 0;
  if (!inflate(packed, out.data(), out.size(), out_len) || out_len != expected.size() ||
      memcmp(out.data(), expected.data(), out_len) != 0) {
    puts("FAIL: exact-size buffer did not reproduce the input");
    return 1;
  }
  // A buffer one byte short must fail rather than truncate.
  if (inflate(packed, out.data(), out.size() - 1, out_len)) {
    puts("FAIL: undersized buffer accepted");
    return 1;
  }
  // Corrupt input must fail, not crash.
  std::vector<unsigned char> broken(packed);
  broken[0] ^= 0xFF;
  for (size_t i = 1; i < broken.size(); i += 3) broken[i] ^= 0x5A;
  std::vector<unsigned char> sink(expected.size() * 4 + 64);
  size_t ignored = 0;
  inflate(broken, sink.data(), sink.size(), ignored);
  puts("OK");
  return 0;
}
