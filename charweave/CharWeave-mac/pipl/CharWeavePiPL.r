#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "AE_General.r"

resource 'PiPL' (16000) {
    {
        Kind { AEEffect },
        Name { "CharWeave Native" },
        Category { "CharWeave" },
        CodeMacARM64 { "EffectMain" },
        CodeMacIntel64 { "EffectMain" },
        AE_PiPL_Version { 2, 0 },
        AE_Effect_Spec_Version { 13, 9 },
        AE_Effect_Version { 0x00022402 },
        AE_Effect_Info_Flags { 0 },
        AE_Effect_Global_OutFlags { 0x02000004 },
        AE_Effect_Global_OutFlags_2 { 0 },
        AE_Effect_Match_Name { "CW CharWeave Native" },
        AE_Reserved_Info { 0 }
    }
};
