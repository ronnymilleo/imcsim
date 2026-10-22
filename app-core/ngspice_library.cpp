/**
 * @file    ngspice_library.cpp
 * @brief   Starts, feeds and listens to the ngspice shared library.
 * @details ngspice keeps global state, so this module talks to one library instance for the whole process. Calls
 *          must come from one thread, and the callbacks fire on that same thread.
 */

#include "ngspice_library.h"

#include <algorithm>
#include <ngspice/sharedspice.h>
#include <sstream>
#include <string_view>
#include <utility>

namespace Core {

namespace {

constexpr std::string_view OUTPUT_PREFIX = "stdout ";

constexpr std::string_view ERROR_PREFIX = "stderr ";

/**
 * @struct  NgspiceState
 * @brief   Library-wide state shared with the ngspice callbacks.
 */
struct NgspiceState {
    bool Initialized = false;
    // Set when ngspice asks to quit; the library cannot be used again in this process
    bool Exited = false;
    // Where the callbacks store printed lines; only set while a run is in progress
    std::vector<SimulatorMessage> *Messages = nullptr;
    // Where a build that ships the digital code models keeps digital.cm, when the system ngspice does not load them
    std::filesystem::path CodeModelDirectory;
    // Whether the digital code models are loaded, which the logic parts need; known once ngspice is initialized
    bool HasDigitalModels = false;
};

NgspiceState &GetState() {
    static NgspiceState state;
    return state;
}

// ngspice prefixes every line with the stream it was printed to
int HandleOutput(char *text, int /*id*/, void * /*user_data*/) {
    std::vector<SimulatorMessage> *messages = GetState().Messages;
    if (messages == nullptr || text == nullptr) {
        return 0;
    }
    std::string_view line(text);
    const bool from_error_stream = line.starts_with(ERROR_PREFIX);
    if (from_error_stream) {
        line.remove_prefix(ERROR_PREFIX.size());
    } else if (line.starts_with(OUTPUT_PREFIX)) {
        line.remove_prefix(OUTPUT_PREFIX.size());
    }
    if (!line.empty()) {
        messages->push_back({std::string(line), from_error_stream});
    }
    return 0;
}

int HandleStatus(char * /*status*/, int /*id*/, void * /*user_data*/) {
    return 0;
}

int HandleExit(int /*status*/, NG_BOOL /*immediate*/, NG_BOOL /*quit*/, int /*id*/, void * /*user_data*/) {
    GetState().Exited = true;
    return 0;
}

// The help of a code model is printed only when the model is loaded
bool AskForDigitalModels() {
    std::vector<SimulatorMessage> messages;
    std::vector<SimulatorMessage> *const printing = std::exchange(GetState().Messages, &messages);
    SendCommand("devhelp d_and");
    GetState().Messages = printing;
    return std::ranges::any_of(messages,
                               [](const SimulatorMessage &message) { return message.Text.starts_with("d_and"); });
}

// The codemodel command keeps quotes as part of the file name and splits it at spaces, so the library is named
// relative to its folder, made the working directory for the call; on Windows ngspice resolves a relative name
// against the program's folder instead, which is where the Windows build keeps it
void LoadLibraryFrom(const std::filesystem::path &directory) {
    std::error_code error;
    const std::filesystem::path previous = std::filesystem::current_path(error);
    std::filesystem::current_path(directory, error);
    if (error) {
        return;
    }
    SendCommand("codemodel ./digital.cm");
    std::filesystem::current_path(previous, error);
}

// A system ngspice loads digital.cm through its spinit file, and loading it a second time crashes ngspice, so the
// copy shipped with the program is loaded only when the digital models are missing
void LoadDigitalModels() {
    const std::filesystem::path &directory = GetState().CodeModelDirectory;
    std::error_code error;
    if (!AskForDigitalModels() && !directory.empty() && std::filesystem::exists(directory / "digital.cm", error)) {
        LoadLibraryFrom(directory);
    }
    GetState().HasDigitalModels = AskForDigitalModels();
}

} // namespace

/**
 * @brief   Starts ngspice the first time and loads the digital code models.
 * @return  True when the library can run; false after ngspice asked to quit, as it cannot be used again in this
 *          process.
 */
bool EnsureInitialized() {
    NgspiceState &state = GetState();
    if (!state.Initialized) {
        ngSpice_Init(HandleOutput, HandleStatus, HandleExit, nullptr, nullptr, nullptr, nullptr);
        state.Initialized = true;
        LoadDigitalModels();
    }
    return !state.Exited;
}

/**
 * @brief   Sends one command to ngspice, such as "run".
 * @param[in] command  Command line.
 * @return  The return code of ngspice, which stays 0 even when the command fails: errors show in the messages.
 * @note    ngspice takes non-const strings and keeps no reference to them after the call.
 */
int SendCommand(std::string command) {
    return ngSpice_Command(command.data());
}

/**
 * @brief   Hands a netlist to ngspice as the current circuit.
 * @param[in] netlist  Netlist, one element per line, ending with ".end".
 */
void LoadCircuit(const std::string &netlist) {
    std::vector<std::string> lines;
    std::istringstream stream(netlist);
    for (std::string line; std::getline(stream, line);) {
        lines.push_back(std::move(line));
    }
    std::vector<char *> pointers;
    for (std::string &line : lines) {
        pointers.push_back(line.data());
    }
    pointers.push_back(nullptr);
    ngSpice_Circ(pointers.data());
}

/**
 * @brief   Sets where the lines ngspice prints go.
 * @param[in] messages  Vector that collects them, or null to drop them.
 * @return  The previous target, to put back when a nested call is done.
 */
std::vector<SimulatorMessage> *SetMessageTarget(std::vector<SimulatorMessage> *messages) {
    return std::exchange(GetState().Messages, messages);
}

/**
 * @brief   Tells the simulator where the program keeps the digital code models, for a build that ships them.
 * @param[in] directory  Folder that may hold digital.cm; the library is loaded from it when ngspice has not loaded
 *                       the digital models itself, as the system ngspice does through its spinit file.
 * @note    Call it before the first simulation; the library is looked for once, when ngspice starts.
 */
void SetCodeModelDirectory(const std::filesystem::path &directory) {
    GetState().CodeModelDirectory = directory;
}

/**
 * @brief   Tells whether the digital code models of ngspice are loaded, which the logic gates and flip-flops need.
 * @return  True when ngspice has them, from the system or from the code model directory; false when it has not,
 *          or when ngspice has stopped.
 */
bool AreDigitalModelsAvailable() {
    return EnsureInitialized() && GetState().HasDigitalModels;
}

} // namespace Core
