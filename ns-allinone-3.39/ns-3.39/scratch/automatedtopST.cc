#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"


#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("AutomatedQoS");

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;



enum class TimeDistType {
    EXPONENTIAL_T = 0,
    DETERMINISTIC_T = 1,
    UNIFORM_T = 2,
    NORMAL_T = 3,
    ONOFF_T = 4,
    PPBP_T = 5
};

enum class SizeDistType {
    DETERMINISTIC_S = 0,
    UNIFORM_S = 1,
    NORMAL_S = 2
};

static const std::unordered_map<std::string, TimeDistType> TimeDistMap = {
    {"EXPONENTIAL_T", TimeDistType::EXPONENTIAL_T},
    {"DETERMINISTIC_T", TimeDistType::DETERMINISTIC_T},
    {"UNIFORM_T", TimeDistType::UNIFORM_T},
    {"NORMAL_T", TimeDistType::NORMAL_T},
    {"ONOFF_T", TimeDistType::ONOFF_T},
    {"PPBP_T", TimeDistType::PPBP_T}
};

static const std::unordered_map<std::string, SizeDistType> SizeDistMap = {
    {"DETERMINISTIC_S", SizeDistType::DETERMINISTIC_S},
    {"UNIFORM_S", SizeDistType::UNIFORM_S},
    {"NORMAL_S", SizeDistType::NORMAL_S}
};
struct FlowSpec {
    int flowId{};          // Flow ID
    int sourceNode{};      // Nodo origen
    int destinationNode{}; // Nodo destino
    int dscp{};            // DSCP del flujo
    int port{};            // Puerto del flujo
    bool StartInOnState{}; 
    // --- Packet size / data rate ---
    int avgPktSize{};       // Tamaño medio del paquete (SizeDist)
    double dataRateMbps{};  // opcional si lo calculas de la dist.

    // --- Time distribution ---
    TimeDistType timeDistType{};  // Tipo de distribución de tiempo
    // std::string timeDistName;   // Nombre de la distribución (TimeDist.TypeName)
    // Common distribution parameters 
    double eqLambda{};          // EqLambda (TimeDist)
    double avgPktsLambda{};     // AvgPktsLambda (TimeDist)
    double expMaxFactor{};      // ExpMaxFactor (TimeDist)

    // --- Specific fields for ON/OFF ---
    double pktsLambdaOn{};     // PktsLambdaOn
    double avgTOff{};          // AvgTOff
    double avgTOn{};           // AvgTOn
    // --- Size distribution ---
    SizeDistType sizeDistType{};         // Tipo de distribución de tamaño (SizeDist.Type)
    std::string sizeDistName;   // Nombre de la distribución (SizeDist.TypeName)
};

struct ApSpec {
    int nof{}; // number of flows
    int non{}; // number of nodes
    // std::vector<std::pair<int,int>> mapQueue;         // (DSCP, queueId)
    // std::vector<std::pair<int,int>> markingPortQueue; // (port, DSCP)
    std::string mapQueue;         // (DSCP, queueId)
    std::string markingPortQueue; // (port, DSCP)
    std::vector<FlowSpec> flows;

    // Derived lookups
    std::unordered_map<int,int> dscpToQueue; // DSCP → queueId
    std::unordered_map<int,int> portToDscp;  // Port → DSCP
};

static std::vector<std::pair<int,int>> ParsePairList(const std::string& s) {
  std::istringstream iss(s);
  std::vector<int> nums;
  int x;
  while (iss >> x) nums.push_back(x);

  if (nums.size() % 2 != 0) {
    throw std::runtime_error("Pair-list string has odd number of integers: " + s);
  }

  std::vector<std::pair<int,int>> out;
  for (size_t i = 0; i < nums.size(); i += 2) {
    out.emplace_back(nums[i], nums[i + 1]);
  }
  return out;
}

static ApSpec LoadApSpecFromJson(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open JSON file: " + path);
    }

    json j;
    in >> j;

    ApSpec a;
    a.nof = j.at("NoF").get<int>();
    a.non = j.at("NoN").get<int>();
    // a.simulationTime = j.at("SimulationTime").get<double>();

    // a.mapQueue = ParsePairList(j.at("MapQueue").get<std::string>());
    // a.markingPortQueue = ParsePairList(j.at("MarkingPortQueue").get<std::string>());

    a.mapQueue = j.at("MapQueue").get<std::string>();
    a.markingPortQueue = j.at("MarkingPortQueue").get<std::string>();
    for (const auto& f : j.at("FeaturesperFlow")) {
        FlowSpec fs;
        fs.flowId = f.at("FlowId").get<int>();
        fs.sourceNode = f.at("SourceNode").get<int>();
        fs.destinationNode = f.at("DestinationNode").get<int>();
        fs.dscp = f.at("DSCP").get<int>();
        // std::cout << "DSCP for flow " << fs.flowId << ": " << fs.dscp << "\n";
        fs.port = f.at("Port").get<int>();

        // --- Time Distribution ---
        const auto& tdist = f.at("TimeDist");
        fs.timeDistType = TimeDistMap.at(tdist.at("Type").get<std::string>());
        // fs.timeDistName = tdist.at("TypeName").get<std::string>();
        // --- Common parameters for all distributions ---
        fs.eqLambda = tdist.at("EqLambda").get<double>();
        fs.expMaxFactor = tdist.at("ExpMaxFactor").get<double>();

        // --- Specific parameters for ON/OFF ---
        if (fs.timeDistType == TimeDistType::ONOFF_T) { // ON/OFF
            fs.pktsLambdaOn = tdist.at("pktsLambdaOn").get<double>();
            fs.avgTOff = tdist.at("avgTOff").get<double>();
            fs.avgTOn = tdist.at("avgTOn").get<double>();
            fs.StartInOnState = tdist.at("StartInOnState").get<bool>();
        }else if (fs.timeDistType == TimeDistType::EXPONENTIAL_T) { // EXPONENTIAL
            fs.avgPktsLambda = tdist.at("AvgPktsLambda").get<double>();
        }
       

        // --- Size Distribution ---
        const auto& sdist = f.at("SizeDist");
        fs.sizeDistType = SizeDistMap.at(sdist.at("Type").get<std::string>());
        fs.avgPktSize = sdist.at("AvgPktSize").get<int>();

        a.flows.push_back(fs);
    }

    if (static_cast<int>(a.flows.size()) != a.nof) {
        throw std::runtime_error("NoF mismatch: NoF=" + std::to_string(a.nof) +
                                 ", FeaturesperFlow=" + std::to_string(a.flows.size()));
    }

    // // Map DSCP to queue
    // for (const auto& [dscp, q] : a.mapQueue) a.dscpToQueue[dscp] = q;
    // // Map Port to DSCP
    // for (const auto& [port, dscp] : a.markingPortQueue) a.portToDscp[port] = dscp;

    return a;
}




// ============================================
// Globals & Structures
// ============================================
std::string g_outputDir;

struct NodeConfig {
    uint32_t id;
    std::string label;
    std::string policy;
    std::string weights; 
    uint32_t qosLevels;
    std::vector<std::string> queuePolicies;
    std::vector<int> hqosLevels;
    std::string queueSizes;
};

struct EdgeEntry {
    uint32_t src;
    uint32_t dst;
    std::string bw;
};

struct PacketInfo { double txTime; uint32_t size; double rxTime; };
std::map<uint32_t, std::map<uint64_t, PacketInfo>> flowPackets;

static std::ofstream g_intermediateSojournFile;

void IntermediateNodeSojournTrace(uint32_t nodeId, Time sojournTime)
{
    if (!g_intermediateSojournFile.is_open())
    {
        return;
    }

    g_intermediateSojournFile << nodeId << "\t"
                              << Simulator::Now().GetNanoSeconds() << "\t"
                              << sojournTime.GetNanoSeconds() << "\n";
}


void ChildSojournTrace (uint32_t node, uint32_t band, Ptr<QueueDisc> qd, Time sojournTime)
{
    if (!g_intermediateSojournFile.is_open())
    {
        return;
    }


    int64_t now = Simulator::Now().GetNanoSeconds();
    int64_t nPackets = qd->GetNPackets();
    int64_t sumTimestamps = qd->GetTotalAccTimestamp(); 


    int64_t accQueueTimeNs = (nPackets * now) - sumTimestamps;

    double accQueueTimeUs = static_cast<double>(accQueueTimeNs) / 1000.0;
    if (nPackets == 0) {
        accQueueTimeUs = 0; // Forzado, porque si no hay paquetes no hay espera
    }
    g_intermediateSojournFile << now << "\t0\t" << node << "\t" << band  << "\t" << sojournTime.GetMicroSeconds() << "\t" << accQueueTimeUs << std::endl;
}




void WrrChildTrace (uint32_t nodeId, uint32_t bandId, Ptr<QueueDisc> childQd, Time sojournTime)
{

    if (!g_intermediateSojournFile.is_open())
    {
        return;
    }
    double sojournUs = sojournTime.GetMicroSeconds();

   
    int64_t now = Simulator::Now().GetNanoSeconds();
    int64_t nPackets = childQd->GetNPackets();
    int64_t sumTimestamps = childQd->GetTotalAccTimestamp(); 

   
    int64_t accQueueTimeNs = (nPackets * now) - sumTimestamps;
    double accQueueTimeUs = static_cast<double>(accQueueTimeNs) / 1000.0;
    if (nPackets == 0) {
        accQueueTimeUs = 0; // Forzado, porque si no hay paquetes no hay espera
    }
    g_intermediateSojournFile << now  << "\t1\t" << nodeId << "\t"
                 << bandId  << "\t" << sojournTime.GetMicroSeconds() << "\t" << accQueueTimeUs << std::endl;}
// ============================================
// CALLBACKS
// ============================================
void PacketTxCallback(uint32_t flowId, Ptr<const Packet> packet) {
    // std::cout << "Flow " << flowId << " sent packet " << packet->GetUid() << " at time " << Simulator::Now().GetSeconds() << "s\n";
    uint64_t pktId = packet->GetUid();
    flowPackets[flowId][pktId] = {Simulator::Now().GetSeconds(), packet->GetSize(), 0.0};
    std::ofstream txFile(g_outputDir + "/txfileflow" + std::to_string(flowId) + ".txt", std::ios::app);
    txFile << pktId << "\t" << Simulator::Now().GetNanoSeconds() << "\t" << packet->GetSize() << "\n";
    txFile.close();
}

void PacketRxCallback(uint32_t flowId, Ptr<const Packet> packet, const Address &from) {
    // std::cout << "Flow " << flowId << " received packet " << packet->GetUid() << " at time " << Simulator::Now().GetSeconds() << "s from " << from << "\n";
    std::ofstream rxFile(g_outputDir + "/rxfileflow" + std::to_string(flowId) + ".txt", std::ios::app);
    flowPackets[flowId][packet->GetUid()].rxTime = Simulator::Now().GetSeconds();
    rxFile << packet->GetUid() << "\t" << Simulator::Now().GetNanoSeconds() << "\t" << packet->GetSize() << "\n";
    rxFile.close(); 
}

// Helper to convert "90,5,5" to "90 5 5" for ns-3 attributes
std::string CleanWeights(std::string w) {
    std::replace(w.begin(), w.end(), ',', ' ');
    return w;
}

std::vector<std::string> SplitString(const std::string& input, char delimiter = ',') {
    std::vector<std::string> result;
    std::stringstream ss(input);
    std::string item;

    while (std::getline(ss, item, delimiter)) {
        // opcional: limpiar espacios
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        result.push_back(item);
    }

    return result;
}
std::vector<int> SplitIntList(const std::string& input, char delimiter = ',') {
    std::vector<int> result;
    std::stringstream ss(input);
    std::string item;

    while (std::getline(ss, item, delimiter)) {
        // limpiar espacios
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);

        if (!item.empty()) {
            result.push_back(std::stoi(item));
        }
    }

    return result;
}
// Helper to convert weights to integers (e.g., "33.3 33.3 33.3" to "33 33 33")
std::string WeightsToIntegers(std::string w) {
    std::istringstream iss(w);
    std::ostringstream oss;
    double val;
    bool first = true;
    while (iss >> val) {
        if (!first) oss << " ";
        oss << static_cast<int>(val);
        first = false;
    }
    return oss.str();
}
static std::string QueueCsvToRootMaxSize(const std::string& csv)
{
    std::stringstream ss(csv);
    std::string tok;
    uint32_t sum = 0;
    while (std::getline(ss, tok, ','))
    {
        if (!tok.empty())
        {
            sum += static_cast<uint32_t>(std::stoul(tok));
        }
    }
    if (sum == 0) sum = 1;
    return std::to_string(sum) + "p";
}


static std::string DecrementMapQueue(const std::string& input) {
    std::istringstream iss(input);
    std::ostringstream oss;
    int dscp, band;
    bool first = true;

    while (iss >> dscp >> band) {
        if (!first) oss << " ";
        // Restamos 1 a la banda para que el WRR hijo use índices correctos
        oss << dscp << " " << (band - 1);
        first = false;
    }
    return oss.str();
}
// ============================================
// MAIN
// ============================================
int main(int argc, char *argv[]) {
    double simulationTime = 1;
    g_outputDir = "../../ns3-automated-output";
    std::string graphFile = "./scratch/graph-triang-newnodes.gml";
    std::string routingFile = "./scratch/routing-triang.txt";
    std::string path = "./scratch/traffic.json";

    CommandLine cmd(__FILE__);
    cmd.AddValue("simulationTime", "Simulation time", simulationTime);
    cmd.AddValue("graphFile", "Input graph file", graphFile);
    cmd.AddValue("routingFile", "Input routing file", routingFile);
    cmd.AddValue("outputDir", "Output directory for results", g_outputDir);
    cmd.Parse(argc, argv);

    int res = system(("mkdir -p " + g_outputDir).c_str());
    (void)res;

    g_intermediateSojournFile.open(g_outputDir + "/intermediate_nodes_sojourn_times.txt", std::ios::out);
    g_intermediateSojournFile << "tSimTimeNs\tNodeId\tLevelHQoS\tPktSojournTimeUs\tAccSojournTimeUs\n";

    ApSpec spec = LoadApSpecFromJson(path);
    std::set<uint32_t> endpointNodes;
    for (const auto& f : spec.flows) {
        endpointNodes.insert(static_cast<uint32_t>(f.sourceNode));
        endpointNodes.insert(static_cast<uint32_t>(f.destinationNode));
    }
    // std::cout << "DSCP to Queue mapping:\n";
    // std::cout << spec.mapQueue << "\n";;
    // std::cout << "Number of Flows: " << spec.nof << " remaining features per flow \n";
    // for (const auto& f : spec.flows) {
    //     auto effDscp = f.dscp;
    //     auto p = spec.portToDscp.find(f.port);
    //     if (p != spec.portToDscp.end()) effDscp = p->second;

    //     int q = -1;
    //     auto qit = spec.dscpToQueue.find(f.dscp);
    //     if (qit != spec.dscpToQueue.end()) q = qit->second;

    //     std::cout << "Flow " << f.flowId << " port=" << f.port
    //             << " dscp=" << f.dscp << " queue=" << q << "\n";
    // }
    // 1. PARSE GRAPH
    std::map<uint32_t, NodeConfig> nodeConfigs;
    std::vector<EdgeEntry> edges;
    uint32_t maxNodeId = 0;

    std::ifstream gFile(graphFile);
    std::string line;
    while (std::getline(gFile, line)) {
        if (line.find("node [") != std::string::npos) {
            NodeConfig nc;
            while (std::getline(gFile, line) && line.find("]") == std::string::npos) {

                if (line.find("id") != std::string::npos) {
                    size_t pos = line.find_last_of(" ");
                    nc.id = std::stoi(line.substr(pos + 1));
                }

                if (line.find("schedulingPolicy") != std::string::npos) {
                    size_t f = line.find("\"") + 1;
                    nc.policy = line.substr(f, line.find_last_of("\"") - f);
                }

                if (line.find("schedulingWeights") != std::string::npos) {
                    size_t f = line.find("\"") + 1;
                    nc.weights = CleanWeights(line.substr(f, line.find_last_of("\"") - f));
                }

                if (line.find("label") != std::string::npos) {
                    size_t f = line.find("\"") + 1;
                    nc.label = line.substr(f, line.find_last_of("\"") - f);
                }

                if (line.find("queueSizes") != std::string::npos) {
                    size_t f = line.find("\"") + 1;
                    nc.queueSizes = line.substr(f, line.find_last_of("\"") - f);
                }

                if (line.find("HQoSlevels") != std::string::npos) {
                    size_t f = line.find("\"") + 1;
                    std::string raw = line.substr(f, line.find_last_of("\"") - f);
                    nc.hqosLevels = SplitIntList(raw, ',');
                }

                if (line.find("queuePolicies") != std::string::npos) {
                    size_t f = line.find("\"") + 1;
                    nc.queuePolicies = SplitString(line.substr(f, line.find_last_of("\"") - f));
                }

                if (line.find("levelsQoS") != std::string::npos) {
                    size_t f = line.find("\"") + 1;
                    std::string raw = line.substr(f, line.find_last_of("\"") - f);
                    nc.qosLevels = std::stoi(raw);
                }
        }
       
            nodeConfigs[nc.id] = nc;
            if (nc.id > maxNodeId) maxNodeId = nc.id;
        }
        if (line.find("edge [") != std::string::npos) {
            EdgeEntry e;
            while (std::getline(gFile, line) && line.find("]") == std::string::npos) {
                if (line.find("source") != std::string::npos) e.src = std::stoi(line.substr(line.find_last_of(" ")));
                if (line.find("target") != std::string::npos) e.dst = std::stoi(line.substr(line.find_last_of(" ")));
                if (line.find("bandwidth") != std::string::npos) {
                    size_t f = line.find("\"") + 1;
                    e.bw = line.substr(f, line.find_last_of("\"") - f) + "bps";
                }
            }
            edges.push_back(e);
        }
    }





    // 2. CREATE TOPOLOGY
    NodeContainer nodes;
    nodes.Create(maxNodeId + 1);
    InternetStackHelper stack;
    stack.Install(nodes);

    // Usamos un set para evitar procesar el mismo enlace físico dos veces
    std::set<std::pair<uint32_t, uint32_t>> processedEdges;
    Ipv4AddressHelper ipv4;

    for (const auto& e : edges) {
        // Normalizar el par para detectar duplicados (ej: 0-1 es lo mismo que 1-0)
        uint32_t u = std::min(e.src, e.dst);
        uint32_t v = std::max(e.src, e.dst);
        if (processedEdges.count({u, v})) continue;
        processedEdges.insert({u, v});

        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue(e.bw));
        p2p.SetChannelAttribute("Delay", StringValue("0ms"));
        p2p.SetDeviceAttribute("Mtu", UintegerValue(8000));
        p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

        std::cout << "Instalando enlace entre nodo " << e.src << " y nodo " << e.dst << " con BW=" << e.bw << "\n";
        NetDeviceContainer d = p2p.Install(nodes.Get(e.src), nodes.Get(e.dst));
        
        uint32_t nodesInLink[2] = {e.src, e.dst};
        for (uint32_t i = 0; i < 2; ++i) { 
            
            std::cout << "Configuring link between node " << e.src << " and node " << e.dst << "\n";
            TrafficControlHelper tch;
            NodeConfig cfg = nodeConfigs[nodesInLink[i]];
            std::string maxSizeStr = QueueCsvToRootMaxSize(cfg.queueSizes);
        
            std::cout << "Policy for node " << nodesInLink[i] << " (" << cfg.label << "): " << cfg.policy << "\n";
            if (spec.mapQueue != "0 0"){
                if (cfg.policy == "WRR") {
                    tch.SetRootQueueDisc("ns3::WrrQueueDisc",
                                    "Quantum", StringValue(WeightsToIntegers(cfg.weights)), 
                                    "MapQueue", StringValue(spec.mapQueue), "MaxSize", StringValue(maxSizeStr), "Bands", UintegerValue(cfg.qosLevels));
                } else if (cfg.policy == "SP") {
                    // std::cout << "Configuring PrioQueueDisc with MapQueue=" << spec.mapQueue<< " and MaxSize=" << maxSizeStr << "\n";
                    // tch.SetRootQueueDisc("ns3::PrioQueueDscpDisc", "MapQueue", StringValue(spec.mapQueue)); // TODO adjust automatically to the number of quques, not hardcoded 3
                    std::cout << "Configuring PrioQueueDisc with MapQueue=" << spec.mapQueue 
                              << ", MarkingQueue=" << spec.markingPortQueue 
                              << " and Bands=" << cfg.qosLevels << "\n";
                    uint16_t roothandle =  tch.SetRootQueueDisc("ns3::PrioQueueDscpDisc", "MapQueue", StringValue(spec.mapQueue), "MarkingQueue", StringValue(spec.markingPortQueue), "Bands", UintegerValue(cfg.qosLevels));// TODO adjust automatically to the number of quques, not hardcoded 3
                    TrafficControlHelper::ClassIdList cid = tch.AddQueueDiscClasses(roothandle, ParsePairList(spec.mapQueue).size(), "ns3::QueueDiscClass");
                    for (size_t i = 0; i < cid.size(); ++i) {
                        tch.AddChildQueueDisc(roothandle, cid[i], "ns3::FifoQueueDisc", "MaxSize", StringValue("30200059p"));
                    }
                } else if (cfg.policy == "SPWRR") {
                    // 1. Calcular cuántas bandas (prioridades) tendrá el SP
                    std::set<int> uniqueLevels(cfg.hqosLevels.begin(), cfg.hqosLevels.end());
                    uint16_t numDistinct = uniqueLevels.size();
                    uint16_t roothandle = tch.SetRootQueueDisc("ns3::PrioQueueDscpDisc", 
                                                                "MapQueue", StringValue(spec.mapQueue), 
                                                                "MarkingQueue", StringValue(spec.markingPortQueue), 
                                                                "Bands", UintegerValue(numDistinct));

                    // 3. Crear las clases para el Root (una por banda)
                    TrafficControlHelper::ClassIdList cid = tch.AddQueueDiscClasses(roothandle, 2, "ns3::QueueDiscClass");           
                
                    tch.AddChildQueueDisc(roothandle, cid[0], "ns3::FifoQueueDisc", 
                                        "MaxSize", StringValue("1000p"));
                    
                    
                    tch.AddChildQueueDisc(roothandle, cid[1], "ns3::WrrQueueDisc", 
                                        "Quantum", StringValue(WeightsToIntegers(cfg.weights)), 
                                        "MapQueue", StringValue(DecrementMapQueue(spec.mapQueue)), 
                                        "MaxSize", StringValue("1000p"), "Bands", UintegerValue(numDistinct));
                        
                    std::cout << "Configuring SPWRR with " << numDistinct << " bands. Root MapQueue=" << spec.mapQueue 
                              << ", MarkingQueue=" << spec.markingPortQueue 
                              << ", Weights for WRR band: " << cfg.weights 
                              << " and MaxSize=" << maxSizeStr << "\n";


                }   

            }else {
                std::cout << "Configuring default FifoQueueDisc with MaxSize=" << maxSizeStr << " for node " << nodesInLink[i] << "\n";
                tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", StringValue(maxSizeStr));
            }
             
            QueueDiscContainer qdisc = tch.Install(d.Get(i));
            // Ptr<QueueDisc> root = qdisc.Get(0);
            // for (uint32_t j = 0; j < root->GetNQueueDiscClasses(); ++j) {
            //     Ptr<QueueDiscClass> qdc = root->GetQueueDiscClass(j);
            //     Ptr<QueueDisc> child = qdc->GetQueueDisc();
                
            // }
                        

            Ptr<QueueDisc> qd = qdisc.Get(0);
            qd->Initialize();
            std::cout << "=== Configuración en Nodo " << nodesInLink[i] << " ===" << std::endl;
            std::cout << "Tipo Root: " << qd->GetInstanceTypeId().GetName() << std::endl;
            std::cout << "Bandas/Clases: " << qd->GetNQueueDiscClasses() << std::endl;
            std::cout << "Colas Internas Root: " << qd->GetNInternalQueues() << std::endl;

            // Si tiene hijos, listarlos
            for (uint32_t j = 0; j < qd->GetNQueueDiscClasses(); ++j) {
                Ptr<QueueDisc> child = qd->GetQueueDiscClass(j)->GetQueueDisc();
                std::cout << "  -> Hijo " << j << ": " << child->GetInstanceTypeId().GetName() 
                        << " (Internal Queues: " << child->GetNInternalQueues() << ")" << std::endl;
                        child->TraceConnectWithoutContext("SojournTime", 
                         MakeBoundCallback(&ChildSojournTrace, nodesInLink[i], j,Ptr<QueueDisc>(child)));
                        if (child->GetInstanceTypeId().GetName() == "ns3::WrrQueueDisc") {
                            uint32_t nInternal = child->GetNQueueDiscClasses();
                            std::cout << "     [WRR] Detectadas " << nInternal << " colas internas." << std::endl;
                            
                            for (uint32_t k = 0; k < nInternal; ++k) {
                                // CAMBIO AQUÍ: Usamos Ptr<Queue<QueueDiscItem>> o directamente el rastro desde el objeto
                                // Pero la forma más segura en ns-3.39 es:
                                // auto iq = child->GetInternalQueue(k); 
                                Ptr<QueueDisc> iq = child->GetQueueDiscClass(k)->GetQueueDisc(); // En ns-3.39, cada clase interna tiene una sola cola interna
                                std::cout << "         Cola interna " << k << ": " << (iq ? iq->GetInstanceTypeId().GetName() : "nullptr") << std::endl;
                                if (iq) {
                                    iq->TraceConnectWithoutContext("SojournTime", 
                                        MakeBoundCallback(&WrrChildTrace, (uint32_t)nodesInLink[i], k, iq));
                                }
                            }
                        }
            }
            }

        // --- ASIGNACIÓN DE IP ÚNICA ---
        std::stringstream ss;
        ss << "10.0." << (u + 1) << (v + 1) << ".0"; // Siempre genera la misma red para el par
        ipv4.SetBase(ss.str().c_str(), "255.255.255.0");
        ipv4.Assign(d);
    }

    // 3. STATIC ROUTING MANUAL
    Ipv4StaticRoutingHelper staticRoutingHelper;
    std::ifstream rFile(routingFile);
    std::string rLine;
    uint32_t rowIdx = 0;

    // std::cout << "\n--- Configurando Rutas Estáticas ---" << std::endl;


    while (std::getline(rFile, rLine) && rowIdx <= maxNodeId) {
        std::stringstream ss(rLine);
        std::string val;
        uint32_t colIdx = 0;
        Ptr<Ipv4> ipv4Obj = nodes.Get(rowIdx)->GetObject<Ipv4>();
        Ptr<Ipv4StaticRouting> sr = staticRoutingHelper.GetStaticRouting(ipv4Obj);
        // std::cout << "Nodo " << rowIdx << ":\n";
        while (std::getline(ss, val, ',')) {
            int portIndex = std::stoi(val);

            if (portIndex != -1 && rowIdx != colIdx) {
                // Buscamos la IP del nodo destino. 
                // IMPORTANTE: Un nodo tiene varias IPs (una por interfaz). 
                // Buscamos la IP que esté en la subred que el nodo 'rowIdx' conoce.
                Ptr<Ipv4> destIpv4 = nodes.Get(colIdx)->GetObject<Ipv4>();
                Ipv4Address destAddr;
                
                // Lógica para encontrar la IP correcta del destino en esta topología
                // Por simplicidad en dumbbell, tomamos la primera dirección global (interfaz 1)
                if (destIpv4->GetNInterfaces() > 1) {
                    destAddr = destIpv4->GetAddress(1, 0).GetLocal();
                    sr->AddHostRouteTo(destAddr, portIndex + 1);
                    
                    // std::cout << "Nodo " << rowIdx << " -> Destino Nodo " << colIdx 
                    //         << " (IP: " << destAddr << ") por Port: " << portIndex << "(+1 for ns3)" << std::endl;
                }
            }
            colIdx++;
        }
        rowIdx++;
    // }
    }
    // Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    // Ipv4GlobalRoutingHelper g;
    // Ptr<OutputStreamWrapper> routingStream =
    // Create<OutputStreamWrapper>(g_outputDir + "/routing-table.txt", std::ios::out);

    // g.PrintRoutingTableAllAt(Seconds(1.0), routingStream);
    // ============================================
    // APPLICATIONS 
    // ============================================
    std::cout << "\n--- Configurando Aplicaciones ---" << std::endl;
    for (const auto& f : spec.flows) {
        std::ofstream(g_outputDir + "/txfileflow" + std::to_string(f.flowId) + ".txt").close();
        std::ofstream(g_outputDir + "/rxfileflow" + std::to_string(f.flowId) + ".txt").close();

        Ptr<Ipv4> ipv4Dest = nodes.Get(f.destinationNode)->GetObject<Ipv4>();
        Address remoteAddr(InetSocketAddress(ipv4Dest->GetAddress(1,0).GetLocal(), f.port));

        if (f.timeDistType == TimeDistType::ONOFF_T) { // ON/OFF GENERATION
            OnOffHelper helper("ns3::UdpSocketFactory", remoteAddr);
            helper.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(f.avgTOn) + "]"));  // seconds
            helper.SetAttribute("StartInOnState", BooleanValue(f.StartInOnState));
            helper.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(f.avgTOff) + "]")); // seconds
            helper.SetAttribute("PacketSize", UintegerValue(f.avgPktSize));
            helper.SetAttribute("DataRate", StringValue(std::to_string((f.pktsLambdaOn * f.avgPktSize * 8)) + "bps"));
            helper.SetAttribute("DSCP", UintegerValue(f.dscp));
            ApplicationContainer app = helper.Install(nodes.Get(f.sourceNode));
            app.Start(Seconds(0.0));
            DynamicCast<OnOffApplication>(app.Get(0))->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PacketTxCallback, f.flowId));
        } else { // EXP GENERATION
            DistributionHelper helper("ns3::UdpSocketFactory", remoteAddr);
            helper.SetAttribute("ArrivalGen", StringValue("Exp"));
            helper.SetAttribute("PacketGen", StringValue("Constant"));
            helper.SetAttribute("PacketSize", UintegerValue(f.avgPktSize));
            helper.SetAttribute("Interval", DoubleValue(1/(f.avgPktsLambda)));
            helper.SetAttribute("DSCP", UintegerValue(f.dscp));
            ApplicationContainer app = helper.Install(nodes.Get(f.sourceNode));
            app.Start(Seconds(0.0));
    
            DynamicCast<Distributionapp>(app.Get(0))->TraceConnectWithoutContext("Tx", MakeBoundCallback(&PacketTxCallback, f.flowId));
        }
        PacketSinkHelper sink("ns3::UdpSocketFactory", remoteAddr);
        ApplicationContainer sApp = sink.Install(nodes.Get(f.destinationNode));
        sApp.Start(Seconds(0.0));
        DynamicCast<PacketSink>(sApp.Get(0))->TraceConnectWithoutContext("Rx", MakeBoundCallback(&PacketRxCallback, f.flowId));
    }
    std::cout << "Starting Automated Simulation..." << std::endl;
    Simulator::Stop(Seconds(simulationTime + 1));
    Simulator::Run();
    std::cout << "Simulation finished succesfully !!!" << std::endl;

    if (g_intermediateSojournFile.is_open()) {
        g_intermediateSojournFile.close();
    }
    Simulator::Destroy();
    return 0;
}