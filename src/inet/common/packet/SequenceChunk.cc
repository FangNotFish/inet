//
    // This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

#include "inet/common/packet/SequenceChunk.h"

namespace inet {

SequenceChunk::SequenceChunk() :
    Chunk()
{
}

SequenceChunk::SequenceChunk(const SequenceChunk& other) :
    Chunk(other),
    chunks(other.isImmutable() ? other.chunks : other.dupChunks())
{
}

SequenceChunk::SequenceChunk(const std::vector<std::shared_ptr<Chunk>>& chunks) :
    Chunk(),
    chunks(chunks)
{
}

void SequenceChunk::makeImmutable()
{
    Chunk::makeImmutable();
    for (auto& chunk : chunks)
        chunk->makeImmutable();
}

std::vector<std::shared_ptr<Chunk> > SequenceChunk::dupChunks() const
{
    std::vector<std::shared_ptr<Chunk> > copies;
    for (auto& chunk : chunks)
        copies.push_back(chunk->isImmutable() ? chunk : chunk->dupShared());
    return copies;
}

int64_t SequenceChunk::getByteLength() const
{
    int64_t byteLength = 0;
    for (auto& chunk : chunks)
        byteLength += chunk->getByteLength();
    return byteLength;
}

void SequenceChunk::seekIterator(Iterator& iterator, int64_t byteOffset) const
{
    iterator.setPosition(byteOffset);
    if (byteOffset == 0)
        iterator.setIndex(0);
    else {
        int startIndex = getStartIndex(iterator);
        int endIndex = getEndIndex(iterator);
        int increment = getIndexIncrement(iterator);
        int64_t p = 0;
        for (int i = startIndex; i != endIndex + increment; i += increment) {
            p += chunks[i]->getByteLength();
            if (p == byteOffset) {
                iterator.setIndex(i + 1);
                return;
            }
        }
        iterator.setIndex(-1);
    }
}

void SequenceChunk::moveIterator(Iterator& iterator, int64_t byteLength) const
{
    iterator.setPosition(iterator.getPosition() + byteLength);
    if (iterator.getIndex() != -1 && iterator.getIndex() != chunks.size() && getElementChunk(iterator)->getByteLength() == byteLength)
        iterator.setIndex(iterator.getIndex() + 1);
    else
        iterator.setIndex(-1);
}

std::shared_ptr<Chunk> SequenceChunk::peekWithIterator(const Iterator& iterator, int64_t byteLength) const
{
    if (iterator.getIndex() != -1 && iterator.getIndex() != chunks.size()) {
        const auto& chunk = getElementChunk(iterator);
        if (byteLength == -1 || chunk->getByteLength() == byteLength)
            return chunk;
    }
    return nullptr;
}

std::shared_ptr<Chunk> SequenceChunk::peekWithLinearSearch(const Iterator& iterator, int64_t byteLength) const
{
    int position = 0;
    int startIndex = getStartIndex(iterator);
    int endIndex = getEndIndex(iterator);
    int increment = getIndexIncrement(iterator);
    for (int i = startIndex; i != endIndex + increment; i += increment) {
        auto& chunk = chunks[i];
        if (iterator.getPosition() == position && (byteLength == -1 || byteLength == chunk->getByteLength()))
            return chunk;
        position += chunk->getByteLength();
    }
    return nullptr;
}

bool SequenceChunk::mergeToEnd(const std::shared_ptr<Chunk>& chunk)
{
    if (chunks.size() != 0) {
        auto& lastChunk = chunks.back();
        if (lastChunk->isImmutable() && chunk->isImmutable()) {
            auto mergedChunk = lastChunk->dupShared();
            if (mergedChunk->insertToEnd(chunk)) {
                if (mergedChunk->getChunkType() == TYPE_SLICE) {
                    auto sliceChunk = std::static_pointer_cast<SliceChunk>(mergedChunk);
                    if (sliceChunk->getByteOffset() == 0 && sliceChunk->getByteLength() == sliceChunk->getChunk()->getByteLength()) {
                        chunks.back() = sliceChunk->getChunk();
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool SequenceChunk::insertToBeginning(const std::shared_ptr<Chunk>& chunk)
{
    assertMutable();
    handleChange();
    if (chunk->getByteLength() <= 0)
        throw cRuntimeError("Invalid chunk length: %li", chunk->getByteLength());
    chunks.insert(chunks.begin(), chunk);
    return true;
}

void SequenceChunk::insertToBeginning(const std::shared_ptr<SequenceChunk>& chunk)
{
    for (auto it = chunk->chunks.rbegin(); it != chunk->chunks.rend(); it++)
        insertToBeginning(*it);
}

void SequenceChunk::prepend(const std::shared_ptr<Chunk>& chunk, bool flatten)
{
    if (!flatten)
        insertToBeginning(chunk);
    else if (chunk->getChunkType() == TYPE_SLICE)
        insertToBeginning(std::static_pointer_cast<SliceChunk>(chunk));
    else if (chunk->getChunkType() == TYPE_SEQUENCE)
        insertToBeginning(std::static_pointer_cast<SequenceChunk>(chunk));
    else
        insertToBeginning(chunk);
}

bool SequenceChunk::insertToEnd(const std::shared_ptr<Chunk>& chunk)
{
    assertMutable();
    handleChange();
    if (chunk->getByteLength() <= 0)
        throw cRuntimeError("Invalid chunk length: %li", chunk->getByteLength());
    if (!mergeToEnd(chunk))
        chunks.push_back(chunk);
    return true;
}

void SequenceChunk::insertToEnd(const std::shared_ptr<SliceChunk>& sliceChunk)
{
    if (sliceChunk->getChunk()->getChunkType() == TYPE_SEQUENCE) {
        auto sequenceChunk = std::static_pointer_cast<SequenceChunk>(sliceChunk->getChunk());
        int64_t byteOffset = 0;
        for (auto& elementChunk : sequenceChunk->chunks) {
            int64_t sliceChunkBegin = sliceChunk->getByteOffset();
            int64_t sliceChunkEnd = sliceChunk->getByteOffset() + sliceChunk->getByteLength();
            int64_t chunkBegin = byteOffset;
            int64_t chunkEnd = byteOffset + elementChunk->getByteLength();
            if (sliceChunkBegin <= chunkBegin && chunkEnd <= sliceChunkEnd)
                insertToEnd(elementChunk);
            else if (chunkBegin < sliceChunkBegin && sliceChunkBegin < chunkEnd)
                insertToEnd(std::make_shared<SliceChunk>(elementChunk, sliceChunkBegin - chunkBegin, chunkEnd - sliceChunkBegin));
            else if (chunkBegin < sliceChunkEnd && sliceChunkEnd < chunkEnd)
                insertToEnd(std::make_shared<SliceChunk>(elementChunk, 0, chunkEnd - sliceChunkEnd));
            byteOffset += elementChunk->getByteLength();
        }
    }
    else
        insertToEnd(std::static_pointer_cast<Chunk>(sliceChunk));
}

void SequenceChunk::insertToEnd(const std::shared_ptr<SequenceChunk>& chunk)
{
    for (auto& chunk : chunk->chunks)
        insertToEnd(chunk);
}

void SequenceChunk::append(const std::shared_ptr<Chunk>& chunk, bool flatten)
{
    if (!flatten)
        insertToEnd(chunk);
    else if (chunk->getChunkType() == TYPE_SLICE)
        insertToEnd(std::static_pointer_cast<SliceChunk>(chunk));
    else if (chunk->getChunkType() == TYPE_SEQUENCE)
        insertToEnd(std::static_pointer_cast<SequenceChunk>(chunk));
    else
        insertToEnd(chunk);
}

bool SequenceChunk::removeFromBeginning(int64_t byteLength)
{
    handleChange();
    auto it = chunks.begin();
    while (it != chunks.end()) {
        auto chunk = *it;
        int64_t chunkByteLength = chunk->getByteLength();
        if (chunkByteLength <= byteLength) {
            it++;
            byteLength -= chunkByteLength;
        }
        else {
            *it = std::make_shared<SliceChunk>(chunk, byteLength, chunkByteLength - byteLength);
            byteLength = 0;
        }
        if (byteLength == 0)
            break;
    }
    chunks.erase(chunks.begin(), it);
    return true;
}

bool SequenceChunk::removeFromEnd(int64_t byteLength)
{
    handleChange();
    auto it = chunks.rbegin();
    while (it != chunks.rend()) {
        auto chunk = *it;
        int64_t chunkByteLength = chunk->getByteLength();
        if (chunkByteLength <= byteLength) {
            it++;
            byteLength -= chunkByteLength;
        }
        else {
            *it = std::make_shared<SliceChunk>(chunk, 0, chunkByteLength - byteLength);
            byteLength = 0;
        }
        if (byteLength == 0)
            break;
    }
    chunks.erase((++it).base(), chunks.end());
    return true;
}

std::string SequenceChunk::str() const
{
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (auto& chunk : chunks) {
        if (!first)
            os << " | ";
        else
            first = false;
        os << chunk->str();
    }
    os << "]";
    return os.str();
}

} // namespace