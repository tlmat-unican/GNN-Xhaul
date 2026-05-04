/*
 * Copyright (c) 2016 Universita' degli Studi di Napoli Federico II
 *
 *
 * Default Queueing, Dropping and Scheduling Configuration - Configure Catalyst 3750 QoS
 */

#include "wdrr-queue-disc.h"


#include "ns3/log.h"
#include "ns3/object-factory.h"
#include "ns3/pointer.h"
#include "ns3/socket.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/queue.h"
#include "ns3/object-map.h"
// #include "ns3/prio-queue-disc.h"

#include <algorithm>
#include <iterator>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("WdrrQueueDisc");
NS_OBJECT_ENSURE_REGISTERED(WdrrFlow);

ATTRIBUTE_HELPER_CPP(Quantum);

std::ostream&
operator<<(std::ostream& os, const Quantum& Quantum)
{
    if (!Quantum.empty())
    {
        for (auto it = Quantum.begin(); it != Quantum.end() - 1; ++it)
        {
            os << *it << " ";
        }
        os << Quantum.back();
    }
    return os;
}

std::istream&
operator>>(std::istream& is, Quantum& Quantum)
{
    Quantum.clear();

    while (true)
    {
        int temp;
        if (is >> temp)
        {
            Quantum.push_back(temp);
            continue;
        }

        if (is.eof())
        {
            is.clear(is.rdstate() & ~std::ios::failbit);
        }
        return is;
    }
}

ATTRIBUTE_HELPER_CPP(MapQueue);
std::ostream&
operator<<(std::ostream& os, const MapQueue& map2)
{
    // Copy all key-value pairs to the out stream except the last pair
    for (auto it = map2.begin(); it != std::prev(map2.end()); ++it) {
        std::cout << it->first << std::endl;
        os << it->first << " " << it->second << " ";
    }
    
    // Copy the last key-value pair
    if (!map2.empty()) {
        os << map2.rbegin()->first << " " << map2.rbegin()->second;
    }

    return os;
}

std::istream&
operator>>(std::istream& is, MapQueue& map2)
{
    map2.clear();

    while (true)
    {
        int key;
        int value;
        if (is >> key >> value)
        {
            map2.insert(std::pair<int, int>(key, value));
            continue;
        }

        if (is.eof())
        {
            is.clear(is.rdstate() & ~std::ios::failbit);
        }
        return is;
    }
}




TypeId
WdrrFlow::GetTypeId()
{
    static TypeId tid = TypeId("ns3::WdrrFlow")
                            .SetParent<QueueDiscClass>()
                            .SetGroupName("TrafficControl")
                            .AddConstructor<WdrrFlow>();
    return tid;
}

WdrrFlow::WdrrFlow()
    : m_deficit(0),
      m_status(INACTIVE),
      m_index(0)
{
    NS_LOG_FUNCTION(this);
}

WdrrFlow::~WdrrFlow()
{
    NS_LOG_FUNCTION(this);
}

void
WdrrFlow::SetDeficit(uint32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit = deficit;
}

int32_t
WdrrFlow::GetDeficit() const
{
    NS_LOG_FUNCTION(this);
    return m_deficit;
}

void
WdrrFlow::IncreaseDeficit(int32_t deficit)
{
    NS_LOG_FUNCTION(this << deficit);
    m_deficit += deficit;
}

void
WdrrFlow::SetStatus(FlowStatus status)
{
    NS_LOG_FUNCTION(this);
    m_status = status;
}

WdrrFlow::FlowStatus
WdrrFlow::GetStatus() const
{
    NS_LOG_FUNCTION(this);
    return m_status;
}

void
WdrrFlow::SetIndex(uint32_t index)
{
    NS_LOG_FUNCTION(this);
    m_index = index;
}

uint32_t
WdrrFlow::GetIndex() const
{
    return m_index;
}

NS_OBJECT_ENSURE_REGISTERED(WdrrQueueDisc);

TypeId
WdrrQueueDisc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::WdrrQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<WdrrQueueDisc>()
            .AddAttribute("MaxSize",
                          "The maximum number of packets accepted by this queue disc",
                          QueueSizeValue(QueueSize("10240p")),
                          MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize),
                          MakeQueueSizeChecker())
            .AddAttribute("Flows",
                          "The number of queues into which the incoming packets are classified",
                          UintegerValue(1024),
                          MakeUintegerAccessor(&WdrrQueueDisc::m_flows),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Quantum",
                          "The quantum to band mapping.",
                          QuantumValue(Quantum{{1486,20000,1486,1486,1486}}),
                          MakeQuantumAccessor(&WdrrQueueDisc::m_quantum),
                          MakeQuantumChecker())
             .AddAttribute("DropBatchSize",
                          "The maximum number of packets dropped from the fat flow",
                          UintegerValue(64),
                          MakeUintegerAccessor(&WdrrQueueDisc::m_dropBatchSize),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("MapQueue",
                          "It can be used in order to map dscp marking with the queues",
                          MapQueueValue(MapQueue{{1, 2},{2, 4}}),
                          MakeMapQueueAccessor(&WdrrQueueDisc::mapuca),
                          MakeMapQueueChecker());
    return tid;
}

WdrrQueueDisc::WdrrQueueDisc()
    : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS),
      m_quantum(0)
{
    NS_LOG_FUNCTION(this);
}

WdrrQueueDisc::~WdrrQueueDisc()
{
    NS_LOG_FUNCTION(this);
}

void
WdrrQueueDisc::SetQuantum(uint32_t id, uint32_t quantum)
{
    NS_LOG_FUNCTION(this << quantum);
    m_quantum[id] = quantum;
}

uint32_t
WdrrQueueDisc::GetQuantum(uint32_t id) const
{
    return m_quantum[id];
}



bool
WdrrQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
     NS_LOG_FUNCTION(this << item);

    uint32_t band = 0;

    Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
    Ipv4Header ipHeader = ipItem->GetHeader();

    // Extract DSCP value from the IP header
    int dscp = ipHeader.GetDscp();


    auto it = mapuca.find(dscp);
    
    // If the port is found in the map, assign the corresponding DSCP value
    if (it != mapuca.end()) {
        // std::cout << dscp << std::endl;
        band = it->second;
        NS_LOG_INFO(band);
    }



    Ptr<WdrrFlow> flow;
    if (m_flowsIndices.find(band) == m_flowsIndices.end())
    {       
        Ptr<QueueDisc> qd = m_queueDiscFactory.Create<QueueDisc>();
        qd->Initialize();
        flow = m_flowFactory.Create<WdrrFlow>();
        NS_LOG_DEBUG("Creating a new flow queue with index");
        flow->SetIndex(band);
        flow->SetQueueDisc(qd);
        AddQueueDiscClass(flow);
        flow->SetDeficit(m_quantum[flow->GetIndex()]);
        m_flowsIndices[band] =  GetNQueueDiscClasses() - 1;
    }
    else
    {
        flow = StaticCast<WdrrFlow>(GetQueueDiscClass(m_flowsIndices[band]));
    }

    if (flow->GetStatus() == WdrrFlow::INACTIVE)
    {
        flow->SetStatus(WdrrFlow::NEW_FLOW);
        flow->SetDeficit(m_quantum[flow->GetIndex()]);
        m_newFlows.push_back(flow);
    }


    bool retval = flow->GetQueueDisc()->Enqueue(item);

    if (ipHeader.GetSource()  == "10.1.0.1"){
        std::cout << "Error" << std::endl;
    }
    // Way to obtain the current pkts in the queue
    // NS_LOG_INFO("--- Number packets band " << band << ": " << flow->GetQueueDisc()->GetNPackets() << " from: " << ipHeader.GetSource()); 
    
    
    if (GetCurrentSize() > GetMaxSize())
    {
        NS_LOG_DEBUG("Overload; enter FqCobaltDrop ()");
        WdrrDrop();
    }
    return retval;
}

Ptr<QueueDiscItem>
WdrrQueueDisc::DoDequeue()
{
    NS_LOG_FUNCTION(this);

    Ptr<WdrrFlow> flow;
    Ptr<QueueDiscItem> item;
    do
    {
        bool found = false;

        while (!found && !m_newFlows.empty())
        {
            flow = m_newFlows.front();
            if (flow->GetDeficit() <= 0)
            {
                NS_LOG_DEBUG("Increase deficit for new flow index " << flow->GetIndex());
                flow->IncreaseDeficit(m_quantum[flow->GetIndex()]);
                flow->SetStatus(WdrrFlow::OLD_FLOW);
                m_oldFlows.push_back(flow);
                m_newFlows.pop_front();
            }
            else
            {
                NS_LOG_DEBUG("Found a new flow " << flow->GetIndex() << " with positive deficit");
                found = true;
            }
            Bufferlog << "New - Queue status: " <<flow->GetIndex() << " " << flow->GetQueueDisc()->GetNPackets()<<std::endl;
        }

        while (!found && !m_oldFlows.empty())
        {
            flow = m_oldFlows.front();
            if (flow->GetDeficit() <= 0)
            {
                NS_LOG_DEBUG("Increase deficit for old flow index " << flow->GetIndex());
                flow->IncreaseDeficit(m_quantum[flow->GetIndex()]);
                m_oldFlows.push_back(flow);
                m_oldFlows.pop_front();
            }
            else
            {  
                NS_LOG_DEBUG("Found an old flow " << flow->GetIndex() << " with positive deficit");
                found = true;
            }
            Bufferlog << "Old - Queue status: "<<flow->GetIndex() << " " << flow->GetQueueDisc()->GetNPackets()<<std::endl;
        }

        if (!found)
        {
            NS_LOG_DEBUG("No flow found to dequeue a packet");
            return nullptr;
        }

        item = flow->GetQueueDisc()->Dequeue();

        if (!item)
        {
            NS_LOG_DEBUG("Could not get a packet from the selected flow queue");
            if (!m_newFlows.empty())
            {
                flow->SetStatus(WdrrFlow::OLD_FLOW);
                m_oldFlows.push_back(flow);
                m_newFlows.pop_front();
            }
            else
            {
                flow->SetStatus(WdrrFlow::INACTIVE);
                m_oldFlows.pop_front();
            }
        }
        else
        {   NS_LOG_INFO("Flow " << flow->GetIndex() << " has been select with deficit " << flow->GetDeficit());
            NS_LOG_DEBUG("Dequeued packet " << item->GetPacket()->GetSize());
        }
    } while (!item);

    Bufferlog << flow->GetIndex() << " " << item->GetSize() <<std::endl;
    flow->IncreaseDeficit(item->GetSize() * -1);

    return item;

}

bool
WdrrQueueDisc::CheckConfig()
{   

    NS_LOG_FUNCTION(this);
    if (GetNQueueDiscClasses() > 0)
    {
        NS_LOG_ERROR("FqCobaltQueueDisc cannot have classes");
        return false;
    }


    NS_LOG_FUNCTION(this);
    if (GetNInternalQueues() > 0)
    {
        NS_LOG_ERROR("WdrrQueueDisc cannot have internal queues");
        return false;
    }

     // we are at initialization time. If the user has not set a quantum value,
    // set the quantum to the MTU of the device (if any)
    for (int i = 0; i < (int)m_quantum.size(); i++){
        if (!m_quantum[i])
        {
            Ptr<NetDeviceQueueInterface> ndqi = GetNetDeviceQueueInterface();
            Ptr<NetDevice> dev;
            // if the NetDeviceQueueInterface object is aggregated to a
            // NetDevice, get the MTU of such NetDevice
            if (ndqi && (dev = ndqi->GetObject<NetDevice>()))
            {
                m_quantum[i] = dev->GetMtu();
                NS_LOG_DEBUG("Setting the quantum to the MTU of the device: " << m_quantum[i]);
            }

            if (!m_quantum[i])
            {
                NS_LOG_ERROR("The quantum parameter cannot be null");
                return false;
            }
        }

    }

    


    return true;
}


uint32_t
WdrrQueueDisc::WdrrDrop()
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





void
WdrrQueueDisc::InitializeParams()
{
    NS_LOG_FUNCTION(this);

    m_flowFactory.SetTypeId("ns3::WdrrFlow");
    m_queueDiscFactory.SetTypeId("ns3::FifoQueueDisc");
    m_queueDiscFactory.Set("MaxSize", QueueSizeValue(GetMaxSize()));
    Bufferlog.open("./sim_results/sched-wdrr-decision.log", std::fstream::out);
}



} // namespace ns3
