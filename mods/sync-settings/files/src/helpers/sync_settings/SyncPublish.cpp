#include "SyncPublish.h"

#include <string.h>

namespace mobmesh {
namespace sync {

static bool reached(uint32_t now, uint32_t target) {
  return (int32_t)(now - target) >= 0;
}

Transmitter::Transmitter(const TxOps& ops) : ops_(ops) {
  memset(slot_, 0, sizeof(slot_));
}

Transmitter::~Transmitter() {
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) clear(slot_[i]);
}

TxState* Transmitter::find(uint8_t dataset) {
#if SYNC_SETTINGS_WITH_REGION
  if (dataset == REGION) return &slot_[0];
#endif
#if SYNC_SETTINGS_WITH_POLICY
  if (dataset == POLICY) return &slot_[SYNC_SETTINGS_WITH_REGION];
#endif
  return nullptr;
}

const TxState* Transmitter::find(uint8_t dataset) const {
  return const_cast<Transmitter*>(this)->find(dataset);
}

void Transmitter::clear(TxState& state) {
  if (state.data != nullptr && ops_.release != nullptr) {
    ops_.release(state.data, ops_.context);
  }
  memset(&state, 0, sizeof(state));
}

bool Transmitter::begin(const TxStart& start, uint32_t now) {
  TxState* state = find(start.dataset);
  uint8_t chunks = chunkCount(start.data_len);
  Manifest manifest = {};
  if (state == nullptr || state->active || state->aborting || start.data == nullptr ||
      chunks == 0 || start.interval_hours < 3 || start.interval_hours > 24 ||
      start.duration_days < 1 || start.duration_days > 4 ||
      readManifest(start.manifest, sizeof(start.manifest), manifest) != WIRE_OK ||
      manifest.dataset != start.dataset || manifest.generation != start.generation ||
      manifest.data_len != start.data_len || manifest.chunks != chunks ||
      manifest.days != start.duration_days) return false;
  memset(state, 0, sizeof(*state));
  state->active = true;
  state->dataset = start.dataset;
  state->generation = start.generation;
  state->interval_hours = start.interval_hours;
  state->duration_days = start.duration_days;
  state->scoped = start.scoped;
  memcpy(state->key, start.key, sizeof(state->key));
  memcpy(state->manifest, start.manifest, sizeof(state->manifest));
  state->data = start.data;
  state->data_len = start.data_len;
  state->chunks = chunks;
  state->rounds = (uint8_t)(((uint16_t)start.duration_days * 24u) /
                            start.interval_hours + 1u);
  state->started = now;
  state->retry_at = now;
  return true;
}

bool Transmitter::abort(uint8_t dataset,
                        const uint8_t manifest_hash[DIGEST_LEN], bool scoped,
                        const uint8_t key[16], uint32_t now) {
  TxState* state = find(dataset);
  if (state == nullptr || state->aborting || manifest_hash == nullptr ||
      (scoped && key == nullptr) || ops_.sign == nullptr) return false;
  uint8_t notice[ABORT_LEN];
  size_t len = writeAbortPrefix(dataset, manifest_hash, notice, sizeof(notice));
  if (len != ABORT_SIGNED_LEN ||
      !ops_.sign(notice, len, notice + len, ops_.context)) return false;
  uint8_t* owned = state->data;
  state->data = nullptr;
  if (owned != nullptr && ops_.release != nullptr) ops_.release(owned, ops_.context);
  state->active = false;
  state->round_open = false;
  state->dataset = dataset;
  state->scoped = scoped;
  memset(state->key, 0, sizeof(state->key));
  if (scoped) memcpy(state->key, key, sizeof(state->key));
  memcpy(state->abort_frame, notice, sizeof(notice));
  state->aborting = true;
  state->abort_next = now;
  state->retry_at = now;
  state->abort_deadline = now + ABORT_DEADLINE_MS;
  return true;
}

void Transmitter::advance(TxState& state) {
  state.attempts = 0;
  if (++state.frame > state.chunks) {
    state.frame = 0;
    state.round_open = false;
    ++state.rounds_done;
  }
}

TxState* Transmitter::choose(uint32_t now) {
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) {
    if (slot_[i].active && slot_[i].round_open) return &slot_[i];
  }
  TxState* selected = nullptr;
  uint32_t due = 0;
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) {
    TxState& state = slot_[i];
    if (!state.active || state.rounds_done >= state.rounds ||
        !reached(now, state.retry_at)) continue;
    uint32_t nominal = state.started + (uint32_t)state.rounds_done *
                       state.interval_hours * 3600000u;
    if (!reached(now, nominal)) continue;
    if (selected == nullptr || (int32_t)(nominal - due) < 0 ||
        (nominal == due && state.dataset == REGION)) {
      selected = &state;
      due = nominal;
    }
  }
  if (selected != nullptr) {
    selected->round_open = true;
    selected->retry_at = now;
  }
  return selected;
}

void Transmitter::send(TxState& state, uint32_t now) {
  uint8_t frame[FRAME_MAX];
  size_t len;
  if (state.frame == 0) {
    memcpy(frame, state.manifest, MANIFEST_LEN);
    len = MANIFEST_LEN;
  } else {
    uint8_t index = (uint8_t)(state.frame - 1);
    uint8_t bytes = chunkLength(index, state.data_len, state.chunks);
    uint16_t offset = (uint16_t)index * CHUNK_DATA_MAX;
    len = writeChunkPrefix(state.dataset, state.generation, index,
                           state.data + offset, bytes, frame, sizeof(frame));
    if (len == 0 || ops_.sign == nullptr ||
        !ops_.sign(frame, len, frame + len, ops_.context)) {
      state.retry_at = now + TX_RETRY_MS;
      if (++state.attempts >= TX_ATTEMPTS) {
        state.sign_failed = true;
        state.round_open = false;
        state.rounds_done = state.rounds;
      }
      return;
    }
    len += SIGNATURE_LEN;
  }
  uint32_t id = 0;
  uint32_t airtime = 0;
  if (ops_.send != nullptr &&
      ops_.send(frame, len, state.scoped, state.key, id, airtime,
                ops_.context)) {
    state.in_flight = true;
    state.abort_flight = false;
    state.packet_id = id;
    state.sent_at = now;
    state.flight_airtime = airtime;
  } else if (++state.attempts >= TX_ATTEMPTS) {
    advance(state);
  } else {
    state.retry_at = now + TX_RETRY_MS;
  }
}

void Transmitter::sendAbort(TxState& state, uint32_t now) {
  if (state.abort_sent >= 2 || reached(now, state.abort_deadline)) {
    if (!reached(now, state.retry_at)) return;
    TxEnd ending = state.abort_sent >= 2 ? TX_ABORTED : TX_ABORT_FAILED;
    if (ops_.finish != nullptr &&
        ops_.finish(state.dataset, ending, state.abort_sent, ops_.context)) {
      clear(state);
    } else {
      state.retry_at = now + TX_RETRY_MS;
    }
    return;
  }
  if (!reached(now, state.abort_next)) return;
  uint32_t id = 0;
  uint32_t airtime = 0;
  if (ops_.send != nullptr &&
      ops_.send(state.abort_frame, sizeof(state.abort_frame), state.scoped,
                state.key, id, airtime, ops_.context)) {
    state.in_flight = true;
    state.abort_flight = true;
    state.packet_id = id;
    state.sent_at = now;
    state.flight_airtime = airtime;
  } else {
    state.abort_next = now + TX_RETRY_MS;
  }
}

void Transmitter::tick(uint32_t now) {
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) {
    TxState& state = slot_[i];
    if (!state.in_flight || !reached(now, state.sent_at + TX_WAIT_MS)) continue;
    state.in_flight = false;
    if (state.aborting) state.abort_next = now;
    else advance(state);
  }
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) {
    if (slot_[i].in_flight) return;
  }
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) {
    TxState& state = slot_[i];
    if (!state.aborting) continue;
    bool terminal = state.abort_sent >= 2 || reached(now, state.abort_deadline);
    if (!reached(now, terminal ? state.retry_at : state.abort_next)) continue;
    sendAbort(state, now);
    if (state.in_flight) return;
  }
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) {
    TxState& state = slot_[i];
    if (state.active && state.rounds_done >= state.rounds && !state.in_flight) {
      if (!reached(now, state.retry_at)) continue;
      if (ops_.finish != nullptr &&
          ops_.finish(state.dataset, state.sign_failed ? TX_SIGN_FAILED : TX_QUIET,
                      0, ops_.context)) clear(state);
      else state.retry_at = now + TX_RETRY_MS;
    }
  }
  TxState* state = choose(now);
  if (state != nullptr && !state->in_flight && reached(now, state->retry_at)) {
    send(*state, now);
  }
}

void Transmitter::complete(uint32_t id, bool success, uint32_t now) {
  if (id == 0) return;
  for (uint8_t i = 0; i < SYNC_SETTINGS_DATASET_COUNT; ++i) {
    TxState& state = slot_[i];
    if (!state.in_flight || state.packet_id != id) continue;
    state.in_flight = false;
    if (state.aborting) {
      bool notice = state.abort_flight;
      state.abort_flight = false;
      if (!notice) return;
      if (success) ++state.abort_sent;
      state.abort_next = success ? now + ABORT_INTERVAL_MS : now;
    } else {
      advance(state);
      if (success) {
        state.retry_at = now + FLOOD_WAIT_BASE_MS +
                         (uint32_t)FLOOD_WAIT_FACTOR * state.flight_airtime;
      }
    }
    return;
  }
}

void Transmitter::discard(uint8_t dataset) {
  TxState* value = find(dataset);
  if (value != nullptr) clear(*value);
}

bool Transmitter::active(uint8_t dataset) const {
  const TxState* value = find(dataset);
  return value != nullptr && value->active;
}

bool Transmitter::aborting(uint8_t dataset) const {
  const TxState* value = find(dataset);
  return value != nullptr && value->aborting;
}

const TxState* Transmitter::state(uint8_t dataset) const {
  const TxState* value = find(dataset);
  return value != nullptr && (value->active || value->aborting) ? value : nullptr;
}

}  // namespace sync
}  // namespace mobmesh
