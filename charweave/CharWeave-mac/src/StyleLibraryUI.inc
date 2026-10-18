/* Never execute scripts or query dynamic effect context inside PF callbacks.
 * Native buttons only enqueue work; AEGP's main-thread idle hook runs the JSX.
 */
#include "StyleLibraryScript.inc"
#ifndef CHARWEAVE_UI_TRACE
#define CHARWEAVE_UI_TRACE 0
#endif
static void StyleUITrace(const char *stage){
#if CHARWEAVE_UI_TRACE
  if(FILE *f=std::fopen("/tmp/charweave-style-ui-trace.log","a")){
    std::fprintf(f,"%s\n",stage);std::fclose(f);
  }
#else
  (void)stage;
#endif
}
template <class T> class UISuite {
 public:
  UISuite(PF_InData *in,const char *name,A_long version):basic_(in?in->pica_basicP:nullptr),name_(name),version_(version) {
    if(basic_ && basic_->AcquireSuite(name,version,&raw_)!=0)raw_=nullptr;
  }
  ~UISuite(){if(raw_)basic_->ReleaseSuite(name_,version_);}
  const T *operator->()const{return static_cast<const T *>(raw_);}
  explicit operator bool()const{return raw_!=nullptr;}
  UISuite(const UISuite &)=delete;UISuite &operator=(const UISuite &)=delete;
 private:SPBasicSuite *basic_=nullptr;const char *name_;A_long version_;const void *raw_=nullptr;
};
static AEGP_PluginID gStylePluginID=0;
static SPBasicSuite *gStyleBasic=nullptr; // Host-owned, valid during the AE session.
static bool gStyleIdleRegistered=false,gStyleDead=false,gStyleWatchInitialized=false,gStyleBootPending=false;
static int gStylePendingAction=0;
static std::chrono::steady_clock::time_point gStyleWatchAttempt{};
static PF_Err StyleUIError(PF_OutData *out,const char *text){
  if(out){std::snprintf(out->return_msg,sizeof(out->return_msg),"%s",text);out->out_flags|=PF_OutFlag_DISPLAY_ERROR_MESSAGE;}
  return PF_Err_NONE;
}
static bool RunStyleScript(PF_InData *in,const std::string &script,std::string &error){
  UISuite<AEGP_UtilitySuite5> utility(in,kAEGPUtilitySuite,kAEGPUtilitySuiteVersion5);
  UISuite<AEGP_MemorySuite1> memory(in,kAEGPMemorySuite,kAEGPMemorySuiteVersion1);
  if(!utility || !memory){error="AE scripting suites unavailable.";return false;}
  A_Boolean available=FALSE;
  if(utility->AEGP_IsScriptingAvailable(&available) || !available){error="AE scripting is unavailable.";return false;}
  AEGP_MemHandle result=nullptr,errH=nullptr;
  StyleUITrace("EXECUTE before");
  A_Err e=utility->AEGP_ExecuteScript(gStylePluginID,script.c_str(),FALSE,&result,&errH);
  StyleUITrace("EXECUTE after");
  if(errH){void *p=nullptr;if(!memory->AEGP_LockMemHandle(errH,&p)){if(p)error=static_cast<const char *>(p);memory->AEGP_UnlockMemHandle(errH);}memory->AEGP_FreeMemHandle(errH);}
  if(result)memory->AEGP_FreeMemHandle(result);
  if(e && error.empty())error="AE could not execute the style manager.";
  return !e && error.empty();
}
static A_Err StyleIdle(AEGP_GlobalRefcon,AEGP_IdleRefcon,A_long *sleep){
  if(gStylePendingAction || gStyleBootPending)StyleUITrace("IDLE pending entry");
  if(sleep)*sleep=std::min<A_long>(*sleep,60); // SDK units: 1/60 second.
  if(gStyleDead || !gStyleBasic || (!gStyleBootPending && !gStylePendingAction))return 0;
  const int action=gStylePendingAction;gStylePendingAction=0;gStyleBootPending=false;
  PF_InData context{};context.pica_basicP=gStyleBasic;
  std::string script(kStyleLibraryScript);
  script+="\n$.global.CWStyleLibrary.start();\n";
  if(action)script+="$.global.CWStyleLibrary.runForSelection("+std::to_string(action)+");\n";
  std::string error;
  gStyleWatchInitialized=RunStyleScript(&context,script,error);
  if(!error.empty()){
    UISuite<AEGP_UtilitySuite5> utility(&context,kAEGPUtilitySuite,kAEGPUtilitySuiteVersion5);
    if(utility)utility->AEGP_ReportInfo(gStylePluginID,error.c_str());
  }
  return 0;
}
static A_Err StyleDeath(AEGP_GlobalRefcon,AEGP_DeathRefcon){
  gStyleDead=true;gStylePendingAction=0;gStyleBootPending=false;gStyleBasic=nullptr;return 0;
}
static bool EnsureIdleBridge(PF_InData *in,std::string &error){
  StyleUITrace("BRIDGE entry");
  if(!in || ![NSThread isMainThread] || gStyleDead){error="Interactive AE main thread required.";return false;}
  if(gStyleIdleRegistered)return true;
  UISuite<AEGP_UtilitySuite5> utility(in,kAEGPUtilitySuite,kAEGPUtilitySuiteVersion5);
  UISuite<AEGP_RegisterSuite5> hooks(in,kAEGPRegisterSuite,kAEGPRegisterSuiteVersion5);
  if(!utility || !hooks){error="AE idle registration suites unavailable. Use the Styles Manager script.";return false;}
  StyleUITrace("BRIDGE suites acquired");
  A_Boolean suppressed=FALSE;
  if(utility->AEGP_GetSuppressInteractiveUI(&suppressed) || suppressed){error="Style UI is disabled in render-only instances.";return false;}
  StyleUITrace("REGISTER plugin before");
  if(!gStylePluginID && utility->AEGP_RegisterWithAEGP(nullptr,"CharWeave Style Library",&gStylePluginID)){error="Could not register style helper.";return false;}
  StyleUITrace("REGISTER plugin after");
  gStyleBasic=in->pica_basicP;
  StyleUITrace("REGISTER idle before");
  if(hooks->AEGP_RegisterIdleHook(gStylePluginID,StyleIdle,nullptr)){gStyleBasic=nullptr;error="Could not register the style idle hook.";return false;}
  StyleUITrace("REGISTER idle after");
  gStyleIdleRegistered=true;
  hooks->AEGP_RegisterDeathHook(gStylePluginID,StyleDeath,nullptr);
  return true;
}
static void InitializeStyleWatch(PF_InData *in){
  if(![NSThread isMainThread])return;
  const auto now=std::chrono::steady_clock::now();if(now-gStyleWatchAttempt<std::chrono::seconds(1))return;gStyleWatchAttempt=now;
  std::string error;if(EnsureIdleBridge(in,error))gStyleBootPending=true;
}
static PF_Err QueueStyleUI(PF_InData *in,PF_OutData *out,int action){
  StyleUITrace("BUTTON callback entry");
  std::string error;if(!EnsureIdleBridge(in,error))return StyleUIError(out,error.c_str());
  // No ExecuteScript or PF-interface/stream queries here. Select one target
  // layer in the active comp; the idle JSX validates the exact single effect.
  gStylePendingAction=action;gStyleBootPending=true;StyleUITrace("BUTTON queued");return PF_Err_NONE;
}
