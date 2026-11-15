#include "bismuth/gles31_culler.hpp"
#if defined(__ANDROID__) && defined(BISMUTH_ANDROID_GLES31)
#include "embedded_shaders.hpp"
#include <algorithm>
#include <limits>
namespace bismuth {
namespace {
GLuint compileCompute(const char* source, std::string* error) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &source, nullptr); glCompileShader(shader);
    GLint ok = GL_FALSE; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) { GLint n=0; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &n); std::string log(n>0?static_cast<size_t>(n):1,'\\0'); glGetShaderInfoLog(shader,n,nullptr,log.data()); if(error)*error=log; glDeleteShader(shader); return 0; }
    GLuint program=glCreateProgram(); glAttachShader(program,shader); glLinkProgram(program); glDeleteShader(shader);
    glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){GLint n=0;glGetProgramiv(program,GL_INFO_LOG_LENGTH,&n);std::string log(n>0?static_cast<size_t>(n):1,'\\0');glGetProgramInfoLog(program,n,nullptr,log.data());if(error)*error=log;glDeleteProgram(program);return 0;}
    return program;
}
void bufferData(GLuint id, GLsizeiptr bytes) { glBindBuffer(GL_SHADER_STORAGE_BUFFER,id); glBufferData(GL_SHADER_STORAGE_BUFFER,bytes,nullptr,GL_DYNAMIC_DRAW); }
}
Gles31Culler::~Gles31Culler(){reset();}
void Gles31Culler::reset() noexcept {
    GLuint programs[]={classifyProgram_,offsetsProgram_,scatterProgram_}; glDeleteProgram(programs[0]);glDeleteProgram(programs[1]);glDeleteProgram(programs[2]);
    GLuint buffers[]={candidates_,flags_,localOffsets_,groupSums_,groupOffsets_,visibleIndices_,indirectCommand_};glDeleteBuffers(7,buffers);
    classifyProgram_=offsetsProgram_=scatterProgram_=0;candidates_=flags_=localOffsets_=groupSums_=groupOffsets_=visibleIndices_=indirectCommand_=0;capacity_=groupCapacity_=0;
}
bool Gles31Culler::initialize(std::string* error){
    GLint major=0,minor=0;glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    if(major<3||(major==3&&minor<1)){if(error)*error="OpenGL ES 3.1 is required";return false;}
    classifyProgram_=compileCompute(shaders::classify,error);if(!classifyProgram_)return false;
    offsetsProgram_=compileCompute(shaders::offsets,error);if(!offsetsProgram_)return false;
    scatterProgram_=compileCompute(shaders::scatter,error);if(!scatterProgram_)return false;
    glGenBuffers(1,&candidates_);glGenBuffers(1,&flags_);glGenBuffers(1,&localOffsets_);glGenBuffers(1,&groupSums_);glGenBuffers(1,&groupOffsets_);glGenBuffers(1,&visibleIndices_);glGenBuffers(1,&indirectCommand_);
    return glGetError()==GL_NO_ERROR;
}
bool Gles31Culler::allocate(std::uint32_t count,std::string* error){
    if(count<=capacity_)return true;
    const std::uint32_t groups=(count+127u)/128u;
    if(groups>128u){if(error)*error="Prototype currently supports at most 16384 candidates per dispatch";return false;}
    capacity_=groups*128u;groupCapacity_=groups;
    bufferData(candidates_,static_cast<GLsizeiptr>(capacity_)*sizeof(Candidate));
    bufferData(flags_,static_cast<GLsizeiptr>(capacity_)*sizeof(GLuint));bufferData(localOffsets_,static_cast<GLsizeiptr>(capacity_)*sizeof(GLuint));
    bufferData(groupSums_,static_cast<GLsizeiptr>(groupCapacity_)*sizeof(GLuint));bufferData(groupOffsets_,static_cast<GLsizeiptr>(groupCapacity_)*sizeof(GLuint));
    bufferData(visibleIndices_,static_cast<GLsizeiptr>(capacity_)*sizeof(GLuint));bufferData(indirectCommand_,4*sizeof(GLuint));
    if(glGetError()!=GL_NO_ERROR){if(error)*error="Failed to allocate GLES storage buffers";return false;}return true;
}
bool Gles31Culler::uploadCandidates(const std::vector<Candidate>& c,std::string* error){
    if(c.empty())return true;if(!allocate(static_cast<std::uint32_t>(c.size()),error))return false;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,candidates_);glBufferSubData(GL_SHADER_STORAGE_BUFFER,0,static_cast<GLsizeiptr>(c.size())*sizeof(Candidate),c.data());
    if(glGetError()!=GL_NO_ERROR){if(error)*error="Candidate upload failed";return false;}return true;
}
bool Gles31Culler::dispatch(const Rect& view,std::uint32_t count,std::string* error){
    if(count==0)return true;if(count>capacity_){if(error)*error="Candidate count exceeds allocated capacity";return false;}
    glUseProgram(classifyProgram_);glUniform4f(glGetUniformLocation(classifyProgram_,"uView"),view.left,view.bottom,view.right,view.top);glUniform1ui(glGetUniformLocation(classifyProgram_,"uCount"),count);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,candidates_);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,flags_);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,localOffsets_);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,groupSums_);
    glDispatchCompute((count+127u)/128u,1,1);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(offsetsProgram_);glUniform1ui(glGetUniformLocation(offsetsProgram_,"uGroupCount"),(count+127u)/128u);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,groupSums_);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,groupOffsets_);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,indirectCommand_);
    glDispatchCompute(1,1,1);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(scatterProgram_);glUniform1ui(glGetUniformLocation(scatterProgram_,"uCount"),count);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,flags_);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,localOffsets_);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,groupOffsets_);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,visibleIndices_);
    glDispatchCompute((count+127u)/128u,1,1);glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT|GL_COMMAND_BARRIER_BIT|GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    if(glGetError()!=GL_NO_ERROR){if(error)*error="GLES compute dispatch failed";return false;}return true;
}
}
#endif
