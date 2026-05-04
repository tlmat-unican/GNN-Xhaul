/*
 * Copyright (c) 2017 Universita' degli Studi di Napoli Federico II
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
 * Authors:  Stefano Avallone <stavallo@unina.it>
 */

#include "prio-queue-dscp-disc.h"





#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/pointer.h"
#include "ns3/socket.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/udp-l4-protocol.h"
#include "ns3/udp-header.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/wdrr-queue-disc.h"
#include <algorithm>
#include <iterator>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PrioQueueDscpDisc");

NS_OBJECT_ENSURE_REGISTERED(PrioQueueDscpDisc);



TypeId
PrioQueueDscpDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::PrioQueueDscpDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<PrioQueueDscpDisc>()
            .AddAttribute("MapQueue",
                        "It can be used in order to map dscp marking with the queues",
                        MapQueueValue(MapQueue{{1, 2},{2, 4}}),
                        MakeMapQueueAccessor(&PrioQueueDscpDisc::mapuca),
                        MakeMapQueueChecker())
            .AddAttribute("MarkingQueue",
                "It can be used in order to map dscp marking with the queues",
                MapQueueValue(MapQueue{{8080, 0x2E},{8081, 4}}),
                MakeMapQueueAccessor(&PrioQueueDscpDisc::markingMap),
                MakeMapQueueChecker())
            .AddAttribute("Bands",
                "Number of bands (queues) to be used in the PrioQueueDscpDisc",
                UintegerValue(2),
                MakeUintegerAccessor(&PrioQueueDscpDisc::nBands),
                MakeUintegerChecker<uint32_t>());

    return tid;
}

PrioQueueDscpDisc::PrioQueueDscpDisc()
    : QueueDisc(QueueDiscSizePolicy::NO_LIMITS)
{
    NS_LOG_FUNCTION(this);
}

PrioQueueDscpDisc::~PrioQueueDscpDisc()
{
    NS_LOG_FUNCTION(this);
}

Ptr<QueueDiscItem>
PrioQueueDscpDisc::MarkingPacket(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
    Ipv4Header ipHeader = ipItem->GetHeader();

    // Copia del paquete para inspección
    Ptr<Packet> writablePacket = ipItem->GetPacket()->Copy();

    Ipv4Header ipv4Header;
    UdpHeader udpHeader;

    // Parse IPv4
    writablePacket->RemoveHeader(ipv4Header);

    // Parse UDP
    writablePacket->RemoveHeader(udpHeader);

    // Obtener puerto destino
    uint16_t udpDestPort = udpHeader.GetDestinationPort();

    bool portFound = false;

    //  Búsqueda directa en el mapa (port → DSCP)
    auto it = markingMap.find(udpDestPort);

    if (it != markingMap.end())
    {
        // std::cout << "Marking packet with destination port " << udpDestPort << " to DSCP " << it->second << std::endl;
        ipHeader.SetDscp(ns3::Ipv4Header::DscpType(it->second));
        portFound = true;
    }

    // Si no se encuentra el puerto → DSCP por defecto
    if (!portFound)
    {
        ipHeader.SetDscp(Ipv4Header::DSCP_EF);
    }

    //---------------------------------------------
    // Reconstruir paquete con header modificado
    //---------------------------------------------

    Ptr<Packet> modpkt = ipItem->GetPacket()->Copy();

    // Eliminar header antiguo
    modpkt->RemoveHeader(ipHeader);

    // Crear nuevo QueueDiscItem con header actualizado
    Ptr<QueueDiscItem> modifiedQueueItem =
        Create<Ipv4QueueDiscItem>(modpkt,
                                  ipItem->GetAddress(),
                                  ipItem->GetProtocol(),
                                  ipHeader);
    return modifiedQueueItem;                              
}



bool
PrioQueueDscpDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    
    uint32_t band = 0;



    Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
    Ipv4Header ipHeader = ipItem->GetHeader();

    // Extract DSCP value from the IP header
    int dscp = ipHeader.GetDscp();

    if (dscp == 22) {
        item = MarkingPacket(item);
        Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
        Ipv4Header ipHeader = ipItem->GetHeader();
        dscp = ipHeader.GetDscp();
        // std::cout << "Packet marked with DSCP: " << dscp << std::endl;
    }

    // std::cout << dscp << std::endl;

    auto it = mapuca.find(dscp);
    if (it != mapuca.end()) {
        band = it->second;
        // std::cout << "DSCP value: " << dscp << " mapped to band " << band  << " number of bands: " << GetNQueueDiscClasses() << std::endl;
        // NS_LOG_LOGIC("DSCP value: " << dscp << " band " << band);
        if (band >= GetNQueueDiscClasses()) {
            band = GetNQueueDiscClasses() - 1; // Map to the last band if out of range
        }
        // std::cout << Simulator::Now().GetSeconds() << " PRIO Enqueue: DSCP value: " << dscp << " mapped to band " << band << " from: " << ipHeader.GetSource() << std::endl;
    } 

    
    // std::cout << Simulator::Now().GetNanoSeconds() << " " << item << " PRIO Enqueue: DSCP value: " << dscp << " mapped to band " << band << " from: " << ipHeader.GetSource() << std::endl;
    NS_ASSERT_MSG(band < GetNQueueDiscClasses(), "Selected band out of range");
    bool retval = GetQueueDiscClass(band)->GetQueueDisc()->Enqueue(item);
    NS_LOG_INFO(Simulator::Now().GetSeconds() << " PRIO Enqueue: Number packets band " << band << ": " <<  GetQueueDiscClass(band)->GetQueueDisc()->GetNPackets() << " from: " << ipHeader.GetSource());
    // If Queue::Enqueue fails, QueueDisc::Drop is called by the child queue disc
    // because QueueDisc::AddQueueDiscClass sets the drop callback
    // std::cout<< "+++ Number of packets in Band: " << band << " " << GetQueueDiscClass(band)->GetQueueDisc()->GetNPackets()<<std::endl ;
    NS_LOG_LOGIC("Number packets band " << band << ": "
                                        << GetQueueDiscClass(band)->GetQueueDisc()->GetNPackets());
    
    return retval;
}

Ptr<QueueDiscItem>
PrioQueueDscpDisc::DoDequeue()
{
    NS_LOG_FUNCTION(this);

    Ptr<QueueDiscItem> item;

    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
        if ((item = GetQueueDiscClass(i)->GetQueueDisc()->Dequeue()))
        {
            //   std::cout << Simulator::Now().GetSeconds() << " PRIO Dequeue: Number packets band " << i << ": " <<  GetQueueDiscClass(i)->GetQueueDisc()->GetNPackets() << std::endl;
            // std::cout << "Dequeued from band " << i << ": " << item->GetSize() << std::endl;
            // NS_LOG_INFO("Popped from band " << i << ": " << item);
            NS_LOG_LOGIC("Number packets band "
                         << i << ": " << GetQueueDiscClass(i)->GetQueueDisc()->GetNPackets());
            // std::cout << Simulator::Now().GetSeconds() << " PRIO Dequeue: Number packets band " << i << ": " <<  GetQueueDiscClass(i)->GetQueueDisc()->GetNPackets() << std::endl;
            NS_LOG_INFO(Simulator::Now().GetSeconds() << " PRIO Dequeue: Number packets band " << i << ": " <<  GetQueueDiscClass(i)->GetQueueDisc()->GetNPackets());
            return item;
        }
    }
   
    NS_LOG_LOGIC("Queue empty");
    return item;
}

Ptr<const QueueDiscItem>
PrioQueueDscpDisc::DoPeek()
{
    NS_LOG_FUNCTION(this);

    Ptr<const QueueDiscItem> item;

    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
        if ((item = GetQueueDiscClass(i)->GetQueueDisc()->Peek()))
        {
           
            NS_LOG_LOGIC("Peeked from band " << i << ": " << item);
            NS_LOG_LOGIC("Number packets band "
                         << i << ": " << GetQueueDiscClass(i)->GetQueueDisc()->GetNPackets());
            return item;
           
        }
    }

    NS_LOG_LOGIC("Queue empty");
    return item;
}

bool
PrioQueueDscpDisc::CheckConfig()
{
    NS_LOG_FUNCTION(this);
    if (GetNInternalQueues() > 0)
    {
        NS_LOG_ERROR("PrioQueueDscpDisc cannot have internal queues");
        return false;
    }

    if (GetNQueueDiscClasses() == 0)
    {
        // create 3 fifo queue discs
        ObjectFactory factory;
        factory.SetTypeId("ns3::FifoQueueDisc");
        
        for (uint8_t i = 0; i < nBands; i++)
        {
            Ptr<QueueDisc> qd = factory.Create<QueueDisc>();
            qd->Initialize();
            Ptr<QueueDiscClass> c = CreateObject<QueueDiscClass>();
            c->SetQueueDisc(qd);
            AddQueueDiscClass(c);
        }
    }

    if (GetNQueueDiscClasses() < 2)
    {
        NS_LOG_ERROR("PrioQueueDscpDisc needs at least 2 classes");
        return false;
    }

    return true;
}

void
PrioQueueDscpDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);
}

} // namespace ns3
