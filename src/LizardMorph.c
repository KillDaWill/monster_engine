#include "LizardMorph.h"
LizardMorph* LizardMorph_Create(void) { return AnatomyDeformer_Create(); }
void LizardMorph_Free(LizardMorph* m) { AnatomyDeformer_Free(m); }
bool LizardMorph_Bind(LizardMorph* m,const Mesh* mesh,const AnatomyGraph* g) { return AnatomyDeformer_Bind(m,mesh,g); }
bool LizardMorph_Deform(const LizardMorph* m,const AnatomyGraph* g,Mesh* mesh) { return AnatomyDeformer_Deform(m,g,mesh); }
bool LizardMorph_IsBound(const LizardMorph* m) { return AnatomyDeformer_IsBound(m); }
size_t LizardMorph_GetVertexCount(const LizardMorph* m) { return AnatomyDeformer_GetVertexCount(m); }
