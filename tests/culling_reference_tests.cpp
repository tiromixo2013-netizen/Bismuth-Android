#include "bismuth/culling_reference.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace bismuth;
int main(){
    const Rect view{0,0,100,100};
    std::vector<Candidate> c={{{-10,10,10,20},0},{{110,10,120,20},1},{{20,20,30,30},2},{{100,100,110,110},3},{{40,40,40,40},4}};
    auto r=cullStable(c,view);
    assert((r.visibleIndices==std::vector<std::uint32_t>{0,2,3,4}));
    auto empty=cullStable({},view);assert(empty.visibleIndices.empty());
    const Rect inverted{10,10,0,0};
    auto none=cullStable(c,inverted);assert(none.visibleIndices.empty());
    std::cout<<"Bismuth-Android reference tests passed\n";
}
