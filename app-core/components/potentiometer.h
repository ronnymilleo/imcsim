/**
 * @file    potentiometer.h
 * @brief   A potentiometer in the simulation model.
 */

#ifndef IMCSIM_POTENTIOMETER_H
#define IMCSIM_POTENTIOMETER_H

#include "component.h"
#include <memory>

namespace Core {

/**
 * @class   Potentiometer
 * @brief   A resistor with a wiper that taps it at any point.
 * @details The value is the resistance between the two ends, terminals 1 and 2; the wiper is terminal 3. The wiper
 *          position goes from 0, at terminal 1, to 1, at terminal 2, and splits the value into two resistors.
 */
class Potentiometer : public Component {
public:
    Potentiometer();
    ~Potentiometer() override = default;

    std::unique_ptr<Component> Clone() const override;

    double GetWiper() const;
    void SetWiper(double position);
    static bool IsValidWiper(double position);
    double GetFirstTrackResistance() const;
    double GetSecondTrackResistance() const;

private:
    double m_Wiper;
};

} // namespace Core

#endif // IMCSIM_POTENTIOMETER_H
