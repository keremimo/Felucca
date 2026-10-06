/* Host bridge: actual C port, native parameters and deterministic voice state. */
#define main hostsim_main
#include "hostsim.c"
#undef main
static oxf_part_t part;
static int feature;
const void *obxf_parity_setup(int f)
{
    uint16_t v[OX_NP]; int16_t e[7]={0};
    feature=f; memset(&part,0,sizeof part); obxf_part_init(&part,0);
    memcpy(v,OXF_INIT,sizeof v);
    v[OX_SPOR]=v[OX_SCUT]=v[OX_SENV]=v[OX_SLVL]=0;
    v[OX_CUT]=oxf_from_ui(OX_CUT,80); v[OX_RES]=oxf_from_ui(OX_RES,40);
    v[OX_AR]=oxf_from_ui(OX_AR,20);
    switch(f) {
    case 1: v[OX_SAW1]=0;v[OX_PUL1]=1;v[OX_PW]=oxf_from_ui(OX_PW,50);break;
    case 2: v[OX_SAW1]=0;break;
    case 3: v[OX_PUL1]=1;break;
    case 4: v[OX_SYNC]=1;v[OX_MIX2]=OXF_ONE;v[OX_O2P]=oxf_from_ui(OX_O2P,7);break;
    case 5: v[OX_XMOD]=oxf_from_ui(OX_XMOD,30);v[OX_MIX2]=OXF_ONE;break;
    case 6: v[OX_RING]=OXF_ONE;v[OX_MIX2]=OXF_ONE;break;
    case 7: case 8: case 9: v[OX_MIX1]=0;v[OX_NOISE]=OXF_ONE;v[OX_NCOL]=f-7;break;
    case 10: v[OX_BPB]=1;v[OX_MODE]=oxf_from_ui(OX_MODE,35);break;
    case 11: v[OX_PUSH]=1;v[OX_RES]=OXF_ONE;v[OX_CUT]=oxf_from_ui(OX_CUT,50);break;
    case 12: case 13: v[OX_FOUR]=1;v[OX_MODE]=f==13?oxf_from_ui(OX_MODE,70):0;break;
    case 29: v[OX_FA]=oxf_from_ui(OX_FA,30);v[OX_FD]=oxf_from_ui(OX_FD,50);v[OX_FS]=oxf_from_ui(OX_FS,25);v[OX_FAMT]=oxf_from_ui(OX_FAMT,30);break;
    case 30: v[OX_AA]=oxf_from_ui(OX_AA,30);v[OX_AD]=oxf_from_ui(OX_AD,50);v[OX_AS]=oxf_from_ui(OX_AS,40);v[OX_ACRV]=OXF_ONE;break;
    case 31: case 32: v[f==31?OX_L1A1:OX_L2A1]=oxf_from_ui(OX_L1A1,30);v[f==31?OX_L1P1:OX_L2P1]=1;break;
    case 33: v[OX_PORTA]=oxf_from_ui(OX_PORTA,65);break;
    case 34: v[OX_UDET]=oxf_from_ui(OX_UDET,110);break;
    case 35: v[OX_L1A2]=oxf_from_ui(OX_L1A2,100);v[OX_L1PW1]=1;v[OX_L1VOL]=1;v[OX_PUL1]=1;v[OX_L1W2]=0;break;
    case 36: v[OX_L2W3]=OXF_ONE;v[OX_L2A1]=oxf_from_ui(OX_L2A1,30);v[OX_L2CUT]=1;break;
    case 37: v[OX_L1SYNC]=1;v[OX_L1A1]=oxf_from_ui(OX_L1A1,30);v[OX_L1P1]=2;break;
    }
    if(f>=14 && f<29) { v[OX_FOUR]=1;v[OX_XPD]=1;v[OX_XPM]=f-14; }
    obxf_par_make(&part.par,v,e); part.sm_cut=part.par.cut;part.sm_res=part.par.res;part.sm_mode=part.par.mode;
    oxf_voice_t *s=&part.v[0];oxf_voice_init(s,&part.seed);
    s->o[0].ph=s->o[1].ph=0; s->o[0].slop=-.25f;s->o[1].slop=.25f;
    s->nz.white=17;s->cut_nz=23;
    s->lfo2.rng=part.lfo1.rng=part.vib.rng=1;
    s->lfo2.sh=s->lfo2.hist=part.lfo1.sh=part.lfo1.hist=0;
    oxf_note_on(&part,s,69,0.8f,0);
    return &part.par;
}
void obxf_parity_block(float *out, int index)
{
    int32_t buf[CTL]={0}; oxf_voice_t *v=&part.v[0];
    if(index==256) oxf_note_on(&part,v,feature==33?81:69,0.8f,1);
    if(index==768) oxf_note_off(&part,v);
    oxf_part_block(&part,CTL,0,feature==38?0.5f:0,120);
    oxf_voice_render(&part,v,buf,CTL,0,0,0,16777216.f,16777216.f,120);
    for(int i=0;i<CTL;i++) out[i]=buf[i]*(1.f/16777216.f);
}
