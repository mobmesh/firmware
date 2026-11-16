#include <Arduino.h>
#include <string.h>
#include <Mesh.h>
#include <helpers/ClientACL.h>
#include <helpers/CommonCLI.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/CoreIntegration.h>
#include <helpers/ModLateReply.h>

#define LATE_ROW_MAX         155
#define LATE_ROW_SPACING_MS  1500UL
#define LATE_REPLY_DELAY_MS  600UL     // upstream's CLI_REPLY_DELAY_MILLIS

// The admin whose command is running: upstream records its timestamp on the client before dispatch.
bool modReplyCapture(uint32_t sender_timestamp, ModReplyTarget* out) {
  memset(out, 0, sizeof(*out));
  out->sender_timestamp = sender_timestamp;
  if (sender_timestamp == 0) return true;
  const ModBindings* b = modBindings();
  if (b == nullptr || b->acl == nullptr) return false;
  for (int i = 0; i < b->acl->getNumClients(); i++) {
    ClientInfo* c = b->acl->getClientByIdx(i);
    if (c->isAdmin() && c->last_timestamp == sender_timestamp) {
      memcpy(out->admin_key, c->id.pub_key, PUB_KEY_SIZE);
      out->remote = true;
      return true;
    }
  }
  return false;
}

void modReplyLate(const ModReplyTarget& target, const char* rows, size_t stride, int count) {
  if (!target.remote) {
    for (int i = 0; i < count; i++) {
      Serial.print(i == 0 ? "  -> " : "");
      Serial.println(rows + i * stride);
    }
    return;
  }
  const ModBindings* b = modBindings();
  if (b == nullptr || b->acl == nullptr || b->mesh == nullptr) return;
  ClientInfo* c = b->acl->getClient(target.admin_key, PUB_KEY_SIZE);
  if (c == nullptr) return;
  uint8_t hash_size = b->prefs ? b->prefs->path_hash_mode + 1 : 1;
  bool flood = c->out_path_len == OUT_PATH_UNKNOWN;
  uint32_t delay = LATE_REPLY_DELAY_MS;
  for (int i = 0; i < count; i++) {
    uint8_t frame[5 + LATE_ROW_MAX];
    const char* text = rows + i * stride;
    size_t len = strnlen(text, LATE_ROW_MAX);
    uint32_t stamp = modCliStamp(target.sender_timestamp);
    memcpy(frame, &stamp, 4);
    frame[4] = TXT_TYPE_CLI_DATA << 2;
    memcpy(&frame[5], text, len);
    mesh::Packet* pkt = b->mesh->createDatagram(PAYLOAD_TYPE_TXT_MSG, c->id, c->shared_secret, frame, 5 + len);
    if (pkt == nullptr) return;
    if (flood) b->mesh->sendFlood(pkt, delay, hash_size);
    else b->mesh->sendDirect(pkt, c->out_path, c->out_path_len, delay);
    // A flooded frame is echoed by the first relay after up to 5 x airtime x f_txdelay; the next frame waits that echo out.
    uint32_t airtime = modAirtimeFor(5 + len + 16);
    float factor = b->prefs ? b->prefs->tx_delay_factor : 1.0f;
    delay += flood ? (uint32_t)(airtime * (2.0f + 5.0f * factor)) + 300 : LATE_ROW_SPACING_MS;
  }
}
