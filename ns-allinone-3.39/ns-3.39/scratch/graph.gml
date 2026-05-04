graph [
  directed 1
  multigraph 1

  node [
    id 0
    label "0"
    schedulingPolicy "SP"
    schedulingWeights "66,33"
    queueSizes "1,1"
    levelsQoS "2"
  ]

  node [
    id 1
    label "1"
    schedulingPolicy "SP"
    schedulingWeights "66,33"
    queueSizes "1,1"
    levelsQoS "3"
    tostoQoSqueues "0;1;2"
  ]

  node [
    id 2
    label "2"
    schedulingPolicy "SP"
    schedulingWeights "66,33"
    queueSizes "1,1"
    levelsQoS "2"
  ]

  node [
    id 3
    label "3"
    schedulingPolicy "SP"
    schedulingWeights "66,33"
    queueSizes "1,1"
    levelsQoS "2"
  ]

  edge [
    source 0
    target 1
    key 0
    port 0
    weight 1
    bandwidth "10000000"
  ]

  edge [
    source 1
    target 0
    key 0
    port 0
    weight 1
   bandwidth "2000000"
  ]

  edge [
    source 1
    target 2
    key 0
    port 1
    weight 1
   bandwidth "2000000"
  ]

  edge [
    source 2
    target 1
    key 0
    port 0
    weight 1
   bandwidth "2000000"
  ]

  edge [
    source 2
    target 3
    key 0
    port 1
    weight 1
    bandwidth "2000000"
  ]

  edge [
    source 3
    target 2
    key 0
    port 0
    weight 1
   bandwidth "2000000"
  ]
]