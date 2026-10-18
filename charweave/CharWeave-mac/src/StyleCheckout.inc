class StyleCheckout {
 public:
  explicit StyleCheckout(PF_InData *in):in_(in) {}
  ~StyleCheckout() { if(checked_) PF_CHECKIN_PARAM(in_,&param_); }
  PF_Err Get() {
    if(!in_ || !in_->inter.checkout_param || !in_->inter.checkin_param)
      return PF_Err_BAD_CALLBACK_PARAM;
    PF_Err e=PF_CHECKOUT_PARAM(in_,P_STYLE_LAYER,in_->current_time,
                             in_->time_step,in_->time_scale,&param_);
    checked_=e==PF_Err_NONE;
    return e;
  }
  const PF_EffectWorld *world() const { return &param_.u.ld; }
  StyleCheckout(const StyleCheckout &)=delete;
  StyleCheckout &operator=(const StyleCheckout &)=delete;
 private:
  PF_InData *in_=nullptr;
  PF_ParamDef param_{};
  bool checked_=false;
};
static PF_Err RenderFrame(PF_InData *in,PF_OutData *out,PF_ParamDef *params[],PF_EffectWorld *dst) {
  if(!in || !params || !params[0] || !dst) return PF_Err_BAD_CALLBACK_PARAM;
  StyleCheckout library(in);
  const PF_EffectWorld *styles=nullptr;
  if(ReadSettings(in,params).glyphSource==2) {
    PF_Err e=library.Get();
    if(e) return e;
    styles=library.world();
    if(!styles->data || styles->width<8 || styles->height<8) {
      if(out) std::snprintf(out->return_msg,sizeof(out->return_msg),
          "Select a valid Style library layer, or click Create style precomp.");
      return PF_Err_BAD_CALLBACK_PARAM;
    }
  }
  try {
    return PF_WORLD_IS_DEEP(dst)
        ? RenderTyped<PF_Pixel16,32768>(in,params,&params[0]->u.ld,dst,styles)
        : RenderTyped<PF_Pixel8,255>(in,params,&params[0]->u.ld,dst,styles);
  } catch(const std::bad_alloc &) {return PF_Err_OUT_OF_MEMORY;}
}
