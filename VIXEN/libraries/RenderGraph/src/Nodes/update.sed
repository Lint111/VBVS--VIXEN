s/, \&octreeNodesBuffer)/, octreeNodesBuffer_.GetPtr())/g
s/, octreeNodesBuffer, \&nodesMemReq)/, octreeNodesBuffer_.Get(), \&nodesMemReq)/g
s/, \&octreeNodesMemory)/, octreeNodesMemory_.GetPtr())/g
s/, octreeNodesBuffer, octreeNodesMemory, 0)/, octreeNodesBuffer_.Get(), octreeNodesMemory_.Get(), 0)/g
s/, \&octreeBricksBuffer)/, octreeBricksBuffer_.GetPtr())/g
s/, octreeBricksBuffer, \&bricksMemReq)/, octreeBricksBuffer_.Get(), \&bricksMemReq)/g
s/, \&octreeBricksMemory)/, octreeBricksMemory_.GetPtr())/g
s/, octreeBricksBuffer, octreeBricksMemory, 0)/, octreeBricksBuffer_.Get(), octreeBricksMemory_.Get(), 0)/g
s/, \&octreeMaterialsBuffer)/, octreeMaterialsBuffer_.GetPtr())/g
s/, octreeMaterialsBuffer, \&materialsMemReq)/, octreeMaterialsBuffer_.Get(), \&materialsMemReq)/g
s/, \&octreeMaterialsMemory)/, octreeMaterialsMemory_.GetPtr())/g
s/, octreeMaterialsBuffer, octreeMaterialsMemory, 0)/, octreeMaterialsBuffer_.Get(), octreeMaterialsMemory_.Get(), 0)/g
