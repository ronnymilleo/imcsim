/**
 * @file    csv_exporter.cpp
 * @brief   Exports simulation results in CSV tabular format.
 */

#include "nodalys/exporters.h"

#include <algorithm>
#include <complex>
#include <format>
#include <numbers>
#include <optional>
#include <sstream>

namespace Nodalys {

namespace {

/// The header of the columns of a matrix of ports, such as Z11_re,Z11_im for the impedance.
void WritePortMatrixHeader(std::ostringstream &ss, const char letter, const int ports) {
    for (int i = 1; i <= ports; ++i) {
        for (int j = 1; j <= ports; ++j) {
            ss << std::format(",{0}{1}{2}_re,{0}{1}{2}_im", letter, i, j);
        }
    }
}

/// The columns of a matrix of ports at one frequency, empty where the circuit has no finite matrix.
void WritePortMatrixRow(std::ostringstream &ss, const std::optional<PortMatrix> &matrix, const int ports) {
    for (int i = 0; i < ports; ++i) {
        for (int j = 0; j < ports; ++j) {
            if (matrix) {
                const std::complex<double> value = (*matrix)[static_cast<std::size_t>(i * ports + j)];
                ss << std::format(",{:.8g},{:.8g}", value.real(), value.imag());
            } else {
                ss << ",,";
            }
        }
    }
}

} // namespace

std::string ExportCsv(const OperatingPointResult &result) {
    std::ostringstream ss;
    ss << "Variable,Type,Value,Unit\n";
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        ss << "V(" << node << "),Voltage," << std::format("{:.8g}", result.NodeVoltages[node]) << ",V\n";
    }
    for (const auto &cur : result.Currents) {
        ss << "I(" << cur.Name << "),Current," << std::format("{:.8g}", cur.Current) << ",A\n";
    }
    return ss.str();
}

std::string ExportCsv(const TransientResult &result) {
    std::ostringstream ss;
    ss << "Time";
    for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
        ss << ",V(" << node << ")";
    }
    for (const auto &cur : result.Currents) {
        ss << ",I(" << cur.Name << ")";
    }
    ss << "\n";

    const std::size_t num_points = result.Times.size();
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        ss << std::format("{:.8g}", result.Times[pt]);
        for (std::size_t node = 1; node < result.NodeVoltages.size(); ++node) {
            const double v = (pt < result.NodeVoltages[node].size()) ? result.NodeVoltages[node][pt] : 0.0;
            ss << "," << std::format("{:.8g}", v);
        }
        for (const auto &cur : result.Currents) {
            const double i = (pt < cur.Values.size()) ? cur.Values[pt] : 0.0;
            ss << "," << std::format("{:.8g}", i);
        }
        ss << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const DcSweepResult &result) {
    std::ostringstream ss;
    if (result.Curves.empty() || result.SweptValues.empty()) {
        return "Empty\n";
    }
    const auto &first_curve = result.Curves.front();
    ss << result.SweptSource;
    if (!result.SteppedSource.empty()) {
        ss << "," << result.SteppedSource;
    }
    for (std::size_t node = 1; node < first_curve.NodeVoltages.size(); ++node) {
        ss << ",V(" << node << ")";
    }
    for (const auto &cur : first_curve.Currents) {
        ss << ",I(" << cur.Name << ")";
    }
    ss << "\n";

    for (const auto &curve : result.Curves) {
        for (std::size_t pt = 0; pt < result.SweptValues.size(); ++pt) {
            ss << std::format("{:.8g}", result.SweptValues[pt]);
            if (!result.SteppedSource.empty()) {
                ss << "," << std::format("{:.8g}", curve.StepValue);
            }
            for (std::size_t node = 1; node < curve.NodeVoltages.size(); ++node) {
                const double v = (pt < curve.NodeVoltages[node].size()) ? curve.NodeVoltages[node][pt] : 0.0;
                ss << "," << std::format("{:.8g}", v);
            }
            for (const auto &cur : curve.Currents) {
                const double i = (pt < cur.Values.size()) ? cur.Values[pt] : 0.0;
                ss << "," << std::format("{:.8g}", i);
            }
            ss << "\n";
        }
    }
    return ss.str();
}

std::string ExportCsv(const AcSweepResult &result) {
    std::ostringstream ss;
    ss << "Frequency_Hz";
    for (std::size_t node = 1; node < result.NodeMagnitudesDecibels.size(); ++node) {
        ss << ",V(" << node << ")_dB,V(" << node << ")_Phase_deg";
    }
    for (const auto &cur : result.CurrentMagnitudesDecibels) {
        ss << ",I(" << cur.Name << ")_dB,I(" << cur.Name << ")_Phase_deg";
    }
    ss << "\n";

    const std::size_t num_points = result.Frequencies.size();
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        ss << std::format("{:.8g}", result.Frequencies[pt]);
        for (std::size_t node = 1; node < result.NodeMagnitudesDecibels.size(); ++node) {
            const double db =
                (pt < result.NodeMagnitudesDecibels[node].size()) ? result.NodeMagnitudesDecibels[node][pt] : 0.0;
            const double deg = (pt < result.NodePhasesDegrees[node].size()) ? result.NodePhasesDegrees[node][pt] : 0.0;
            ss << "," << std::format("{:.8g},{:.8g}", db, deg);
        }
        for (std::size_t c = 0; c < result.CurrentMagnitudesDecibels.size(); ++c) {
            const double db = (pt < result.CurrentMagnitudesDecibels[c].Values.size())
                                ? result.CurrentMagnitudesDecibels[c].Values[pt]
                                : 0.0;
            const double deg =
                (pt < result.CurrentPhasesDegrees[c].Values.size()) ? result.CurrentPhasesDegrees[c].Values[pt] : 0.0;
            ss << "," << std::format("{:.8g},{:.8g}", db, deg);
        }
        ss << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const TransferFunctionResult &result) {
    std::ostringstream ss;
    ss << "Metric,Value,Unit\n";
    ss << "Gain," << std::format("{:.8g}", result.Gain) << "," << result.GainUnit << "\n";
    ss << "InputImpedance," << std::format("{:.8g}", result.InputImpedance) << ",Ohm\n";
    ss << "OutputImpedance," << std::format("{:.8g}", result.OutputImpedance) << ",Ohm\n";
    return ss.str();
}

std::string ExportCsv(const NoiseResult &result) {
    std::ostringstream ss;
    ss << "Frequency_Hz,OutputDensity_V_rtHz,InputDensity";
    for (const auto &c : result.Contributions) {
        ss << "," << c.Name << "_Density";
    }
    ss << "\n";

    const std::size_t num_points = result.Frequencies.size();
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        ss << std::format("{:.8g}", result.Frequencies[pt]);
        ss << "," << std::format("{:.8g}", result.OutputDensities[pt]);
        ss << "," << std::format("{:.8g}", result.InputDensities[pt]);
        for (const auto &c : result.Contributions) {
            const double dens = (pt < c.OutputDensities.size()) ? c.OutputDensities[pt] : 0.0;
            ss << "," << std::format("{:.8g}", dens);
        }
        ss << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const SParameterResult &result) {
    std::ostringstream ss;
    ss << "Frequency_Hz";
    const int ports = result.GetPortCount();
    for (int i = 1; i <= ports; ++i) {
        for (int j = 1; j <= ports; ++j) {
            ss << std::format(",S{0}{1}_dB,S{0}{1}_deg,S{0}{1}_re,S{0}{1}_im", i, j);
        }
    }
    const bool has_matrices = result.ImpedanceMatrices.size() == result.Frequencies.size()
                           && result.AdmittanceMatrices.size() == result.Frequencies.size();
    if (has_matrices) {
        WritePortMatrixHeader(ss, 'Z', ports);
        WritePortMatrixHeader(ss, 'Y', ports);
    }
    ss << "\n";

    constexpr double RAD_TO_DEG = 180.0 / std::numbers::pi;
    for (std::size_t pt = 0; pt < result.Frequencies.size(); ++pt) {
        ss << std::format("{:.8g}", result.Frequencies[pt]);
        for (int i = 1; i <= ports; ++i) {
            for (int j = 1; j <= ports; ++j) {
                const auto s = result.Get(pt, i, j);
                const double mag = std::abs(s);
                const double mag_db = 20.0 * std::log10(std::max(mag, 1e-15));
                const double phase_deg = std::arg(s) * RAD_TO_DEG;
                ss << std::format(",{:.8g},{:.8g},{:.8g},{:.8g}", mag_db, phase_deg, s.real(), s.imag());
            }
        }
        if (has_matrices) {
            WritePortMatrixRow(ss, result.ImpedanceMatrices[pt], ports);
            WritePortMatrixRow(ss, result.AdmittanceMatrices[pt], ports);
        }
        ss << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const PoleZeroResult &result) {
    std::ostringstream ss;
    constexpr double TWO_PI = 2.0 * std::numbers::pi;
    ss << "Type,Index,Real (rad/s),Imag (rad/s),Frequency (Hz)\n";
    for (std::size_t i = 0; i < result.Poles.size(); ++i) {
        const double f_hz = std::abs(result.Poles[i]) / TWO_PI;
        ss << "Pole," << (i + 1) << "," << std::format("{:.8g}", result.Poles[i].real()) << ","
           << std::format("{:.8g}", result.Poles[i].imag()) << "," << std::format("{:.8g}", f_hz) << "\n";
    }
    for (std::size_t i = 0; i < result.Zeros.size(); ++i) {
        const double f_hz = std::abs(result.Zeros[i]) / TWO_PI;
        ss << "Zero," << (i + 1) << "," << std::format("{:.8g}", result.Zeros[i].real()) << ","
           << std::format("{:.8g}", result.Zeros[i].imag()) << "," << std::format("{:.8g}", f_hz) << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const SensitivityResult &result) {
    std::ostringstream ss;
    ss << "Vector,Value\n";
    for (const auto &[name, val] : result.Vectors) {
        ss << name << "," << std::format("{:.8g}", val) << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const AcSensitivityResult &result) {
    std::ostringstream ss;
    ss << "Frequency,Output_re,Output_im";
    for (const AcElementSensitivity &element : result.Elements) {
        ss << std::format(",{0}_dB_per_percent,{0}_deg_per_percent", element.Name);
    }
    ss << "\n";
    for (std::size_t pt = 0; pt < result.Frequencies.size(); ++pt) {
        ss << std::format("{:.8g},{:.8g},{:.8g}",
                          result.Frequencies[pt],
                          result.Output[pt].real(),
                          result.Output[pt].imag());
        for (const AcElementSensitivity &element : result.Elements) {
            // Empty where the output is too small for a relative change to mean anything
            for (const std::optional<double> &figure :
                 {element.MagnitudeDecibelsPerPercent[pt], element.PhaseDegreesPerPercent[pt]}) {
                ss << ",";
                if (figure) {
                    // Adding zero turns the negative zero of a part with no effect into "0", not "-0"
                    ss << std::format("{:.8g}", *figure + 0.0);
                }
            }
        }
        ss << "\n";
    }
    return ss.str();
}

std::string ExportCsv(const HarmonicBalanceResult &result) {
    std::ostringstream ss;
    constexpr double RAD_TO_DEG = 180.0 / std::numbers::pi;

    ss << "Harmonic,Frequency_Hz";
    for (std::size_t node = 1; node < result.NodePhasors.size(); ++node) {
        ss << std::format(",V({0})_Mag,V({0})_Phase_deg,V({0})_Re,V({0})_Im", node);
    }
    for (const auto &cur : result.CurrentPhasors) {
        ss << std::format(",I({0})_Mag,I({0})_Phase_deg,I({0})_Re,I({0})_Im", cur.Name);
    }
    ss << "\n";

    const std::size_t num_harmonics = result.Frequencies.size();
    for (std::size_t h = 0; h < num_harmonics; ++h) {
        ss << h << "," << std::format("{:.8g}", result.Frequencies[h]);
        for (std::size_t node = 1; node < result.NodePhasors.size(); ++node) {
            const auto phasor =
                (h < result.NodePhasors[node].size()) ? result.NodePhasors[node][h] : std::complex<double>{};
            const double mag = std::abs(phasor);
            const double phase_deg = std::arg(phasor) * RAD_TO_DEG;
            ss << std::format(",{:.8g},{:.8g},{:.8g},{:.8g}", mag, phase_deg, phasor.real(), phasor.imag());
        }
        for (const auto &cur : result.CurrentPhasors) {
            const auto phasor = (h < cur.Phasors.size()) ? cur.Phasors[h] : std::complex<double>{};
            const double mag = std::abs(phasor);
            const double phase_deg = std::arg(phasor) * RAD_TO_DEG;
            ss << std::format(",{:.8g},{:.8g},{:.8g},{:.8g}", mag, phase_deg, phasor.real(), phasor.imag());
        }
        ss << "\n";
    }

    if (result.NodePhasors.size() > 1 || !result.CurrentPhasors.empty()) {
        ss << "\nSignal,THD_Percent\n";
        for (std::size_t node = 1; node < result.NodePhasors.size(); ++node) {
            ss << "V(" << node << ")," << std::format("{:.6g}", ComputeThd(result.NodePhasors[node]) * 100.0) << "\n";
        }
        for (const auto &cur : result.CurrentPhasors) {
            ss << "I(" << cur.Name << ")," << std::format("{:.6g}", ComputeThd(cur.Phasors) * 100.0) << "\n";
        }
    }

    if (!result.Times.empty()) {
        ss << "\nTime_s";
        for (std::size_t node = 1; node < result.NodeWaveforms.size(); ++node) {
            ss << ",V(" << node << ")";
        }
        for (const auto &cur : result.CurrentWaveforms) {
            ss << ",I(" << cur.Name << ")";
        }
        ss << "\n";

        for (std::size_t pt = 0; pt < result.Times.size(); ++pt) {
            ss << std::format("{:.8g}", result.Times[pt]);
            for (std::size_t node = 1; node < result.NodeWaveforms.size(); ++node) {
                const double val = (pt < result.NodeWaveforms[node].size()) ? result.NodeWaveforms[node][pt] : 0.0;
                ss << "," << std::format("{:.8g}", val);
            }
            for (const auto &cur : result.CurrentWaveforms) {
                const double val = (pt < cur.Values.size()) ? cur.Values[pt] : 0.0;
                ss << "," << std::format("{:.8g}", val);
            }
            ss << "\n";
        }
    }

    return ss.str();
}

} // namespace Nodalys
