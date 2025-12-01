#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include <type_traits>

namespace VoxelData {

/**
 * IBlockCompressor - Type-erased interface for block compression algorithms
 *
 * Block compression encodes N uncompressed elements into a fixed-size compressed block.
 * This is the pattern used by DXT/BC texture compression and ESVO voxel attributes.
 *
 * Key properties:
 * - Fixed block size (e.g., 16 elements for DXT)
 * - Fixed compressed output size (e.g., 8 bytes for DXT1)
 * - Lossy compression (encode/decode may not be exact)
 *
 * Usage:
 *   auto compressor = std::make_unique<DXT1ColorCompressor>();
 *   std::vector<uint8_t> compressed(compressor->getCompressedBlockSize());
 *   compressor->encodeBlock(colors.data(), 16, compressed.data());
 */
class IBlockCompressor {
public:
    virtual ~IBlockCompressor() = default;

    // ========================================================================
    // BLOCK ENCODING/DECODING
    // ========================================================================

    /**
     * Encode a block of elements into compressed form
     *
     * @param input Pointer to input elements (must have at least validCount elements)
     * @param validCount Number of valid elements (may be < blockSize for partial blocks)
     * @param indices Index mapping for each valid element (0-15 for DXT)
     *                If nullptr, assumes sequential indices [0, 1, 2, ...]
     * @param output Pointer to output buffer (must have getCompressedBlockSize() bytes)
     */
    virtual void encodeBlock(
        const void* input,
        size_t validCount,
        const int32_t* indices,
        void* output
    ) const = 0;

    /**
     * Decode a compressed block back to elements
     *
     * @param input Pointer to compressed block (getCompressedBlockSize() bytes)
     * @param output Pointer to output buffer (must have blockSize * elementSize bytes)
     */
    virtual void decodeBlock(
        const void* input,
        void* output
    ) const = 0;

    // ========================================================================
    // BULK OPERATIONS
    // ========================================================================

    /**
     * Encode multiple blocks from contiguous input
     *
     * @param input Pointer to input elements
     * @param elementCount Total number of input elements
     * @param output Pointer to output buffer for compressed blocks
     * @returns Number of compressed blocks written
     */
    virtual size_t encodeBulk(
        const void* input,
        size_t elementCount,
        void* output
    ) const;

    /**
     * Decode multiple blocks to contiguous output
     *
     * @param input Pointer to compressed blocks
     * @param blockCount Number of blocks to decode
     * @param output Pointer to output buffer for decompressed elements
     */
    virtual void decodeBulk(
        const void* input,
        size_t blockCount,
        void* output
    ) const;

    // ========================================================================
    // PROPERTIES
    // ========================================================================

    /// Number of elements per compression block (e.g., 16 for DXT)
    virtual size_t getBlockSize() const = 0;

    /// Size in bytes of one compressed block (e.g., 8 for DXT1)
    virtual size_t getCompressedBlockSize() const = 0;

    /// Size in bytes of one uncompressed element (e.g., 12 for vec3)
    virtual size_t getUncompressedElementSize() const = 0;

    /// Compression ratio (uncompressed / compressed)
    float getCompressionRatio() const {
        return static_cast<float>(getBlockSize() * getUncompressedElementSize()) /
               static_cast<float>(getCompressedBlockSize());
    }

    /// Calculate compressed size for given element count
    size_t calculateCompressedSize(size_t elementCount) const {
        size_t blockCount = (elementCount + getBlockSize() - 1) / getBlockSize();
        return blockCount * getCompressedBlockSize();
    }

    /// Human-readable name for this compressor
    virtual const char* getName() const = 0;
};

/**
 * BlockCompressor - Typed base class for specific compressors
 *
 * Provides type-safe interface while implementing IBlockCompressor.
 *
 * Template parameters:
 * - InputT: Type of uncompressed elements (e.g., glm::vec3)
 * - OutputT: Type of compressed block (e.g., uint64_t)
 * - BlockSize: Number of elements per block (default 16)
 */
template<typename InputT, typename OutputT, size_t BlockSize = 16>
class BlockCompressor : public IBlockCompressor {
public:
    using InputType = InputT;
    using OutputType = OutputT;
    static constexpr size_t BLOCK_SIZE = BlockSize;

    // Type-safe encoding
    virtual OutputT encodeBlockTyped(
        const InputT* elements,
        size_t validCount,
        const int32_t* indices = nullptr
    ) const = 0;

    // Type-safe decoding
    virtual void decodeBlockTyped(
        const OutputT& block,
        InputT* output
    ) const = 0;

    // ========================================================================
    // IBlockCompressor implementation
    // ========================================================================

    void encodeBlock(
        const void* input,
        size_t validCount,
        const int32_t* indices,
        void* output
    ) const override {
        OutputT result = encodeBlockTyped(
            static_cast<const InputT*>(input),
            validCount,
            indices
        );
        *static_cast<OutputT*>(output) = result;
    }

    void decodeBlock(
        const void* input,
        void* output
    ) const override {
        decodeBlockTyped(
            *static_cast<const OutputT*>(input),
            static_cast<InputT*>(output)
        );
    }

    size_t getBlockSize() const override { return BlockSize; }
    size_t getCompressedBlockSize() const override { return sizeof(OutputT); }
    size_t getUncompressedElementSize() const override { return sizeof(InputT); }
};

/**
 * CompressedBuffer - Manages compressed data with automatic encoding/decoding
 *
 * Wraps a compressor and provides buffer management for GPU upload.
 */
class CompressedBuffer {
public:
    CompressedBuffer(std::unique_ptr<IBlockCompressor> compressor);

    /// Compress data from source buffer
    void compress(const void* source, size_t elementCount);

    /// Decompress to destination buffer
    void decompress(void* dest, size_t elementCount) const;

    /// Get compressed data for GPU upload
    const void* getCompressedData() const { return m_compressedData.data(); }
    size_t getCompressedSize() const { return m_compressedData.size(); }

    /// Get compressor info
    const IBlockCompressor& getCompressor() const { return *m_compressor; }
    size_t getElementCount() const { return m_elementCount; }

private:
    std::unique_ptr<IBlockCompressor> m_compressor;
    std::vector<uint8_t> m_compressedData;
    size_t m_elementCount = 0;
};

} // namespace VoxelData
