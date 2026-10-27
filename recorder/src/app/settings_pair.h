#pragma once

// Publish the name only after its password is durable. Read-back also handles
// Preferences::putString's ambiguous zero result for an empty password.
template <typename Text, typename Read, typename Write>
bool saveSettingsPair(const Text& name, const Text& password, Read read, Write write) {
  const Text oldName = read(false), oldPassword = read(true);
  write(true, password);
  if (read(true) != password) { write(true, oldPassword); return false; }
  write(false, name);
  if (read(false) == name) return true;
  // Best-effort rollback; never report a partially saved profile as success.
  write(true, oldPassword);
  write(false, oldName);
  return false;
}
