/**
 * @file    potentiometer.cpp
 * @brief   A potentiometer in the simulation model.
 */

#include "potentiometer.h"

#include <algorithm>
#include <memory>

namespace Core {

namespace {

constexpr double DEFAULT_RESISTANCE = 10e3;
constexpr double DEFAULT_WIPER = 0.5;
// The two halves of the track never get to 0 Ohm, which SPICE rejects, however close to an end the wiper is
constexpr double MIN_TRACK_RESISTANCE = 1e-3;

} // namespace

/**
 * @brief   Creates a potentiometer of 10 kOhm with its wiper in the middle.
 */
Potentiometer::Potentiometer() : Component(ComponentType::POTENTIOMETER, DEFAULT_RESISTANCE), m_Wiper(DEFAULT_WIPER) {
}

/**
 * @brief   Copies the component with its name, value and settings.
 * @return  An independent component of the same kind.
 */
std::unique_ptr<Component> Potentiometer::Clone() const {
    return std::make_unique<Potentiometer>(*this);
}

/**
 * @brief   Returns where the wiper is.
 * @return  0 with the wiper on terminal 1, 1 with it on terminal 2.
 */
double Potentiometer::GetWiper() const {
    return m_Wiper;
}

/**
 * @brief   Moves the wiper.
 * @param[in] position  New position from 0 to 1; check it with IsValidWiper() first.
 */
void Potentiometer::SetWiper(const double position) {
    m_Wiper = position;
}

/**
 * @brief   Checks whether a wiper position is on the track.
 * @param[in] position  Candidate position.
 * @return  True from 0 to 1, both included.
 */
bool Potentiometer::IsValidWiper(const double position) {
    return position >= 0.0 && position <= 1.0;
}

/**
 * @brief   Returns the resistance of the track between terminal 1 and the wiper.
 * @return  The share of the value up to the wiper, never less than 1 mOhm.
 */
double Potentiometer::GetFirstTrackResistance() const {
    return std::max(GetValue() * m_Wiper, MIN_TRACK_RESISTANCE);
}

/**
 * @brief   Returns the resistance of the track between the wiper and terminal 2.
 * @return  The share of the value past the wiper, never less than 1 mOhm.
 */
double Potentiometer::GetSecondTrackResistance() const {
    return std::max(GetValue() * (1.0 - m_Wiper), MIN_TRACK_RESISTANCE);
}

} // namespace Core
