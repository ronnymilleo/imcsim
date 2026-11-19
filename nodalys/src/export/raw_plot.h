/**
 * @file    raw_plot.h
 * @brief   One plot of a Nutmeg SPICE rawfile: the header, the variables and the rows of values.
 */

#ifndef NODALYS_RAW_PLOT_H
#define NODALYS_RAW_PLOT_H

#include <complex>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Nodalys {

/**
 * @class   RawPlot
 * @brief   One plot of a rawfile in ASCII form, built one variable and one cell at a time.
 * @details Every rawfile plot has the same shape: a header with the title, the date, the plot name and the counts, the
 *          list of variables, and the values point by point. Only the plot name, the flag that says whether the
 *          values are real or complex, the variables and the values change from one analysis to the next, so each
 *          exporter adds those and ToText() writes the rest.
 */
class RawPlot {
public:
    RawPlot(std::string plotname, bool is_complex);

    // Variables
    void AddVariable(std::string name, std::string type);

    // Values
    void BeginPoint();
    void AddReal(double value);
    void AddComplex(std::complex<double> value);
    void AddText(std::string_view cell);

    // Output
    std::string ToText(std::string_view title) const;

private:
    struct Variable {
        std::string Name;
        std::string Type;
    };

    std::string m_Plotname;
    bool m_IsComplex;
    std::vector<Variable> m_Variables;
    std::size_t m_PointCount = 0;
    /// The rows written so far, in the text of the rawfile.
    std::string m_Values;
    /// Whether the next cell is the first of its point, the one that carries the point number.
    bool m_IsPointStart = false;
};

} // namespace Nodalys

#endif // NODALYS_RAW_PLOT_H
