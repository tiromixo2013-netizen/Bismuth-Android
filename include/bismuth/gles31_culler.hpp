#pragma once
#if defined(__ANDROID__) && defined(BISMUTH_ANDROID_GLES31)
#include "bismuth/culling_types.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Geode's Android Cocos headers alias the GLES vertex-array entry points to
// OES symbols. Those macros collide with the GLES 3.1 prototypes. Temporarily
// hide only those aliases while parsing the official GLES header, then restore
// the Cocos compatibility macros for the rest of the translation unit.
#pragma push_macro("glBindVertexArray")
#pragma push_macro("glDeleteVertexArrays")
#pragma push_macro("glGenVertexArrays")
#pragma push_macro("glBindVertexArrayOES")
#pragma push_macro("glDeleteVertexArraysOES")
#pragma push_macro("glGenVertexArraysOES")
#undef glBindVertexArray
#undef glDeleteVertexArrays
#undef glGenVertexArrays
#undef glBindVertexArrayOES
#undef glDeleteVertexArraysOES
#undef glGenVertexArraysOES
#include <GLES3/gl31.h>
#pragma pop_macro("glGenVertexArraysOES")
#pragma pop_macro("glDeleteVertexArraysOES")
#pragma pop_macro("glBindVertexArrayOES")
#pragma pop_macro("glGenVertexArrays")
#pragma pop_macro("glDeleteVertexArrays")
#pragma pop_macro("glBindVertexArray")

namespace bismuth {
class Gles31Culler {
public:
    Gles31Culler() = default;
    ~Gles31Culler();
    Gles31Culler(const Gles31Culler&) = delete;
    Gles31Culler& operator=(const Gles31Culler&) = delete;
    bool initialize(std::string* error = nullptr);
    bool uploadCandidates(const std::vector<Candidate>& candidates, std::uint32_t atlasCapacity, std::string* error = nullptr);
    bool generateAtlasCandidates(GLuint atlasVertexBuffer, std::uint32_t startQuad,
                                 std::uint32_t count, std::uint32_t atlasCapacity,
                                 const float* mvpColumnMajor, std::string* error = nullptr);
    bool dispatch(const Rect& view, std::uint32_t candidateCount, std::string* error = nullptr);
    bool isInitialized() const noexcept {
        return classifyProgram_ != 0 && atlasCandidatesProgram_ != 0 && offsetsProgram_ != 0 && scatterProgram_ != 0 &&
               candidates_ != 0 && flags_ != 0 && localOffsets_ != 0 &&
               groupSums_ != 0 && groupOffsets_ != 0 && visibleIndices_ != 0 &&
               indirectCommand_ != 0 && indexedCommand_ != 0 && atlasElementIndices_ != 0;
    }
    GLuint visibleIndexBuffer() const noexcept { return visibleIndices_; }
    GLuint indirectCommandBuffer() const noexcept { return indirectCommand_; }
    GLuint indexedCommandBuffer() const noexcept { return indexedCommand_; }
    GLuint atlasElementIndexBuffer() const noexcept { return atlasElementIndices_; }
    std::uint32_t capacity() const noexcept { return capacity_; }
    void reset() noexcept;
private:
    GLuint classifyProgram_ = 0, atlasCandidatesProgram_ = 0, offsetsProgram_ = 0, scatterProgram_ = 0;
    GLuint candidates_ = 0, flags_ = 0, localOffsets_ = 0, groupSums_ = 0;
    GLuint groupOffsets_ = 0, visibleIndices_ = 0, indirectCommand_ = 0, indexedCommand_ = 0, atlasElementIndices_ = 0;
    std::uint32_t capacity_ = 0, groupCapacity_ = 0, uploadedCount_ = 0, uploadedAtlasCapacity_ = 0;
    bool allocate(std::uint32_t count, std::string* error);
};
}
#endif
