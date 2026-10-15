/**
 * @file    net_label.h
 * @brief   A net label in the simulation model: a name that joins every point labeled with it into one node.
 */

#ifndef IMCSIM_NET_LABEL_H
#define IMCSIM_NET_LABEL_H

#include "component.h"
#include <string>
#include <string_view>

namespace Core {

/**
 * @class   NetLabel
 * @brief   Names the node it touches; labels with the same name are the same node, even with no wire between them.
 * @details A label gives no line in the netlist: it only changes which points are joined. A net named "0" or "GND" is
 *          ground, and one named "VCC" joins every VCC supply. Names are compared without regard to case, as SPICE does
 *          for nodes.
 */
class NetLabel : public Component {
public:
    NetLabel();
    ~NetLabel() override = default;

    const std::string &GetNet() const;
    void SetNet(std::string net);

    static bool IsValidNet(std::string_view net);
    static std::string NormalizeNet(std::string_view net);

private:
    std::string m_Net;
};

} // namespace Core

#endif // IMCSIM_NET_LABEL_H
