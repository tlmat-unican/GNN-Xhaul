graph [
  directed 1
  multigraph 1
  node [
    id 0
    label "0"
    schedulingPolicy "SPWFQ"
    schedulingWeights "40,60"
    queueSizes "30259,30259,30259"
    levelsQoS "3"
    HQoSlevels "0,1,1"
    queuePolicies "SP,WFQ,WFQ"
    tosToQoSqueue "0;1;2"
  ]
  node [
    id 1
    label "1"
    schedulingPolicy "SPWFQ"
    schedulingWeights "40,60"
    queueSizes "30259,30259,30259"
    levelsQoS "3"
    HQoSlevels "0,1,1"
    queuePolicies "SP,WFQ,WFQ"
    tosToQoSqueue "0;1;2"
  ]
  node [
    id 2
    label "2"
    schedulingPolicy "SPWFQ"
    schedulingWeights "40,60"
    queueSizes "30259,30259,30259"
    levelsQoS "3"
    HQoSlevels "0,1,1"
    queuePolicies "SP,WFQ,WFQ"
    tosToQoSqueue "0;1;2"
  ]
  node [
    id 3
    label "3"
    schedulingPolicy "SPWFQ"
    schedulingWeights "40,60"
    queueSizes "30259,30259,30259"
    levelsQoS "3"
    HQoSlevels "0,1,1"
    queuePolicies "SP,WFQ,WFQ"
    tosToQoSqueue "0;1;2"
  ]
  node [
    id 4
    label "4"
    schedulingPolicy "SPWFQ"
    schedulingWeights "40,60"
    queueSizes "30259,30259,30259"
    levelsQoS "3"
    HQoSlevels "0,1,1"
    queuePolicies "SP,WFQ,WFQ"
    tosToQoSqueue "0;1;2"
  ]
  edge [
    source 0
    target 1
    key 0
    port 0
    weight 1
    bandwidth "2310748746.0"
  ]
  edge [
    source 1
    target 0
    key 0
    port 0
    weight 1
    bandwidth "2310748746.0"
  ]
  edge [
    source 1
    target 2
    key 0
    port 1
    weight 1
    bandwidth "2310748746.0"
  ]
  edge [
    source 1
    target 4
    key 0
    port 2
    weight 1
    bandwidth "2310748746.0"
  ]
  edge [
    source 2
    target 1
    key 0
    port 0
    weight 1
    bandwidth "2310748746.0"
  ]
  edge [
    source 2
    target 3
    key 0
    port 1
    weight 1
    bandwidth "2310748746.0"
  ]
  edge [
    source 2
    target 4
    key 0
    port 2
    weight 1
    bandwidth "2310748746.0"
  ]
  edge [
    source 3
    target 2
    key 0
    port 0
    weight 1
    bandwidth "2310748746.0"
  ]
  edge [
    source 4
    target 1
    key 0
    port 0
    weight 1
    bandwidth "2310748746.0"
  ]
  edge [
    source 4
    target 2
    key 0
    port 1
    weight 1
    bandwidth "2310748746.0"
  ]
]
