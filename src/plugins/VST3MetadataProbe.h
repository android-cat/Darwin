#pragma once

#include <QString>

struct VST3PluginInfo;

namespace Darwin {

bool tryParseVST3MetadataProbeArgs(int argc, char* argv[], QString& modulePath);
int runVST3MetadataProbe(const QString& modulePath);
bool populateInfoFromVST3ProbeProcess(const QString& modulePath,
                                      VST3PluginInfo& info,
                                      int timeoutMs = 8000);

} // namespace Darwin
