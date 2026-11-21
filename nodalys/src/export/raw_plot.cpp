/**
 * @file    raw_plot.cpp
 * @brief   One plot of a Nutmeg SPICE rawfile: the header, the variables and the rows of values.
 */

#include "export/raw_plot.h"

#include <format>
#include <iterator>
#include <utility>

namespace Nodalys {

namespace {

/**
 * @brief   The date every rawfile carries.
 * @details A fixed date and not the current one, so the same simulation always writes the same bytes and the files
 *          can be compared.
 */
constexpr std::string_view FIXED_RAW_FILE_DATE = "Mon Nov  9 22:00:00 2026";

} // namespace

/**
 * @brief   Starts a plot with no variables and no points.
 * @param[in] plotname    Name of the plot, such as "Transient Analysis".
 * @param[in] is_complex  Whether the values are complex, or real.
 */
RawPlot::RawPlot(std::string plotname, const bool is_complex)
    : m_Plotname(std::move(plotname)), m_IsComplex(is_complex) {
}

/**
 * @brief   Appends a variable to the list, numbered after the ones already there.
 * @param[in] name  Name of the variable, such as "v(1)".
 * @param[in] type  Kind of the variable, such as "voltage".
 */
void RawPlot::AddVariable(std::string name, std::string type) {
    m_Variables.push_back({.Name = std::move(name), .Type = std::move(type)});
}

/**
 * @brief   Starts a new point; its first cell carries the number of the point.
 */
void RawPlot::BeginPoint() {
    ++m_PointCount;
    m_IsPointStart = true;
}

/**
 * @brief   Appends a real value to the current point.
 * @param[in] value  Value to write.
 */
void RawPlot::AddReal(const double value) {
    AddText(std::format("{:.15e}", value));
}

/**
 * @brief   Appends a complex value to the current point.
 * @param[in] value  Value to write, as its real and imaginary parts.
 */
void RawPlot::AddComplex(const std::complex<double> value) {
    AddText(std::format("{:.15e},{:.15e}", value.real(), value.imag()));
}

/**
 * @brief   Appends a value that is already written as text to the current point.
 * @param[in] cell  Text of the value, without its line break.
 * @note    Call BeginPoint() first.
 */
void RawPlot::AddText(const std::string_view cell) {
    if (m_IsPointStart) {
        std::format_to(std::back_inserter(m_Values), "{}\t", m_PointCount - 1);
        m_IsPointStart = false;
    } else {
        m_Values += '\t';
    }
    m_Values += cell;
    m_Values += '\n';
}

/**
 * @brief   Writes the plot as the text of a rawfile.
 * @param[in] title  Title of the simulation, written in the header.
 * @return  The header, the variables and the values.
 */
std::string RawPlot::ToText(const std::string_view title) const {
    std::string text = std::format("Title: {}\nDate: {}\nPlotname: {}\nFlags: {}\nNo. Variables: {}\nNo. Points: {}\n"
                                   "Variables:\n",
                                   title,
                                   FIXED_RAW_FILE_DATE,
                                   m_Plotname,
                                   m_IsComplex ? "complex" : "real",
                                   m_Variables.size(),
                                   m_PointCount);
    for (std::size_t index = 0; index < m_Variables.size(); ++index) {
        std::format_to(std::back_inserter(text),
                       "\t{}\t{}\t{}\n",
                       index,
                       m_Variables[index].Name,
                       m_Variables[index].Type);
    }
    text += "Values:\n";
    text += m_Values;
    return text;
}

} // namespace Nodalys
