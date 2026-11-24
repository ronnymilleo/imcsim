/**
 * @file    compiled_circuit.cpp
 * @brief   Internal structures holding the assembled MNA system and stamped device models.
 */

#include "compiler/compiled_circuit.h"

#include <unordered_map>
#include <utility>

namespace Nodalys {

namespace {

Device *RemapDevice(const std::unordered_map<const Device *, Device *> &device_map, Device *const device) {
    const auto found = device_map.find(device);
    return found != device_map.end() ? found->second : device;
}

const Device *RemapDevice(const std::unordered_map<const Device *, Device *> &device_map, const Device *const device) {
    const auto found = device_map.find(device);
    return found != device_map.end() ? found->second : device;
}

std::unique_ptr<MnaSystem> MakeEmptySystemFrom(const MnaSystem &system) {
    const int size = system.GetSize();
    std::vector<bool> node_flags(static_cast<std::size_t>(size), false);
    for (int index = 0; index < size; ++index) {
        node_flags[static_cast<std::size_t>(index)] = system.IsNodeUnknown(index);
    }
    return std::make_unique<MnaSystem>(std::move(node_flags));
}

std::vector<CurrentRef> RemapCurrentRefs(const std::vector<CurrentRef> &refs,
                                         const std::unordered_map<const Device *, Device *> &device_map) {
    std::vector<CurrentRef> mapped_refs;
    mapped_refs.reserve(refs.size());
    for (const CurrentRef &ref : refs) {
        CurrentRef mapped{.Name = ref.Name, .Contributions = {}};
        mapped.Contributions.reserve(ref.Contributions.size());
        for (const CurrentContribution &contribution : ref.Contributions) {
            mapped.Contributions.push_back(
                {.Element = RemapDevice(device_map, contribution.Element), .Terminal = contribution.Terminal});
        }
        mapped_refs.push_back(std::move(mapped));
    }
    return mapped_refs;
}

std::vector<NoiseGroup> RemapNoiseGroups(const std::vector<NoiseGroup> &groups,
                                         const std::unordered_map<const Device *, Device *> &device_map) {
    std::vector<NoiseGroup> mapped_groups;
    mapped_groups.reserve(groups.size());
    for (const NoiseGroup &group : groups) {
        NoiseGroup mapped{.Name = group.Name, .Devices = {}};
        mapped.Devices.reserve(group.Devices.size());
        for (const Device *const device : group.Devices) {
            mapped.Devices.push_back(RemapDevice(device_map, device));
        }
        mapped_groups.push_back(std::move(mapped));
    }
    return mapped_groups;
}

} // namespace

/**
 * @brief   Clones a compiled circuit into an isolated simulation run state.
 * @details Allocates a fresh MNA matrix layout and independent device clones, then remaps every non-owning device
 *          pointer in Currents, NoiseGroups, SourcesByName and ElementsByName to the cloned instances so analyses
 *          never mutate the blueprint circuit.
 * @param[in] blueprint  Immutable compiled circuit held by CircuitImpl.
 * @return  An independent CompiledCircuit ready for a single analysis run.
 */
CompiledCircuit CloneCompiledCircuit(const CompiledCircuit &blueprint) {
    CompiledCircuit copy{.System = blueprint.System ? MakeEmptySystemFrom(*blueprint.System) : nullptr,
                         .Devices = {},
                         .CompiledNodeCount = blueprint.CompiledNodeCount,
                         .CircuitNodeCount = blueprint.CircuitNodeCount,
                         .Currents = {},
                         .NoiseGroups = {},
                         .DynamicNode = blueprint.DynamicNode,
                         .SourcesByName = {},
                         .ElementsByName = {},
                         .Ports = blueprint.Ports,
                         .InitialConditions = blueprint.InitialConditions,
                         .Nodesets = blueprint.Nodesets,
                         .NodeSeeds = blueprint.NodeSeeds,
                         .HasInitialConditions = blueprint.HasInitialConditions,
                         .Thermal = blueprint.Thermal};

    copy.Devices.reserve(blueprint.Devices.size());
    std::unordered_map<const Device *, Device *> device_map;
    device_map.reserve(blueprint.Devices.size());
    for (const std::unique_ptr<Device> &device : blueprint.Devices) {
        std::unique_ptr<Device> cloned = device->Clone();
        cloned->SetTemperature(blueprint.Thermal);
        device_map.emplace(device.get(), cloned.get());
        copy.Devices.push_back(std::move(cloned));
    }

    copy.Currents = RemapCurrentRefs(blueprint.Currents, device_map);
    copy.NoiseGroups = RemapNoiseGroups(blueprint.NoiseGroups, device_map);
    for (const auto &[name, device] : blueprint.SourcesByName) {
        copy.SourcesByName.emplace(name, RemapDevice(device_map, device));
    }
    for (const auto &[name, device] : blueprint.ElementsByName) {
        copy.ElementsByName.emplace(name, RemapDevice(device_map, device));
    }
    return copy;
}

} // namespace Nodalys
