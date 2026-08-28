// Every CLI command the mods add, dispatched from MyMesh ahead of upstream's own chain so
// a mod can claim a longer prefix first. Free functions, not CommonCLI members: the hook
// is called from a file that has no CommonCLI instance in scope.
#include <helpers/CommonCLI.h>
#include <helpers/ModHooks.h>
#include <target.h>

// Set by the dispatcher each call, so the `ver` branch can answer without reaching into
// CommonCLI for a callback it no longer has.
static const char* mod_fw_version = "";
static const char* mod_fw_build_date = "";

bool handleModCommand(uint32_t sender_timestamp, char* command, char* reply) {
  return false;
}

bool handleModSetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  return false;
}

bool handleModGetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  return false;
}

bool modHandleCliCommand(uint32_t sender_timestamp, char* command, char* reply,
                         const char* fw_version, const char* fw_build_date) {
  mod_fw_version = fw_version;
  mod_fw_build_date = fw_build_date;
  // `set`/`get` reach upstream through the same entry point, so the prefix is routed here
  // rather than at three separate insertion points inside CommonCLI.
  if (memcmp(command, "set ", 4) == 0) return handleModSetCmd(sender_timestamp, command, reply);
  if (memcmp(command, "get ", 4) == 0) return handleModGetCmd(sender_timestamp, command, reply);
  return handleModCommand(sender_timestamp, command, reply);
}
