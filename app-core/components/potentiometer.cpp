/**
 * @file    potentiometer.cpp
 * @brief   A potentiometer in the simulation model.
 */

#include "potentiometer.h"

namespace Core {

namespace {

constexpr double DEFAULT_RESISTANCE = 10e3;
constexpr double DEFAULT_WIPER = 0.5;

} // namespace

/**
 * @brief   Creates a potentiometer of 10 kOhm with its wiper in the middle.
 */
Potentiometer::Potentiometer() : Component(ComponentType::POTENTIOMETER, DEFAULT_RESISTANCE), m_Wiper(DEFAULT_WIPER) {
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

} // namespace Core
