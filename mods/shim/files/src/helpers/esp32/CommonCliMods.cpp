// Hook points for mod CLI commands, dispatched ahead of upstream's own chain so a
// mod can match a longer prefix first. No-ops until a mod fills them in.
#include <helpers/CommonCLI.h>
#include <helpers/ModHooks.h>
#include <target.h>

bool CommonCLI::handleModCommand(uint32_t sender_timestamp, char* command, char* reply) {
  return false;
}

bool CommonCLI::handleModSetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  return false;
}

bool CommonCLI::handleModGetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  return false;
}
