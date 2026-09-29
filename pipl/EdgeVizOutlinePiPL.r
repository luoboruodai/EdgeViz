#include "AEConfig.h"
#include "AE_EffectVers.h"
#include "AE_General.r"

resource 'PiPL' (16000) {
	{
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"EdgeViz"
		},
		/* [3] */
		Category {
			"PlugIn EdgeViz"
		},
		/* [4] ARM64 entry point */
		CodeMacARM64 {
			"EffectMain"
		},
		/* [5] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [6] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [7] PF_VERSION(3,1,2,RELEASE,1) = (3<<19)|(1<<15)|(2<<11)|(3<<9)|1 = 1611265 (0x189601) */
		AE_Effect_Version {
			1611265
		},
		/* [8] */
		AE_Effect_Info_Flags {
			0
		},
		/* [9] PIX_INDEPENDENT(1<<10) | USE_OUTPUT_EXTENT(1<<6) | WIDE_TIME_INPUT(1<<1) | NON_PARAM_VARY(1<<2) | SEND_UPDATE_PARAMS_UI(1<<26) = 67109958 */
		AE_Effect_Global_OutFlags {
			67109958
		},
		/* [10] I_USE_3D_CAMERA(1<<1) | PARAM_GROUP_START_COLLAPSED_FLAG(1<<3) = 10 */
		AE_Effect_Global_OutFlags_2 {
			10
		},
		/* [11] */
		AE_Effect_Match_Name {
			"com.edgeviz.outline"
		},
		/* [12] */
		AE_Reserved_Info {
			0
		}
	}
};
