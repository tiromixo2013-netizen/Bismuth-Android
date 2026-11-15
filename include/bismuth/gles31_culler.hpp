#pragma once
#if defined(__ANDROID__) && defined(BISMUTH_ANDROID_GLES31)
#include "bismuth/culling_types.hpp"
#include <GLES3/gl31.h>
#include <string>
namespace bismuth {
class Gles31Culler {
public:
    Gles31Culler() = default;
    ~Gles31Culler();
    Gles31Culler(const Gles31Culler&) = delete;
    Gles31Culler& operator=(const Gles31Culler&) = delete;
    // Call only with the owning GLES 3.1 context current on this thread.
    bool initialize(std::string* error = nullptr);
    bool uploadCandidates(const std::vector<Candidate>& candidates, std::string* error = nullptr);
    // candidateCount must match the most recently uploaded candidate count.
    // A zero count writes an indirect command with instanceCount=0.
    bool dispatch(const Rect& view, std::uint32_t candidateCount, std::string* error = nullptr);
    GLuint visibleIndexBuffer() const noexcept { return visibleIndices_; }
    GLuint indirectCommandBuffer() const noexcept { return indirectCommand_; }
    std::uint32_t capacity() const noexcept { return capacity_; }
    // Call before the GL context is destroyed or lost. All GL methods, including
    // destruction/reset, require the owning context to be current.
    void reset() noexcept;
private:
    GLuint classifyProgram_ = 0, offsetsProgram_ = 0, scatterProgram_ = 0;
    GLuint candidates_ = 0, flags_ = 0, localOffsets_ = 0, groupSums_ = 0;
    GLuint groupOffsets_ = 0, visibleIndices_ = 0, indirectCommand_ = 0;
    std::uint32_t capacity_ = 0, groupCapacity_ = 0, uploadedCount_ = 0;
    bool allocate(std::uint32_t count, std::string* error);
};
}
#endif
