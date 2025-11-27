s/, nodesStagingBuffer, nodesStagingMemory, 0)/, nodesStagingBuffer_.Get(), nodesStagingMemory_.Get(), 0)/g
s/, bricksStagingBuffer, bricksStagingMemory, 0)/, bricksStagingBuffer_.Get(), bricksStagingMemory_.Get(), 0)/g
s/, materialsStagingBuffer, materialsStagingMemory, 0)/, materialsStagingBuffer_.Get(), materialsStagingMemory_.Get(), 0)/g
s/vkCmdCopyBuffer(cmdBuffer, nodesStagingBuffer, octreeNodesBuffer, 1, \&copyRegion)/vkCmdCopyBuffer(cmdBuffer, nodesStagingBuffer_.Get(), octreeNodesBuffer_.Get(), 1, \&copyRegion)/g
s/vkCmdCopyBuffer(cmdBuffer, bricksStagingBuffer, octreeBricksBuffer, 1, \&copyRegion)/vkCmdCopyBuffer(cmdBuffer, bricksStagingBuffer_.Get(), octreeBricksBuffer_.Get(), 1, \&copyRegion)/g
s/vkCmdCopyBuffer(cmdBuffer, materialsStagingBuffer, octreeMaterialsBuffer, 1, \&copyRegion)/vkCmdCopyBuffer(cmdBuffer, materialsStagingBuffer_.Get(), octreeMaterialsBuffer_.Get(), 1, \&copyRegion)/g
