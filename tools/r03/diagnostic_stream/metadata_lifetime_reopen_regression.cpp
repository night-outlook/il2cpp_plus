#include "AssemblyShadowMetadataLifetime.h"
#include <cstdio>
using il2cpp::vm::assembly_shadow_metadata_lifetime::Gate;
int main()
{
    Gate g;
    if(!g.OpenAfterInit())return 1;
    {Gate::Reader first(g);if(!first)return 2;}
    g.CloseAndDrain();
    if(!g.OpenAfterInit())return 3;
    Gate::Reader retainedSource(g);
    if(retainedSource){std::fputs("FAIL: unversioned retained source admitted after reopen\n",stderr);return 4;}
    std::puts("PASS: retained initial-runtime source rejected after reopen");
}
