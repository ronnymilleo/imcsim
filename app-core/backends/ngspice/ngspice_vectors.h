/**
 * @file    ngspice_vectors.h
 * @brief   Reads vectors from the current plot of ngspice and names the ones the results come from.
 * @details ngGet_Vec_Info fills one static record, so a vector's values are copied before the next one is looked up.
 */

#ifndef IMCSIM_NGSPICE_VECTORS_H
#define IMCSIM_NGSPICE_VECTORS_H

#include "circuit/components/component.h"
#include <complex>
#include <cstddef>
#include <ngspice/sharedspice.h>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  CurrentVector
 * @brief   A current the results report, and the ngspice vector it is read from.
 */
struct CurrentVector {
    std::string TraceName;
    std::string VectorName;
};

// Vectors of the current plot
pvector_info FindVector(std::string name);
std::vector<double> RealPart(const vector_info &vector, std::size_t first_point = 0);
std::vector<std::complex<double>> ComplexValues(const vector_info &vector);
std::vector<std::string> ListVectors(std::string plot);

// Names
std::string ToLower(std::string text);
std::string BranchVectorName(const std::string &element_name);
std::vector<CurrentVector> CurrentVectors(const Component &component);

} // namespace Core

#endif // IMCSIM_NGSPICE_VECTORS_H
