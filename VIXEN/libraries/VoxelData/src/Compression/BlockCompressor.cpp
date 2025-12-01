#include "Compression/BlockCompressor.h"
#include <cstring>

namespace VoxelData {

// ============================================================================
// IBlockCompressor - Default bulk implementations
// ============================================================================

size_t IBlockCompressor::encodeBulk(
    const void* input,
    size_t elementCount,
    void* output
) const {
    const size_t blockSize = getBlockSize();
    const size_t elemSize = getUncompressedElementSize();
    const size_t compressedSize = getCompressedBlockSize();

    const uint8_t* inPtr = static_cast<const uint8_t*>(input);
    uint8_t* outPtr = static_cast<uint8_t*>(output);

    size_t blockCount = 0;
    size_t remaining = elementCount;

    while (remaining > 0) {
        size_t validCount = (remaining >= blockSize) ? blockSize : remaining;

        // Sequential indices for bulk encoding
        encodeBlock(inPtr, validCount, nullptr, outPtr);

        inPtr += blockSize * elemSize;
        outPtr += compressedSize;
        remaining -= validCount;
        blockCount++;
    }

    return blockCount;
}

void IBlockCompressor::decodeBulk(
    const void* input,
    size_t blockCount,
    void* output
) const {
    const size_t blockSize = getBlockSize();
    const size_t elemSize = getUncompressedElementSize();
    const size_t compressedSize = getCompressedBlockSize();

    const uint8_t* inPtr = static_cast<const uint8_t*>(input);
    uint8_t* outPtr = static_cast<uint8_t*>(output);

    for (size_t i = 0; i < blockCount; ++i) {
        decodeBlock(inPtr, outPtr);
        inPtr += compressedSize;
        outPtr += blockSize * elemSize;
    }
}

// ============================================================================
// CompressedBuffer
// ============================================================================

CompressedBuffer::CompressedBuffer(std::unique_ptr<IBlockCompressor> compressor)
    : m_compressor(std::move(compressor))
{
}

void CompressedBuffer::compress(const void* source, size_t elementCount) {
    m_elementCount = elementCount;

    // Calculate required compressed size
    size_t compressedSize = m_compressor->calculateCompressedSize(elementCount);
    m_compressedData.resize(compressedSize);

    // Encode all blocks
    m_compressor->encodeBulk(source, elementCount, m_compressedData.data());
}

void CompressedBuffer::decompress(void* dest, size_t elementCount) const {
    size_t blockCount = (elementCount + m_compressor->getBlockSize() - 1) /
                        m_compressor->getBlockSize();

    m_compressor->decodeBulk(m_compressedData.data(), blockCount, dest);
}

} // namespace VoxelData
