/*
 * Copyright (c) 2008 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#include "poisson-helper.h"

#include "ns3/data-rate.h"
#include "ns3/inet-socket-address.h"
#include "ns3/names.h"
#include "ns3/poisson-app.h"
#include "ns3/packet-socket-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

namespace ns3
{

PoissonHelper::PoissonHelper(std::string protocol, Address address)
{
    m_factory.SetTypeId("ns3::PoissonApplication");
    m_factory.Set("Protocol", StringValue(protocol));
    m_factory.Set("Remote", AddressValue(address));
}

void
PoissonHelper::SetAttribute(std::string name, const AttributeValue& value)
{
    m_factory.Set(name, value);
}

ApplicationContainer
PoissonHelper::Install(Ptr<Node> node) const
{
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
PoissonHelper::Install(std::string nodeName) const
{
    Ptr<Node> node = Names::Find<Node>(nodeName);
    return ApplicationContainer(InstallPriv(node));
}

ApplicationContainer
PoissonHelper::Install(NodeContainer c) const
{
    ApplicationContainer apps;
    for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
    {
        apps.Add(InstallPriv(*i));
    }

    return apps;
}

Ptr<Application>
PoissonHelper::InstallPriv(Ptr<Node> node) const
{
    Ptr<Application> app = m_factory.Create<PoissonApplication>();
    node->AddApplication(app);

    return app;
}



void
PoissonHelper::SetConstantRate(DataRate dataRate, uint32_t packetSize)
{
 // PoissonApplication usa 'Interval' y 'PacketGen'
    double intervalSeconds = (double)packetSize * 8 / dataRate.GetBitRate();
    m_factory.Set("Interval", DoubleValue(intervalSeconds));
    m_factory.Set("PacketSize", UintegerValue(packetSize));
    m_factory.Set("PacketGen", StringValue("CBR"));
}

} // namespace ns3
