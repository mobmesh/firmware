#include <stdio.h>
#include <string.h>
#include <helpers/CoreIntegration.h>

bool coreHandleCli(const ModCliContext& context, char* command, char* reply) {
  // Every MobMesh build names its own build after upstream's, whichever mods it carries.
  if (memcmp(command, "ver", 3) == 0) {
    sprintf(reply, "%s (%s) + MobMesh (%s)", context.fw_version, context.fw_build_date, OTA_MOD_BUILD_DATE);
    return true;
  }
  // Upstream's poweroff never wakes, so it is never reachable; a mod that can wake handles it first.
  if (memcmp(command, "poweroff", 8) == 0 || memcmp(command, "shutdown", 8) == 0) {
    strcpy(reply, "ERR: poweroff not available in this build");
    return true;
  }
  return false;
}
