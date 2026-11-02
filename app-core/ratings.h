/**
 * @file    ratings.h
 * @brief   Warnings about parts a run pushes past their ratings, which ngspice simulates without complaint.
 * @details Read from the results of a run, so every simulation backend shares them.
 */

#ifndef IMCSIM_RATINGS_H
#define IMCSIM_RATINGS_H

#include "circuit.h"
#include "simulator.h"

namespace Core {

void AddRatingWarnings(const Circuit &circuit, OperatingPoint &result);
void AddRatingWarnings(const Circuit &circuit, Transient &result);

} // namespace Core

#endif // IMCSIM_RATINGS_H
