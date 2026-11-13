/**
 * @file    check_fixtures.h
 * @brief   The small circuit the tests of the checks share.
 */

#ifndef IMCSIM_CHECK_FIXTURES_H
#define IMCSIM_CHECK_FIXTURES_H

#include "circuit/circuit.h"
#include "circuit/components/ground.h"
#include "circuit/components/resistor.h"
#include "circuit/components/voltage_source.h"

/// A 5 V source across a 1k resistor to ground: node 1 is the source, ground is node 0.
struct SourceAndResistor {
    Core::VoltageSource Source;
    Core::Resistor Load;
    Core::Ground Reference;
    Core::Circuit Circuit;

    SourceAndResistor() {
        Source.SetName("Vin1");
        Load.SetName("R1");
        Load.SetValue(1e3);
        Circuit.Add(Source, {1, 0});
        Circuit.Add(Load, {1, 0});
        Circuit.Add(Reference, {0});
    }
};

#endif // IMCSIM_CHECK_FIXTURES_H
