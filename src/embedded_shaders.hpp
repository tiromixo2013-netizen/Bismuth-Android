#pragma once
namespace bismuth::shaders {
inline constexpr const char* classify = R"GLSL(#version 310 es
precision highp float; precision highp int;
layout(local_size_x=128) in;
struct Candidate { vec4 bounds; uint payload; uint p0; uint p1; uint p2; };
layout(std430,binding=0) readonly buffer Candidates { Candidate items[]; };
layout(std430,binding=1) writeonly buffer Flags { uint flags[]; };
layout(std430,binding=2) writeonly buffer LocalOffsets { uint localOffsets[]; };
layout(std430,binding=3) writeonly buffer GroupSums { uint groupSums[]; };
uniform vec4 uView; uniform uint uCount; shared uint scan[128];
void main(){ uint i=gl_GlobalInvocationID.x,l=gl_LocalInvocationID.x,g=gl_WorkGroupID.x; uint v=0u;
if(i<uCount){vec4 b=items[i].bounds;v=uint(b.z>=uView.x&&b.x<=uView.z&&b.w>=uView.y&&b.y<=uView.w);flags[i]=v;}
scan[l]=v;barrier();for(uint s=1u;s<128u;s<<=1u){uint a=l>=s?scan[l-s]:0u;barrier();scan[l]+=a;barrier();}
if(i<uCount)localOffsets[i]=scan[l]-v;if(l==127u)groupSums[g]=scan[l];}
)GLSL";
inline constexpr const char* offsets = R"GLSL(#version 310 es
precision highp int; layout(local_size_x=128) in;
layout(std430,binding=0) readonly buffer GroupSums{uint groupSums[];};
layout(std430,binding=1) writeonly buffer GroupOffsets{uint groupOffsets[];};
layout(std430,binding=2) buffer IndirectCommand{uint vertexCount;uint instanceCount;uint firstVertex;uint baseInstance;};
uniform uint uGroupCount;shared uint scan[128];
void main(){uint l=gl_LocalInvocationID.x;uint v=l<uGroupCount?groupSums[l]:0u;scan[l]=v;barrier();
for(uint s=1u;s<128u;s<<=1u){uint a=l>=s?scan[l-s]:0u;barrier();scan[l]+=a;barrier();}
if(l<uGroupCount)groupOffsets[l]=scan[l]-v;if(l==127u)instanceCount=scan[l];if(l==0u){vertexCount=6u;firstVertex=0u;baseInstance=0u;}}
)GLSL";
inline constexpr const char* scatter = R"GLSL(#version 310 es
precision highp int;layout(local_size_x=128) in;
layout(std430,binding=0) readonly buffer Flags{uint flags[];};
layout(std430,binding=1) readonly buffer LocalOffsets{uint localOffsets[];};
layout(std430,binding=2) readonly buffer GroupOffsets{uint groupOffsets[];};
layout(std430,binding=3) writeonly buffer VisibleIndices{uint visibleIndices[];};uniform uint uCount;
void main(){uint i=gl_GlobalInvocationID.x;if(i>=uCount||flags[i]==0u)return;visibleIndices[groupOffsets[gl_WorkGroupID.x]+localOffsets[i]]=i;}
)GLSL";
}
