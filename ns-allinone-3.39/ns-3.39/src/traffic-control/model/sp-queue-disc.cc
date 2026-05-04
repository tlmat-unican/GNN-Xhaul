/*
 * Copyright (c) 2016 Universita' degli Studi di Napoli Federico II
 *
 *
 * Default Queueing, Dropping and Scheduling Configuration - Configure Catalyst 3750 QoS
 */

#include "sp-queue-disc.h"


#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/pointer.h"
#include "ns3/socket.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/net-device-queue-interface.h"


#include <algorithm>
#include <iterator>
namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SpQueueDisc");

NS_OBJECT_ENSURE_REGISTERED(SpFlow);

TypeId
SpFlow::GetTypeId()
{
    static TypeId tid = TypeId("ns3::SpFlow")
                            .SetParent<QueueDiscClass>()
                            .SetGroupName("TrafficControl")
                            .AddConstructor<SpFlow>();
    return tid;
}

SpFlow::SpFlow()
    : m_deficit(0),
      m_status(INACTIVE),
      m_index(0)
{
    NS_LOG_FUNCTION(this);
}

SpFlow::~SpFlow()
{
    NS_LOG_FUNCTION(this);
}

void
SpFlow::SetDeficit(uint32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit = deficit;
}

int32_t
SpFlow::GetDeficit() const
{
    NS_LOG_FUNCTION(this);
    return m_deficit;
}

void
SpFlow::IncreaseDeficit(int32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit += deficit;
}

void
SpFlow::SetStatus(FlowStatus status)
{
    NS_LOG_FUNCTION(this);
    m_status = status;
}

SpFlow::FlowStatus
SpFlow::GetStatus() const
{
    NS_LOG_FUNCTION(this);
    return m_status;
}

void
SpFlow::SetIndex(uint32_t index)
{
    NS_LOG_FUNCTION(this);
    m_index = index;
}

uint32_t
SpFlow::GetIndex() const
{
    return m_index;
}

NS_OBJECT_ENSURE_REGISTERED(SpQueueDisc);

TypeId
SpQueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::SpQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<SpQueueDisc>()
            .AddAttribute("UseEcn",
                          "True to use ECN (packets are marked instead of being dropped)",
                          BooleanValue(true),
                          MakeBooleanAccessor(&SpQueueDisc::m_useEcn),
                          MakeBooleanChecker())
    
            // .AddAttribute("Target",
            //               "The CoDel algorithm target queue delay for each FQCoDel queue",
            //               StringValue("5ms"),
            //               MakeStringAccessor(&SpQueueDisc::m_target),
            //               MakeStringChecker())
            .AddAttribute("MaxSize",
                          "The maximum number of packets accepted by this queue disc",
                          QueueSizeValue(QueueSize("10240p")),
                          MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize),
                          MakeQueueSizeChecker())
            .AddAttribute("Flows",
                          "The number of queues into which the incoming packets are classified",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&SpQueueDisc::m_flows),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("DropBatchSize",
                          "The maximum number of packets dropped from the fat flow",
                          UintegerValue(64),
                          MakeUintegerAccessor(&SpQueueDisc::m_dropBatchSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Perturbation",
                          "The salt used as an additional input to the hash function used to "
                          "classify packets",
                          UintegerValue(0),
                          MakeUintegerAccessor(&SpQueueDisc::m_perturbation),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("CeThreshold",
                          "The FqCoDel CE threshold for marking packets",
                          TimeValue(Time::Max()),
                          MakeTimeAccessor(&SpQueueDisc::m_ceThreshold),
                          MakeTimeChecker())
            .AddAttribute("EnableSetAssociativeHash",
                          "Enable/Disable Set Associative Hash",
                          BooleanValue(false),
                          MakeBooleanAccessor(&SpQueueDisc::m_enableSetAssociativeHash),
                          MakeBooleanChecker())
            .AddAttribute("SetWays",
                          "The size of a set of queues (used by set associative hash)",
                          UintegerValue(8),
                          MakeUintegerAccessor(&SpQueueDisc::m_setWays),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("UseL4s",
                          "True to use L4S (only ECT1 packets are marked at CE threshold)",
                          BooleanValue(false),
                          MakeBooleanAccessor(&SpQueueDisc::m_useL4s),
                          MakeBooleanChecker());
    return tid;
}

SpQueueDisc::SpQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS),
      m_quantum(0)
{
    NS_LOG_FUNCTION(this);
}

SpQueueDisc::~SpQueueDisc()
{
    NS_LOG_FUNCTION(this);
}

void
SpQueueDisc::SetQuantum(uint32_t quantum)
{
    NS_LOG_FUNCTION(this << quantum);
    m_quantum = quantum;
}

uint32_t
SpQueueDisc::GetQuantum() const
{
    return m_quantum;
}

bool
SpQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
     NS_LOG_FUNCTION(this << item);

    uint32_t band = 0;

    Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
    Ipv4Header ipHeader = ipItem->GetHeader();

    // Extract DSCP value from the IP header
    int dscp = ipHeader.GetDscp();

    // Map DSCP to band
    // Implement logic for mapping DSCP to bands here according to some RFC
    if (dscp >= 40 && dscp <= 47)
    {
        band = 0;
       NS_LOG_INFO("DSCP value: " << dscp << " band 1");
    }else{
        band = 1;
        NS_LOG_LOGIC("DSCP value: " << dscp << " band " << band << "- EF");
    }
    


    // Ensure that the band is within the range
    NS_ASSERT_MSG(band < GetNQueueDiscClasses(), "Selected band out of range");

    // Enqueue the packet to the corresponding queue
    bool retval = GetQueueDiscClass(band)->GetQueueDisc()->Enqueue(item);

    // If Queue::Enqueue fails, QueueDisc::Drop is called by the child queue disc
    // because QueueDisc::AddQueueDiscClass sets the drop callback

    NS_LOG_LOGIC("Number packets band " << band << ": "
                                        << GetQueueDiscClass(band)->GetQueueDisc()->GetNPackets());

    return retval;
}

Ptr<QueueDiscItem>
SpQueueDisc::DoDequeue()
{
    NS_LOG_FUNCTION(this);

    Ptr<QueueDiscItem> item;

    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
    {
    
        if ((item = GetQueueDiscClass(i)->GetQueueDisc()->Dequeue()))
        {
            NS_LOG_INFO("Send from band: "<< i );
            NS_LOG_LOGIC("Popped from band " << i << ": " << item);
            NS_LOG_LOGIC("Number packets band "
                         << i << ": " << GetQueueDiscClass(i)->GetQueueDisc()->GetNPackets());
            return item;
        }
    }

    NS_LOG_LOGIC("Queue empty");
   


    return item;

}

bool
SpQueueDisc::CheckConfig()
{   


    NS_LOG_FUNCTION(this);
    if (GetNInternalQueues() > 0)
    {
        NS_LOG_ERROR("PrioQueueDisc cannot have internal queues");
        return false;
    }

    if (GetNQueueDiscClasses() == 0)
    {
        // create 3 fifo queue discs
        ObjectFactory factory;
        factory.SetTypeId("ns3::FifoQueueDisc");
        for (uint8_t i = 0; i < 2; i++)
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
        NS_LOG_ERROR("PrioQueueDisc needs at least 2 classes");
        return false;
    }

    
    return true;
}

void
SpQueueDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);

    m_flowFactory.SetTypeId("ns3::SpFlow");
}

// uint32_t
// SpQueueDisc::FqCoDelDrop()
// {
//     NS_LOG_FUNCTION(this);

//     uint32_t maxBacklog = 0;
//     uint32_t index = 0;
//     Ptr<QueueDisc> qd;

//     /* Queue is full! Find the fat flow and drop packet(s) from it */
//     for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++)
//     {
//         qd = GetQueueDiscClass(i)->GetQueueDisc();
//         uint32_t bytes = qd->GetNBytes();
//         if (bytes > maxBacklog)
//         {
//             maxBacklog = bytes;
//             index = i;
//         }
//     }

//     /* Our goal is to drop half of this fat flow backlog */
//     uint32_t len = 0;
//     uint32_t count = 0;
//     uint32_t threshold = maxBacklog >> 1;
//     qd = GetQueueDiscClass(index)->GetQueueDisc();
//     Ptr<QueueDiscItem> item;

//     do
//     {
//         NS_LOG_DEBUG("Drop packet (overflow); count: " << count << " len: " << len
//                                                        << " threshold: " << threshold);
//         item = qd->GetInternalQueue(0)->Dequeue();
//         DropAfterDequeue(item, OVERLIMIT_DROP);
//         len += item->GetSize();
//     } while (++count < m_dropBatchSize && len < threshold);

//     return index;
// }

} // namespace ns3
