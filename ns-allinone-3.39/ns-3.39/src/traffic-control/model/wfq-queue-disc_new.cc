#include "wfq-queue-disc.h"
#include "ns3/log.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/simulator.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/net-device.h"
#include "ns3/data-rate.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/pointer.h"
#include "ns3/wdrr-queue-disc.h"
                 

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("WfqQueueDisc");
NS_OBJECT_ENSURE_REGISTERED(WfqFlow);

TypeId WfqFlow::GetTypeId()
{
    static TypeId tid = TypeId("ns3::WfqFlow")
                            .SetParent<QueueDiscClass>()
                            .SetGroupName("TrafficControl")
                            .AddConstructor<WfqFlow>();
    return tid;
}

WfqFlow::WfqFlow() : m_deficit(0), m_status(INACTIVE), m_index(0) {}
WfqFlow::~WfqFlow() {}

void WfqFlow::SetDeficit(uint32_t deficit) { m_deficit = deficit; }
int32_t WfqFlow::GetDeficit() const { return m_deficit; }
void WfqFlow::IncreaseDeficit(int32_t deficit) { m_deficit += deficit; }
void WfqFlow::SetStatus(FlowStatus status) { m_status = status; }
WfqFlow::FlowStatus WfqFlow::GetStatus() const { return m_status; }
void WfqFlow::SetIndex(uint32_t index) { m_index = index; }
uint32_t WfqFlow::GetIndex() const { return m_index; }

NS_OBJECT_ENSURE_REGISTERED(WfqQueueDisc);

TypeId WfqQueueDisc::GetTypeId()
{
    static TypeId tid = TypeId("ns3::WfqQueueDisc")
            .SetParent<QueueDisc>()
            .SetGroupName("TrafficControl")
            .AddConstructor<WfqQueueDisc>()
            .AddAttribute("MaxSize", "Max packets", QueueSizeValue(QueueSize("1024p")),
                          MakeQueueSizeAccessor(&QueueDisc::SetMaxSize, &QueueDisc::GetMaxSize), MakeQueueSizeChecker())
            .AddAttribute("Flows", "Number of queues", UintegerValue(1024),
                          MakeUintegerAccessor(&WfqQueueDisc::m_flows), MakeUintegerChecker<uint32_t>())
            .AddAttribute("Quantum", "Weights per band", QuantumValue(Quantum{{10,90,5,5}}),
                          MakeQuantumAccessor(&WfqQueueDisc::m_quantum), MakeQuantumChecker())
            .AddAttribute("ChannelDataRate", "Data rate in Gbps", DoubleValue(45.0),
                          MakeDoubleAccessor(&WfqQueueDisc::m_dataRate), MakeDoubleChecker<double>())
            .AddAttribute("MapQueue", "DSCP to Band map", MapQueueValue(MapQueue{{1, 2},{2, 4}}),
                          MakeMapQueueAccessor(&WfqQueueDisc::mapuca), MakeMapQueueChecker());
    return tid;
}

WfqQueueDisc::WfqQueueDisc() : QueueDisc(QueueDiscSizePolicy::MULTIPLE_QUEUES, QueueSizeUnit::PACKETS) {}
WfqQueueDisc::~WfqQueueDisc() { if (Bufferlog.is_open()) Bufferlog.close(); }

bool WfqQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    uint32_t band = 0;
    item->SetTimeStamp(Simulator::Now());

    Ptr<const Ipv4QueueDiscItem> ipItem = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipItem) {
        int dscp = ipItem->GetHeader().GetDscp();
        auto it = mapuca.find(dscp);
        if (it != mapuca.end()) band = it->second;
    }
    
    Ptr<WfqFlow> flow;
    if (m_flowsIndices.find(band) == m_flowsIndices.end()) {       
        Ptr<QueueDisc> qd = m_queueDiscFactory.Create<QueueDisc>();
        qd->Initialize();
        flow = m_flowFactory.Create<WfqFlow>();
        flow->SetIndex(band);
        flow->SetQueueDisc(qd);
        AddQueueDiscClass(flow);
        m_flowsIndices[band] = GetNQueueDiscClasses() - 1;
        m_queueTailTimes[band] = Seconds(0); // Inicialización del tiempo virtual
    } else {
        flow = StaticCast<WfqFlow>(GetQueueDiscClass(m_flowsIndices[band]));
    }

    if (flow->GetStatus() == WfqFlow::INACTIVE) {
        flow->SetStatus(WfqFlow::NEW_FLOW);
        flow->SetDeficit(m_quantum[band % m_quantum.size()]);
        m_Flows.push_back(flow);
    }

    bool retval = flow->GetQueueDisc()->Enqueue(item);
    if (GetCurrentSize() > GetMaxSize()) WfqDrop();
    return retval;
}

Ptr<QueueDiscItem> WfqQueueDisc::DoDequeue()
{
    if (m_Flows.empty()) return nullptr;

    Ptr<WfqFlow> selectedFlow = nullptr;
    Time minFinishTime = Time::Max();
    uint32_t selectedBand = 0;

    // Buscamos el paquete con menor Finish Time entre todas las colas activas
    for (auto it = m_Flows.begin(); it != m_Flows.end(); ) {
        Ptr<WfqFlow> currentFlow = *it;
        if (currentFlow->GetQueueDisc()->GetNPackets() > 0) {
            Ptr<const QueueDiscItem> pkt = currentFlow->GetQueueDisc()->Peek();
            uint32_t band = currentFlow->GetIndex();
            
            // BW proporcional al peso (Deficit)
            double weight = double(currentFlow->GetDeficit()) / 100.0;
            double virtualBw = weight * (m_dataRate * 1e9 / 8.0);

            if (virtualBw > 0) {
                Time txTime = Seconds(double(pkt->GetSize()) / virtualBw);
                Time finishTime = std::max(pkt->GetTimeStamp(), m_queueTailTimes[band]) + txTime;

                if (finishTime < minFinishTime) {
                    minFinishTime = finishTime;
                    selectedFlow = currentFlow;
                    selectedBand = band;
                }
            }
            ++it;
        } else {
            // Limpieza: si la cola está vacía, la pasamos a OLD y la sacamos de la lista activa
            currentFlow->SetStatus(WfqFlow::OLD_FLOW);
            it = m_Flows.erase(it);
        }
    }

    if (!selectedFlow) return nullptr;

    Ptr<QueueDiscItem> item = selectedFlow->GetQueueDisc()->Dequeue();
    if (item) {
        // ACTUALIZACIÓN ROBUSTA: Solo avanzamos el tiempo del flujo seleccionado
        m_queueTailTimes[selectedBand] = minFinishTime;
        
        if (Bufferlog.is_open()) {
            Bufferlog << Simulator::Now().GetSeconds() << " " << selectedBand 
                      << " " << item->GetSize() << " " << minFinishTime.GetSeconds() << std::endl;
        }
    }

    return item;
}

uint32_t WfqQueueDisc::WfqDrop()
{
    uint32_t maxBacklog = 0;
    uint32_t index = 0;
    for (uint32_t i = 0; i < GetNQueueDiscClasses(); i++) {
        uint32_t bytes = GetQueueDiscClass(i)->GetQueueDisc()->GetNBytes();
        if (bytes > maxBacklog) {
            maxBacklog = bytes;
            index = i;
        }
    }
    Ptr<QueueDiscItem> item = GetQueueDiscClass(index)->GetQueueDisc()->Dequeue();
    DropAfterDequeue(item, OVERLIMIT_DROP);
    return index;
}

bool WfqQueueDisc::CheckConfig()
{
    if (GetNQueueDiscClasses() > 0 || GetNInternalQueues() > 0) return false;
    return true;
}

void WfqQueueDisc::InitializeParams()
{
    m_flowFactory.SetTypeId("ns3::WfqFlow");
    m_queueDiscFactory.SetTypeId("ns3::FifoQueueDisc");
    // m_queueDiscFactory.Set("MaxSize", QueueSizeValue(GetMaxSize()));
    Bufferlog.open("./wfq-results.log", std::fstream::out);
}

} // namespace ns3