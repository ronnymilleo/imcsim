/**
 * @file    netlist_sources.h
 * @brief   Writes the netlist line of an independent source, with its waveform.
 */

#ifndef IMCSIM_NETLIST_SOURCES_H
#define IMCSIM_NETLIST_SOURCES_H

#include "circuit/components/source.h"
#include <string>
#include <vector>

namespace Core {

std::string FormatSource(const Source &source, const std::vector<int> &nodes);

} // namespace Core

#endif // IMCSIM_NETLIST_SOURCES_H
