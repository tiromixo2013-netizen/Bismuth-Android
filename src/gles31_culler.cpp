#include "bismuth/gles31_culler.hpp"
#include "bismuth/atlas_indices.hpp"
#include <cmath>
#if defined(__ANDROID__) && defined(BISMUTH_ANDROID_GLES31)
#include <embedded_shaders.hpp>
namespace bismuth {
namespace {
constexpr std::uint32_t kWorkgroupSize = 128u;
constexpr std::uint32_t kMaxGroups = 128u;
constexpr std::uint32_t kMaxCandidates = kWorkgroupSize * kMaxGroups;

GLuint compileCompute(const char* source, std::string* error) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    if (!shader) { if (error) *error = "glCreateShader failed"; return 0; }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint n = 0; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &n);
        std::string log(n > 0 ? static_cast<size_t>(n) : 1, '\0');
        glGetShaderInfoLog(shader, n, nullptr, log.data());
        if (error) *error = log;
        glDeleteShader(shader);
        return 0;
    }
    GLuint program = glCreateProgram();
    if (!program) { if (error) *error = "glCreateProgram failed"; glDeleteShader(shader); return 0; }
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint n = 0; glGetProgramiv(program, GL_INFO_LOG_LENGTH, &n);
        std::string log(n > 0 ? static_cast<size_t>(n) : 1, '\0');
        glGetProgramInfoLog(program, n, nullptr, log.data());
        if (error) *error = log;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}
class ScopedComputeState {
public:
    ScopedComputeState() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &genericStorage_);
        for (GLuint i = 0; i < 5; ++i) {
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &indexedStorage_[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &indexedStorageStart_[i]);
            glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &indexedStorageSize_[i]);
        }
    }
    ~ScopedComputeState() {
        for (GLuint i = 0; i < 5; ++i) {
            if (indexedStorage_[i] != 0 && indexedStorageSize_[i] > 0) {
                glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i,
                                  static_cast<GLuint>(indexedStorage_[i]),
                                  static_cast<GLintptr>(indexedStorageStart_[i]),
                                  static_cast<GLsizeiptr>(indexedStorageSize_[i]));
            } else {
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, static_cast<GLuint>(indexedStorage_[i]));
            }
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(genericStorage_));
        glUseProgram(static_cast<GLuint>(program_));
    }
    ScopedComputeState(const ScopedComputeState&) = delete;
    ScopedComputeState& operator=(const ScopedComputeState&) = delete;
private:
    GLint program_ = 0;
    GLint genericStorage_ = 0;
    GLint indexedStorage_[5] = {0, 0, 0, 0, 0};
    GLint64 indexedStorageStart_[5] = {0, 0, 0, 0, 0};
    GLint64 indexedStorageSize_[5] = {0, 0, 0, 0, 0};
};

void bufferData(GLuint id, GLsizeiptr bytes) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
}

// Best-effort fail-closed command reset. ScopedComputeState restores the
// generic SSBO binding after this helper returns.
void clearIndirectCommand(GLuint commandBuffer) {
    const GLuint emptyCommand[4] = {6u, 0u, 0u, 0u};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, commandBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(emptyCommand), emptyCommand);
}

void clearIndexedCommand(GLuint commandBuffer) {
    const GLuint emptyCommand[5] = {0u, 0u, 0u, 0u, 0u};
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, commandBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(emptyCommand), emptyCommand);
    // Do not call glGetError() here: GLES exposes a context-wide sticky error
    // flag, so this helper could consume an unrelated error raised by the game.
    // The reset remains best-effort; callers must not consume the command after
    // a rejected dispatch regardless of whether the reset itself can be verified.
}
}

Gles31Culler::~Gles31Culler() { reset(); }

void Gles31Culler::reset() noexcept {
    // GLES object deletion is context-dependent. The owner must call reset()
    // while the context that created these objects is current.
    const GLuint programs[] = {classifyProgram_, offsetsProgram_, scatterProgram_, atlasIndicesProgram_};
    for (GLuint program : programs) if (program) glDeleteProgram(program);
    const GLuint buffers[] = {candidates_, flags_, localOffsets_, groupSums_, groupOffsets_, visibleIndices_, indirectCommand_, indexedCommand_, atlasElementIndices_};
    bool hasBuffer = false;
    for (GLuint buffer : buffers) hasBuffer = hasBuffer || buffer != 0;
    if (hasBuffer) glDeleteBuffers(9, buffers);
    classifyProgram_ = offsetsProgram_ = scatterProgram_ = atlasIndicesProgram_ = 0;
    candidates_ = flags_ = localOffsets_ = groupSums_ = 0;
    groupOffsets_ = visibleIndices_ = indirectCommand_ = indexedCommand_ = atlasElementIndices_ = 0;
    capacity_ = groupCapacity_ = uploadedCount_ = uploadedAtlasCapacity_ = 0;
}

bool Gles31Culler::initialize(std::string* error) {
    if (classifyProgram_ || offsetsProgram_ || scatterProgram_ || atlasIndicesProgram_) {
        if (error) *error = "Culler is already initialized; call reset() first";
        return false;
    }
    GLint major = 0, minor = 0, maxBindings = 0, maxInvocations = 0;
    GLint maxWorkgroupSizeX = 0, maxWorkgroupCountX = 0;
    GLint64 maxStorageBlockSize = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 3 || (major == 3 && minor < 1)) {
        if (error) *error = "OpenGL ES 3.1 is required";
        return false;
    }
    // The culler shares the game's context. Preserve the current program and
    // SSBO bindings so initialization does not leak GL state into the renderer.
    ScopedComputeState state;
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxBindings);
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &maxInvocations);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &maxWorkgroupSizeX);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &maxWorkgroupCountX);
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &maxStorageBlockSize);
    if (maxBindings < 5 || maxInvocations < static_cast<GLint>(kWorkgroupSize) ||
        maxWorkgroupSizeX < static_cast<GLint>(kWorkgroupSize) ||
        maxWorkgroupCountX < static_cast<GLint>(kMaxGroups) ||
        maxStorageBlockSize < static_cast<GLint64>(kMaxCandidates * sizeof(Candidate))) {
        if (error) *error = "GPU limits are below this prototype's GLES 3.1 requirements";
        return false;
    }
    classifyProgram_ = compileCompute(shaders::classify, error);
    if (!classifyProgram_) { reset(); return false; }
    offsetsProgram_ = compileCompute(shaders::offsets, error);
    if (!offsetsProgram_) { reset(); return false; }
    scatterProgram_ = compileCompute(shaders::scatter, error);
    if (!scatterProgram_) { reset(); return false; }
    atlasIndicesProgram_ = compileCompute(shaders::atlasIndices, error);
    if (!atlasIndicesProgram_) { reset(); return false; }
    glGenBuffers(1, &candidates_); glGenBuffers(1, &flags_); glGenBuffers(1, &localOffsets_);
    glGenBuffers(1, &groupSums_); glGenBuffers(1, &groupOffsets_); glGenBuffers(1, &visibleIndices_);
    glGenBuffers(1, &indirectCommand_); glGenBuffers(1, &indexedCommand_); glGenBuffers(1, &atlasElementIndices_);
    if (glGetError() != GL_NO_ERROR || !candidates_ || !flags_ || !localOffsets_ ||
        !groupSums_ || !groupOffsets_ || !visibleIndices_ || !indirectCommand_ || !indexedCommand_ || !atlasElementIndices_) {
        if (error) *error = "Failed to create GLES storage buffers";
        reset(); return false;
    }
    // Allocate the indirect command immediately so an empty-scene dispatch is valid
    // even before the first non-empty candidate upload.
    bufferData(indirectCommand_, 4 * sizeof(GLuint));
    bufferData(indexedCommand_, 5 * sizeof(GLuint));
    if (glGetError() != GL_NO_ERROR) {
        if (error) *error = "Failed to allocate the indirect command buffer";
        reset(); return false;
    }
    return true;
}

bool Gles31Culler::allocate(std::uint32_t count, std::string* error) {
    if (count > kMaxCandidates) {
        if (error) *error = "Prototype currently supports at most 16384 candidates per dispatch";
        return false;
    }
    if (count <= capacity_) return true;
    const std::uint32_t groups = (count + kWorkgroupSize - 1u) / kWorkgroupSize;
    const std::uint32_t newCapacity = groups * kWorkgroupSize;
    bufferData(candidates_, static_cast<GLsizeiptr>(newCapacity) * sizeof(Candidate));
    bufferData(flags_, static_cast<GLsizeiptr>(newCapacity) * sizeof(GLuint));
    bufferData(localOffsets_, static_cast<GLsizeiptr>(newCapacity) * sizeof(GLuint));
    bufferData(groupSums_, static_cast<GLsizeiptr>(groups) * sizeof(GLuint));
    bufferData(groupOffsets_, static_cast<GLsizeiptr>(groups) * sizeof(GLuint));
    bufferData(visibleIndices_, static_cast<GLsizeiptr>(newCapacity) * sizeof(GLuint));
    bufferData(atlasElementIndices_, static_cast<GLsizeiptr>(newCapacity) * 3 * sizeof(GLuint));
    bufferData(indirectCommand_, 4 * sizeof(GLuint));
    if (glGetError() != GL_NO_ERROR) {
        capacity_ = groupCapacity_ = uploadedCount_ = uploadedAtlasCapacity_ = 0;
        if (error) *error = "Failed to allocate GLES storage buffers";
        return false;
    }
    capacity_ = newCapacity; groupCapacity_ = groups;
    return true;
}

bool Gles31Culler::uploadCandidates(const std::vector<Candidate>& candidates, std::uint32_t atlasCapacity, std::string* error) {
    if (!classifyProgram_ || !candidates_ || !indirectCommand_ || !indexedCommand_ || !atlasElementIndices_) {
        if (error) *error = "Culler is not initialized";
        return false;
    }
    ScopedComputeState state;
    if (atlasCapacity > kAtlasMaxQuads16) {
        if (error) *error = "Atlas capacity exceeds the 16-bit GLES atlas limit";
        uploadedCount_ = uploadedAtlasCapacity_ = 0;
        clearIndirectCommand(indirectCommand_);
        clearIndexedCommand(indexedCommand_);
        return false;
    }
    if (candidates.size() > kMaxCandidates) {
        if (error) *error = "Candidate upload exceeds the 16384 candidate prototype limit";
        uploadedCount_ = uploadedAtlasCapacity_ = 0;
        clearIndirectCommand(indirectCommand_);
        clearIndexedCommand(indexedCommand_);
        return false;
    }
    for (const Candidate& candidate : candidates) {
        if (!isValidAtlasSlot(candidate.payload, atlasCapacity)) {
            if (error) *error = "Candidate payload is outside the current atlas slot range";
            uploadedCount_ = uploadedAtlasCapacity_ = 0;
            clearIndirectCommand(indirectCommand_);
            clearIndexedCommand(indexedCommand_);
            return false;
        }
    }
    if (candidates.empty()) {
        uploadedCount_ = uploadedAtlasCapacity_ = 0;
        clearIndirectCommand(indirectCommand_);
        clearIndexedCommand(indexedCommand_);
        return true;
    }
    if (!allocate(static_cast<std::uint32_t>(candidates.size()), error)) {
        uploadedCount_ = uploadedAtlasCapacity_ = 0;
        clearIndirectCommand(indirectCommand_);
        clearIndexedCommand(indexedCommand_);
        return false;
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, candidates_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    static_cast<GLsizeiptr>(candidates.size()) * sizeof(Candidate), candidates.data());
    if (glGetError() != GL_NO_ERROR) {
        uploadedCount_ = uploadedAtlasCapacity_ = 0;
        clearIndirectCommand(indirectCommand_);
        clearIndexedCommand(indexedCommand_);
        if (error) *error = "Candidate upload failed";
        return false;
    }
    uploadedCount_ = static_cast<std::uint32_t>(candidates.size());
    uploadedAtlasCapacity_ = atlasCapacity;
    return true;
}

bool Gles31Culler::dispatch(const Rect& view, std::uint32_t count, std::string* error) {
    if (!indirectCommand_ || !indexedCommand_) {
        if (error) *error = "Culler is not initialized";
        return false;
    }
    ScopedComputeState state;
    const auto failClosed = [&](const char* message) {
        uploadedCount_ = uploadedAtlasCapacity_ = 0;
        clearIndirectCommand(indirectCommand_);
        clearIndexedCommand(indexedCommand_);
        if (error) *error = message;
        return false;
    };
    if (!std::isfinite(view.left) || !std::isfinite(view.bottom) ||
        !std::isfinite(view.right) || !std::isfinite(view.top) ||
        view.left > view.right || view.bottom > view.top) {
        return failClosed("View rectangle must contain finite, non-inverted bounds");
    }
    if (count == 0) {
        clearIndirectCommand(indirectCommand_);
        clearIndexedCommand(indexedCommand_);
        uploadedCount_ = uploadedAtlasCapacity_ = 0;
        return true;
    }
    if (!classifyProgram_ || count > capacity_ || count != uploadedCount_ || uploadedAtlasCapacity_ == 0) {
        return failClosed("Culler is uninitialized or candidate count does not match the latest upload");
    }
    const GLuint groups = (count + kWorkgroupSize - 1u) / kWorkgroupSize;
    glUseProgram(classifyProgram_);
    glUniform4f(glGetUniformLocation(classifyProgram_, "uView"), view.left, view.bottom, view.right, view.top);
    glUniform1ui(glGetUniformLocation(classifyProgram_, "uCount"), count);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, candidates_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, flags_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, localOffsets_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, groupSums_);
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(offsetsProgram_);
    glUniform1ui(glGetUniformLocation(offsetsProgram_, "uGroupCount"), groups);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, groupSums_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, groupOffsets_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, indirectCommand_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, indexedCommand_);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(scatterProgram_);
    glUniform1ui(glGetUniformLocation(scatterProgram_, "uCount"), count);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, flags_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, localOffsets_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, groupOffsets_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, visibleIndices_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, candidates_);
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(atlasIndicesProgram_);
    glUniform1ui(glGetUniformLocation(atlasIndicesProgram_, "uCandidateCount"), count);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, visibleIndices_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, atlasElementIndices_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, indexedCommand_);
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT |
                    GL_ELEMENT_ARRAY_BARRIER_BIT);
    if (glGetError() != GL_NO_ERROR) {
        return failClosed("GLES compute dispatch failed");
    }
    return true;
}
}
#endif
