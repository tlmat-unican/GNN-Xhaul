/*
 * WFQ Queue Disc Implementation for ns-3
 */

#ifndef WFQ_QUEUE_DISC
#define WFQ_QUEUE_DISC

#include "ns3/object-factory.h"
#include "ns3/queue-disc.h"
#include "ns3/vector.h"
#include "ns3/attribute.h"
#include "ns3/object-vector.h"
#include <list>
#include <map>
#include <vector>
#include <fstream>

namespace ns3
{

typedef std::vector<int> Quantum;
typedef std::map<int,int> MapQueue;

class WfqFlow : public QueueDiscClass
{
  public:
    static TypeId GetTypeId();
    WfqFlow();
    ~WfqFlow() override;

    enum FlowStatus { INACTIVE, NEW_FLOW, OLD_FLOW };

    void SetDeficit(uint32_t deficit);
    int32_t GetDeficit() const;
    void IncreaseDeficit(int32_t deficit);
    void SetStatus(FlowStatus status);
    FlowStatus GetStatus() const;
    void SetIndex(uint32_t index);
    uint32_t GetIndex() const;

  private:
    int32_t m_deficit;   
    FlowStatus m_status; 
    uint32_t m_index;    
};

class WfqQueueDisc : public QueueDisc
{
  public:
    static TypeId GetTypeId();
    WfqQueueDisc();
    ~WfqQueueDisc() override;

    void SetQuantum(uint32_t id, uint32_t quantum);
    uint32_t GetQuantum(uint32_t id) const;

    static constexpr const char* OVERLIMIT_DROP = "Overlimit drop";

  private:
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    bool CheckConfig() override;
    void InitializeParams() override;
    uint32_t WfqDrop();

    std::ofstream Bufferlog;
    uint32_t m_flows;                
    std::list<Ptr<WfqFlow>> m_Flows; 
    std::map<uint32_t, uint32_t> m_flowsIndices; 
    std::map<uint32_t, Time> m_queueTailTimes; // Mapa robusto para tiempos de finalización
    
    ObjectFactory m_flowFactory;      
    ObjectFactory m_queueDiscFactory; 

    Quantum m_quantum; 
    double m_dataRate;
    MapQueue mapuca;
};

} // namespace ns3

#endif