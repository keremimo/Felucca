"""OB-Xf parameter conversions at their switching boundaries."""
import importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location('obxf_import', Path(__file__).resolve().parents[1]/'tools/obxf_import.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
assert len(m.layout())==95
assert m.to_value('CUT','C',0,127,.5)==8128
assert m.to_value('SYNC','D',0,1,.499)==0
assert m.to_value('SYNC','D',0,1,.5)==1
assert m.to_value('HQ','D',0,1,.5)==0
assert m.to_value('HQ','D',0,1,.501)==1
assert [m.to_value('NCOL','D',0,2,x) for x in [0,.33334,.66667,1]]==[0,1,2,2]
assert [m.to_value('LEG','D',0,3,x) for x in [0,.32,.34,.66,.67,1]]==[0,0,1,1,2,3]
assert [m.to_value('L1P1','D',0,2,x) for x in [0,.24,.25,.74,.75,1]]==[0,0,1,1,2,2]
assert [m.to_value('POLY','D',1,32,x) for x in [0,.5,1]]==[0,16,31]
assert [m.to_value('TRNS','D',-24,24,x) for x in [0,.5,1]]==[0,24,48]
assert [m.to_value('XPM','D',0,14,x) for x in [0,.5,1]]==[0,7,14]
print('OBXF importer: switching boundaries and 95-value schema passed')
