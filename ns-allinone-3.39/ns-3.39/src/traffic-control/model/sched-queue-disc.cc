/*
 * Copyright (c) 2007, 2014 University of Washington
 *               2015 Universita' degli Studi di Napoli Federico II
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
 *           Tom Henderson <tomhend@u.washington.edu>
 */
#include <stdint.h>
#include "sched-queue-disc.h"

#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/queue.h"
#include "ns3/socket.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-queue-disc-item.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SchedQueueDisc");

NS_OBJECT_ENSURE_REGISTERED(SchedQueueDisc);

TypeId
SchedFlow::GetTypeId()
{
    static TypeId tid = TypeId("ns3::FqCoDelFlow")
                            .SetParent<QueueDiscClass>()
                            .SetGroupName("TrafficControl")
                            .AddConstructor<SchedFlow>();
    return tid;
}




SchedFlow::SchedFlow()
    : m_deficit(0),
      m_status(INACTIVE),
      m_index(0)
{
    NS_LOG_FUNCTION(this);
}

SchedFlow::~SchedFlow()
{
    NS_LOG_FUNCTION(this);
}

void
SchedFlow::SetDeficit(uint32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit = deficit;
}

int32_t
SchedFlow::GetDeficit() const
{
    NS_LOG_FUNCTION(this);
    return m_deficit;
}

void
SchedFlow::IncreaseDeficit(int32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit += deficit;
}

void
SchedFlow::SetStatus(FlowStatus status)
{
    NS_LOG_FUNCTION(this);
    m_status = status;
}

SchedFlow::FlowStatus
SchedFlow::GetStatus() const
{
    NS_LOG_FUNCTION(this);
    return m_status;
}

void
SchedFlow::SetIndex(uint32_t index)
{
    NS_LOG_FUNCTION(this);
    m_index = index;
}

uint32_t
SchedFlow::GetIndex() const
{
    return m_index;
}



TypeId
SchedQueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::SchedQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<SchedQueueDisc>()
            .AddAttribute("MaxSize",
                          "The maximum number of packets accepted by this queue disc.",
                          QueueSizeValue(QueueSize("1000p")),
                          MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize),
                          MakeQueueSizeChecker())
            .AddAttribute("Flows",
                "The number of queues into which the incoming packets are classified",
                UintegerValue(1024),
                MakeUintegerAccessor(&SchedQueueDisc::m_flows),
                MakeUintegerChecker<uint32_t>())
            .AddAttribute("Perturbation",
                "The salt used as an additional input to the hash function used to "
                "classify packets",
                UintegerValue(0),
                MakeUintegerAccessor(&SchedQueueDisc::m_perturbation),
                MakeUintegerChecker<uint32_t>())
            .AddAttribute("SetWays",
                "The size of a set of queues (used by set associative hash)",
                UintegerValue(8),
                MakeUintegerAccessor(&SchedQueueDisc::m_setWays),
                MakeUintegerChecker<uint32_t>());
    return tid;
}

SchedQueueDisc::SchedQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS)
{
    NS_LOG_FUNCTION(this);
}

SchedQueueDisc::~SchedQueueDisc()
{
    NS_LOG_FUNCTION(this);
}

void
SchedQueueDisc::SetQuantum(uint32_t quantum)
{
    NS_LOG_FUNCTION(this << quantum);
    m_quantum = quantum;
}

uint32_t
SchedQueueDisc::GetQuantum() const
{
    return m_quantum;
}

uint32_t
SchedQueueDisc::SetAssociativeHash(uint32_t flowHash)
{
    NS_LOG_FUNCTION(this << flowHash);

    uint32_t h = (flowHash % m_flows);
    uint32_t innerHash = h % m_setWays;
    uint32_t outerHash = h - innerHash;

    for (uint32_t i = outerHash; i < outerHash + m_setWays; i++)
    {
        auto it = m_flowsIndices.find(i);

        if (it == m_flowsIndices.end() ||
            (m_tags.find(i) != m_tags.end() && m_tags[i] == flowHash) ||
            StaticCast<SchedFlow>(GetQueueDiscClass(it->second))->GetStatus() ==
                SchedFlow::INACTIVE)
        {
            // this queue has not been created yet or is associated with this flow
            // or is inactive, hence we can use it
            m_tags[i] = flowHash;
            return i;
        }
    }

    // all the queues of the set are used. Use the first queue of the set
    m_tags[outerHash] = flowHash;
    return outerHash;
}



















const uint32_t SchedQueueDisc::prio2band[2] = {0,1};

bool
SchedQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    
    // NS_LOG_FUNCTION(this << item);

   
    // uint32_t band;
    


    // SocketPriorityTag priorityTag;
    // if (item->GetPacket()->PeekPacketTag(priorityTag))
    // {
    //     Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
    //     const Ipv4Header ipHeader = ipItem->GetHeader();
    //     if (static_cast<uint32_t>(ipHeader.GetProtocol()) == 6){
    //         band = 0;
    //         NS_LOG_LOGIC("Protocol TCP");
    //     }else{
    //         band = 1;
    //         NS_LOG_LOGIC("Protocol UDP");
    //     }

        
    // }
    // else
    // {
    //     NS_LOG_DEBUG("Pkt is not filtered");

       
    // }

    // if (m_flowsIndices.find(band) == m_flowsIndices.end())
    // {
       
    //     AddQueueDiscClass(band);

    //     m_flowsIndices[band] = GetNQueueDiscClasses() - 1;
    // }

    // NS_ASSERT_MSG(band < GetNQueueDiscClasses(), "Selected band out of range");
    // bool retval = GetQueueDiscClass(band)->GetQueueDisc()->Enqueue(item);

    // // If Queue::Enqueue fails, QueueDisc::Drop is called by the child queue disc
    // // because QueueDisc::AddQueueDiscClass sets the drop callback

    // NS_LOG_LOGIC("Number packets band " << band << ": "
    //                                     << GetQueueDiscClass(band)->GetQueueDisc()->GetNPackets());

    // return retval;





    if (GetCurrentSize() > GetMaxSize())
    {
        NS_LOG_DEBUG("Overload; enter SchedDrop ()");
        SchedDrop();
    }

    NS_LOG_LOGIC("-- Proto item: " << item->GetProtocol());
    uint8_t priority = 0;
    SocketPriorityTag priorityTag;
    
    Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
    const Ipv4Header ipHeader = ipItem->GetHeader();
    
    NS_LOG_LOGIC("--Header: " << ipHeader);
    // NS_LOG_LOGIC("Protocol: " << static_cast<uint32_t>(ipHeader.GetProtocol()));
    // Ipv4Header ipv4Header;
    // item->GetPacket()->PeekHeader(ipv4Header);
    // NS_LOG_LOGIC("--Header: " << ipv4Header);
   
    
   
    // NS_LOG_LOGsIC("Byte It: " << item->GetPacket()->GetByteTagIterator());
    // uint8_t proto = ipv4Header.GetProtocol();
    // NS_LOG_LOGIC("-- ++ IPV4 Proto Header: " << static_cast<uint32_t>(proto));
    // SocketAddresTag tag;
    // item->GetPacket()->PeekPacketTag(tag);
     // Create a packet tag that can hold the transport protocol information
   
   
    if (item->GetPacket()->PeekPacketTag(priorityTag)){
        priority = priorityTag.GetPriority();
    }
    NS_LOG_LOGIC("--Type: " << priorityTag.GetTypeId());
    uint32_t band;
    
    if (static_cast<uint32_t>(ipHeader.GetProtocol()) == 6){
        band = 0;
        NS_LOG_LOGIC("Protocol TCP");
    }else{
        band = 1;
        NS_LOG_LOGIC("Protocol UDP");
    }
    
    bool retval = GetInternalQueue(band)->Enqueue(item);
    // If Queue::Enqueue fails, QueueDisc::DropBeforeEnqueue is called by the
    // internal queue because QueueDisc::AddInternalQueue sets the trace callback

    if (!retval)
    {
        NS_LOG_WARN("Packet enqueue failed. Check the size of the internal queues");
    }

    NS_LOG_LOGIC("Number packets band " << band << ": " << GetInternalQueue(band)->GetNPackets());

    return retval;
}

Ptr<QueueDiscItem>
SchedQueueDisc::DoDequeue()
{
    // NS_LOG_FUNCTION(this);

    // Ptr<QueueDiscItem> item;

    // uint32_t maxQueueSize = 0; // Track the maximum queue size
    // uint32_t maxQueueIndex = 0; // Track the index of the queue with the maximum size

    // // Iterate through internal queues to find the one with the maximum size
    // for (uint32_t i = 0; i < GetNInternalQueues(); i++) {
    //     uint32_t queueSize = GetInternalQueue(i)->GetNPackets(); // You might use GetSize() or GetNBytes() depending on your requirements

    //     // If the current queue has a larger size, update the maximum size and index
    //     if (queueSize > maxQueueSize) {
    //         maxQueueSize = queueSize;
    //         maxQueueIndex = i;
    //     }
    // }

    // // Dequeue from the queue with the largest size
    // if (maxQueueSize > 0) {
    //     item = GetInternalQueue(maxQueueIndex)->Dequeue();
    //     NS_LOG_LOGIC("Popped from band " << maxQueueIndex << ": " << item);
    //     NS_LOG_LOGIC("Number packets band " << maxQueueIndex << ": " << GetInternalQueue(maxQueueIndex)->GetNPackets());
    // }

    // NS_LOG_LOGIC("Queue empty");
    // return item;


     NS_LOG_FUNCTION(this);

    Ptr<QueueDiscItem> item;

    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
        if ((item = GetQueueDiscClass(i)->GetQueueDisc()->Dequeue()))
        {
            NS_LOG_LOGIC("Popped from band " << i << ": " << item);
            NS_LOG_LOGIC("Number packets band "
                         << i << ": " << GetQueueDiscClass(i)->GetQueueDisc()->GetNPackets());
            return item;
        }
    }

    NS_LOG_LOGIC("Queue empty");
    return item;
}

Ptr<const QueueDiscItem>
SchedQueueDisc::DoPeek()
{
    NS_LOG_FUNCTION(this);

    Ptr<const QueueDiscItem> item;

    for (uint32_t i = 0; i < GetNInternalQueues(); i++)
    {
        if ((item = GetInternalQueue(i)->Peek()))
        {
            NS_LOG_LOGIC("Peeked from band " << i << ": " << item);
            NS_LOG_LOGIC("Number packets band " << i << ": " << GetInternalQueue(i)->GetNPackets());
            return item;
        }
    }

    NS_LOG_LOGIC("Queue empty");
    return item;
}

bool
SchedQueueDisc::CheckConfig()
{
    NS_LOG_FUNCTION(this);
    if (GetNQueueDiscClasses() > 0)
    {
        NS_LOG_ERROR("SchedQueueDisc cannot have classes");
        return false;
    }

    if (GetNPacketFilters() != 0)
    {
        NS_LOG_ERROR("SchedQueueDisc needs no packet filter");
        return false;
    }

    if (GetNInternalQueues() == 0)
    {
        // create 3 DropTail queues with GetLimit() packets each
        ObjectFactory factory;
        factory.SetTypeId("ns3::DropTailQueue<QueueDiscItem>");
        factory.Set("MaxSize", QueueSizeValue(GetMaxSize()));
        AddInternalQueue(factory.Create<InternalQueue>());
        AddInternalQueue(factory.Create<InternalQueue>());
        AddInternalQueue(factory.Create<InternalQueue>());
    }

    if (GetNInternalQueues() != 3)
    {
        NS_LOG_ERROR("SchedQueueDisc needs 3 internal queues");
        return false;
    }

    if (GetInternalQueue(0)->GetMaxSize().GetUnit() != QueueSizeUnit::PACKETS ||
        GetInternalQueue(1)->GetMaxSize().GetUnit() != QueueSizeUnit::PACKETS ||
        GetInternalQueue(2)->GetMaxSize().GetUnit() != QueueSizeUnit::PACKETS)
    {
        NS_LOG_ERROR("SchedQueueDisc needs 3 internal queues operating in packet mode");
        return false;
    }

    for (uint8_t i = 0; i < 2; i++)
    {
        if (GetInternalQueue(i)->GetMaxSize() < GetMaxSize())
        {
            NS_LOG_ERROR(
                "The capacity of some internal queue(s) is less than the queue disc capacity");
            return false;
        }
    }

    return true;
}

void
SchedQueueDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);
}




uint32_t
SchedQueueDisc::SchedDrop()
{
    NS_LOG_FUNCTION(this);

    uint32_t maxBacklog = 0;
    uint32_t index = 0;
    Ptr<QueueDisc> qd;

    /* Queue is full! Find the fat flow and drop packet(s) from it */
    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
        qd = GetQueueDiscClass(i)->GetQueueDisc();
        uint32_t bytes = qd->GetNBytes();
        if (bytes > maxBacklog)
        {
            maxBacklog = bytes;
            index = i;
        }
    }

    /* Our goal is to drop half of this fat flow backlog */
    uint32_t len = 0;
    uint32_t count = 0;
    uint32_t threshold = maxBacklog >> 1;
    qd = GetQueueDiscClass(index)->GetQueueDisc();
    Ptr<QueueDiscItem> item;

    do
    {
        NS_LOG_DEBUG("Drop packet (overflow); count: " << count << " len: " << len
                                                       << " threshold: " << threshold);
        item = qd->GetInternalQueue(0)->Dequeue();
        DropAfterDequeue(item, OVERLIMIT_DROP);
        len += item->GetSize();
    } while (++count < m_dropBatchSize && len < threshold);

    return index;
}

} // namespace ns3