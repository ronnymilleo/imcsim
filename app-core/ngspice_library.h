/**
 * @file    ngspice_library.h
 * @brief   Starts, feeds and listens to the ngspice shared library.
 * @details ngspice keeps global state, so this module talks to one library instance for the whole process. Calls
 *          must come from one thread, and the callbacks fire on that same thread.
 */

#ifndef IMCSIM_NGSPICE_LIBRARY_H
#define IMCSIM_NGSPICE_LIBRARY_H

#include "simulation_results.h"
#include <filesystem>
#include <string>
#include <vector>

namespace Core {

// Library
bool EnsureInitialized();
int SendCommand(std::string command);
void LoadCircuit(const std::string &netlist);
std::vector<SimulatorMessage> *SetMessageTarget(std::vector<SimulatorMessage> *messages);

// Digital code models
void SetCodeModelDirectory(const std::filesystem::path &directory);
bool AreDigitalModelsAvailable();

} // namespace Core

#endif // IMCSIM_NGSPICE_LIBRARY_H
