/**
 * @file    fuse.h
 * @brief   A fuse that blows by I squared t, stays open for the rest of the run and cools between overloads.
 */

#ifndef IMCSIM_FUSE_H
#define IMCSIM_FUSE_H

#include "components/component.h"
#include <memory>
#include <span>
#include <string>

namespace Core {

/**
 * @struct  FuseParameters
 * @brief   Datasheet figures of a fuse.
 * @details The fuse heats with the square of its current and cools with a time constant, so it never blows below the
 *          rated current and blows a short overload once the current squared times its duration reaches the melting
 *          I squared t. The time constant is worked out from the two, see GetCoolingTimeConstant().
 */
struct FuseParameters {
    /// Largest current the fuse carries for ever, in amperes.
    double RatedCurrent = 1.0;
    /// Current squared times time that melts the element in a short overload, in A^2 s.
    double MeltingI2t = 1.045;
    double ColdResistance = 0.0964;
    double BlownResistance = 1e9;

    bool operator==(const FuseParameters &) const = default;
};

/**
 * @struct  ReadyFuse
 * @brief   A fuse with typical datasheet figures, to pick from the part editor.
 */
struct ReadyFuse {
    const char *Name;
    FuseParameters Parameters;
};

/**
 * @class   Fuse
 * @brief   A two-terminal fuse: a low resistance that opens for good once it has carried too much current for too long.
 * @details The part has no value, and its parameters are set one by one. It is whole again at the start of each run.
 */
class Fuse : public Component {
public:
    Fuse();
    ~Fuse() override = default;

    std::unique_ptr<Component> Clone() const override;

    const FuseParameters &GetParameters() const;
    void SetParameters(const FuseParameters &parameters);
    static bool IsValidParameters(const FuseParameters &parameters);

private:
    FuseParameters m_Parameters;
};

// Figures
double GetCoolingTimeConstant(const FuseParameters &parameters);
std::span<const ReadyFuse> GetReadyFuses();

// Netlist
const char *GetFuseSubcircuitName();
std::string FormatFuseSubcircuit();

} // namespace Core

#endif // IMCSIM_FUSE_H
