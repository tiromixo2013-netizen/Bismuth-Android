#include "bismuth/gles31_culler.hpp"
#include <cmath>
#if defined(__ANDROID__) && defined(BISMUTH_ANDROID_GLES31)
#include "embedded_shaders.hpp"
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
        for (GLuint i = 0; i < 4; ++i)
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &indexedStorage_[i]);
    }
    ~ScopedComputeState() {
        for (GLuint i = 0; i < 4; ++i)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, static_cast<GLuint>(indexedStorage_[i]));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(genericStorage_));
        glUseProgram(static_cast<GLuint>(program_));
    }
    ScopedComputeState(const ScopedComputeState&) = delete;
    ScopedComputeState& operator=(const ScopedComputeState&) = delete;
private:
    GLint program_ = 0;
    GLint genericStorage_ = 0;
    GLint indexedStorage_[4] = {0, 0, 0, 0};
};

void bufferData(GLuint id, GLsizeiptr bytes) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
}
}

Gles31Culler::~Gles31Culler() { reset(); }

void Gles31Culler::reset() noexcept {
    // GLES object deletion is context-dependent. The owner must call reset()
    // while the context that created these objects is current.
    const GLuint programs[] = {classifyProgram_, offsetsProgram_, scatterProgram_};
    if (programs[0] || programs[1] || programs[2])
        glDeleteProgram(programs[0]), glDeleteProgram(programs[1]), glDeleteProgram(programs[2]);
    const GLuint buffers[] = {candidates_, flags_, localOffsets_, groupSums_, groupOffsets_, visibleIndices_, indirectCommand_};
    bool hasBuffer = false;
    for (GLuint buffer : buffers) hasBuffer = hasBuffer || buffer != 0;
    if (hasBuffer) glDeleteBuffers(7, buffers);
    classifyProgram_ = offsetsProgram_ = scatterProgram_ = 0;
    candidates_ = flags_ = localOffsets_ = groupSums_ = 0;
    groupOffsets_ = visibleIndices_ = indirectCommand_ = 0;
    capacity_ = groupCapacity_ = uploadedCount_ = 0;
}

bool Gles31Culler::initialize(std::string* error) {
    if (classifyProgram_ || offsetsProgram_ || scatterProgram_) {
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
    if (maxBindings < 4 || maxInvocations < static_cast<GLint>(kWorkgroupSize) ||
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
    glGenBuffers(1, &candidates_); glGenBuffers(1, &flags_); glGenBuffers(1, &localOffsets_);
    glGenBuffers(1, &groupSums_); glGenBuffers(1, &groupOffsets_); glGenBuffers(1, &visibleIndices_);
    glGenBuffers(1, &indirectCommand_);
    if (glGetError() != GL_NO_ERROR || !candidates_ || !flags_ || !localOffsets_ ||
        !groupSums_ || !groupOffsets_ || !visibleIndices_ || !indirectCommand_) {
        if (error) *error = "Failed to create GLES storage buffers";
        reset(); return false;
    }
    // Allocate the indirect command immediately so an empty-scene dispatch is valid
    // even before the first non-empty candidate upload.
    bufferData(indirectCommand_, 4 * sizeof(GLuint));
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
    bufferData(indirectCommand_, 4 * sizeof(GLuint));
    if (glGetError() != GL_NO_ERROR) {
        capacity_ = groupCapacity_ = uploadedCount_ = 0;
        if (error) *error = "Failed to allocate GLES storage buffers";
        return false;
    }
    capacity_ = newCapacity; groupCapacity_ = groups;
    return true;
}

bool Gles31Culler::uploadCandidates(const std::vector<Candidate>& candidates, std::string* error) {
    if (!classifyProgram_ || !candidates_ || !indirectCommand_) {
        if (error) *error = "Culler is not initialized";
        return false;
    }
    ScopedComputeState state;
    if (candidates.size() > kMaxCandidates) {
        if (error) *error = "Candidate upload exceeds the 16384 candidate prototype limit";
        uploadedCount_ = 0; return false;
    }
    if (candidates.empty()) { uploadedCount_ = 0; return true; }
    if (!allocate(static_cast<std::uint32_t>(candidates.size()), error)) {
        uploadedCount_ = 0; return false;
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, candidates_);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                    static_cast<GLsizeiptr>(candidates.size()) * sizeof(Candidate), candidates.data());
    if (glGetError() != GL_NO_ERROR) {
        uploadedCount_ = 0;
        if (error) *error = "Candidate upload failed";
        return false;
    }
    uploadedCount_ = static_cast<std::uint32_t>(candidates.size());
    return true;
}

bool Gles31Culler::dispatch(const Rect& view, std::uint32_t count, std::string* error) {
    if (!indirectCommand_) {
        if (error) *error = "Culler is not initialized";
        return false;
    }
    ScopedComputeState state;
    if (!std::isfinite(view.left) || !std::isfinite(view.bottom) ||
        !std::isfinite(view.right) || !std::isfinite(view.top) ||
        view.left > view.right || view.bottom > view.top) {
        if (error) *error = "View rectangle must contain finite, non-inverted bounds";
        return false;
    }
    if (count == 0) {
        const GLuint emptyCommand[4] = {6u, 0u, 0u, 0u};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, indirectCommand_);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(emptyCommand), emptyCommand);
        if (glGetError() != GL_NO_ERROR) {
            if (error) *error = "Failed to clear the empty indirect draw command";
            return false;
        }
        uploadedCount_ = 0;
        return true;
    }
    if (!classifyProgram_ || count > capacity_ || count != uploadedCount_) {
        if (error) *error = "Culler is uninitialized or candidate count does not match the latest upload";
        return false;
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
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(scatterProgram_);
    glUniform1ui(glGetUniformLocation(scatterProgram_, "uCount"), count);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, flags_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, localOffsets_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, groupOffsets_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, visibleIndices_);
    glDispatchCompute(groups, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    if (glGetError() != GL_NO_ERROR) {
        if (error) *error = "GLES compute dispatch failed";
        return false;
    }
    return true;
}
}
#endif
