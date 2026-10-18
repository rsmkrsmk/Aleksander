import sys,subprocess,glob,os
from PIL import Image
sc,name,t_end,step=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4])
d=f'prev/{name}_f'; os.makedirs(d,exist_ok=True)
for f in glob.glob(d+'/*'): os.remove(f)
ts=','.join(str(t) for t in range(0,t_end+1,step))
subprocess.run(['./sim_ui',sc,d+'/f',ts],check=True)
subprocess.run(['python3','raw2png.py']+sorted(glob.glob(d+'/*.raw')),check=True)
fr=[Image.open(f).convert('RGB').resize((360,360),Image.LANCZOS) for f in sorted(glob.glob(d+'/*.png'))]
fr=[f.quantize(128,dither=Image.Dither.NONE) for f in fr]
fr[0].save(f'prev/{name}.gif',save_all=True,append_images=fr[1:],duration=step,loop=0,optimize=True)
print(name,len(fr),os.path.getsize(f'prev/{name}.gif')//1024,'KB')
